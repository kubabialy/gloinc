#include "sema.h"
#include "ast_clone.h"
#include <functional>
#include <set>

std::optional<ValueType> Sema::value_type(const std::shared_ptr<Type> &type) const {
    if (!type)
        return std::nullopt;
    if (const auto *pointer = dynamic_cast<const PointerType *>(type.get())) {
        auto value = value_type(pointer->pointee);
        if (!value)
            return std::nullopt;
        value->pointers.insert(value->pointers.begin(), {pointer->nullable, pointer->read_only});
        return value;
    }
    if (const auto *array = dynamic_cast<const ArrayType *>(type.get())) {
        auto element = value_type(array->element);
        return element ? std::optional<ValueType>(ValueType::array(*element, array->length))
                       : std::nullopt;
    }
    if (const auto *structure = dynamic_cast<const StructType *>(type.get())) {
        if (structure->identity)
            return ValueType::record(*structure->identity);
        return std::nullopt;
    }
    if (auto core = resolve_core_type(type->to_string()))
        return ValueType(*core);
    return std::nullopt;
}

void Sema::collect_structs(const std::vector<std::unique_ptr<Statement>> &program) {
    std::vector<std::pair<const StructDefinition *, std::shared_ptr<StructType>>> declarations;
    for (const auto &statement : program) {
        const auto *definition = dynamic_cast<const StructDefinition *>(statement.get());
        if (!definition)
            continue;
        DiagnosticScope location(current_span, definition->span);
        if (!definition->name || definition->is_packed || definition->backing_type) {
            log_error("Packed structs are not supported in checked programs");
            continue;
        }
        const auto &name = definition->name->value;
        const auto primitive = standard_operation(name, standard_primitive_names);
        if (get_builtin_type(name) || current_scope->types.contains(name) ||
            current_scope->generic_structs.contains(name) ||
            current_scope->symbols.contains(name) || imports.contains(name) ||
            (current_module && !current_module->standard_name.empty() && name == "__write_stdout") ||
            (current_module && primitive &&
             standard_primitive_allowed(*primitive, current_module->standard_name))) {
            log_error("Duplicate or reserved struct name '" + name + "'");
            continue;
        }
        if (!definition->generic_params.empty()) {
            std::set<std::string> parameters;
            bool valid = true;
            for (const auto &parameter : definition->generic_params)
                if (!parameters.insert(parameter).second || get_builtin_type(parameter)) {
                    log_error("Duplicate or reserved generic parameter '" + parameter + "'");
                    valid = false;
                }
            std::set<std::string> field_names;
            for (const auto &field : definition->fields) {
                if (!field.name || !field.type || field.offset != -1) {
                    log_error("Invalid generic struct field declaration");
                    valid = false;
                } else if (!field_names.insert(field.name->value).second) {
                    log_error("Duplicate field '" + field.name->value + "'");
                    valid = false;
                }
            }
            for (const auto &method : definition->methods) {
                if (!method->name || !field_names.insert(method->name->value).second) {
                    log_error("Duplicate field or method in generic struct '" + name + "'");
                    valid = false;
                }
                if (!method->generic_params.empty()) {
                    log_error("Generic methods with their own type parameters await SPEC-033");
                    valid = false;
                }
            }
            if (valid) {
                current_scope->generic_structs.emplace(name, definition);
                generic_struct_owners.emplace(definition, current_module);
            }
            continue;
        }
        auto structure =
            std::make_shared<StructType>(name, std::vector<StructType::Field>{}, false);
        structure->identity = recording->structures.size();
        structure->owner = current_module;
        structure->is_public = definition->is_public;
        recording->structures.push_back(
            {current_module ? current_module->module_name + "." + name : name, {}});
        current_scope->define_type(name, structure);
        recording->types[definition] = ValueType::record(*structure->identity);
        declarations.emplace_back(definition, structure);
        collected_struct_types.push_back(structure);
    }
    for (const auto &[definition, structure] : declarations) {
        std::set<std::string> names;
        for (const auto &field : definition->fields) {
            DiagnosticScope location(current_span,
                                     field.name ? field.name->span : definition->span);
            if (!field.name || !field.type || field.offset != -1) {
                log_error("Invalid ordinary struct field declaration");
                continue;
            }
            if (!names.insert(field.name->value).second) {
                log_error("Duplicate field '" + field.name->value + "'");
                continue;
            }
            auto type = resolve_annotation(field.type.get());
            if (!type)
                continue;
            structure->fields.push_back({field.name->value, type, field.is_public});
            recording->structures.at(*structure->identity)
                .fields.push_back(
                    {field.name->value, *value_type(type), field.is_public, field.is_mutable});
        }
    }
}

std::shared_ptr<StructType>
Sema::specialize_struct(const StructDefinition *definition,
                        const std::vector<std::shared_ptr<Type>> &arguments) {
    std::vector<ValueType> keys;
    for (const auto &argument : arguments) {
        auto value = value_type(argument);
        if (!value)
            return nullptr;
        keys.push_back(*value);
    }
    for (const auto &specialization : generic_specializations)
        if (specialization.definition == definition && specialization.arguments == keys)
            return specialization.type;
    if (generic_specializations.size() >= 256) {
        log_error("Generic struct specialization limit of 256 exceeded");
        return nullptr;
    }
    if (generic_specialization_depth >= 64) {
        log_error("Generic struct specialization exceeds 64 nested applications");
        return nullptr;
    }
    struct DepthGuard {
        size_t &depth;
        explicit DepthGuard(size_t &depth) : depth(depth) { ++depth; }
        ~DepthGuard() { --depth; }
    } depth_guard(generic_specialization_depth);

    const auto *owner = generic_struct_owners.at(definition);
    std::string name = owner ? owner->module_name + "." : "";
    name += definition->name->value + "<";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i)
            name += ", ";
        name += arguments[i]->to_string();
    }
    name += ">";
    auto structure = std::make_shared<StructType>(name, std::vector<StructType::Field>{}, false);
    structure->identity = recording->structures.size();
    structure->owner = owner;
    structure->is_public = definition->is_public;
    recording->structures.push_back({name, {}});
    collected_struct_types.push_back(structure);
    generic_specializations.push_back({definition, std::move(keys), structure, arguments, {}});
    const size_t specialization_index = generic_specializations.size() - 1;

    auto saved_scope = current_scope;
    auto saved_module = current_module;
    auto saved_imports = imports;
    current_scope = std::make_shared<Scope>(module_scopes.at(owner));
    current_module = owner;
    imports = module_imports.at(owner);
    for (size_t i = 0; i < arguments.size(); ++i)
        current_scope->define_type(definition->generic_params[i], arguments[i]);
    std::set<std::string> names;
    for (const auto &field : definition->fields) {
        DiagnosticScope location(current_span, field.name ? field.name->span : definition->span);
        if (!field.name || !field.type || field.offset != -1) {
            log_error("Invalid generic struct field declaration");
            continue;
        }
        if (!names.insert(field.name->value).second) {
            log_error("Duplicate field '" + field.name->value + "'");
            continue;
        }
        auto type = resolve_type_from_string(field.type->value);
        auto value = value_type(type);
        if (!value || dynamic_cast<VoidType *>(type.get())) {
            log_error("Unknown or invalid field type '" + field.type->value + "' in " + name);
            continue;
        }
        structure->fields.push_back({field.name->value, type, field.is_public});
        recording->structures.at(*structure->identity)
            .fields.push_back({field.name->value, *value, field.is_public, field.is_mutable});
    }
    for (const auto &method : definition->methods) {
        auto instance = clone_function(*method);
        auto *function = instance.get();
        recording->specialized_methods.push_back(std::move(instance));
        // Field specialization above may append other entries to this vector.
        generic_specializations[specialization_index].method_instances.push_back(function);
        collect_method(structure, function);
    }
    current_scope = saved_scope;
    current_module = saved_module;
    imports = std::move(saved_imports);
    return structure;
}

void Sema::validate_struct_cycles() {
    std::vector<unsigned> state(recording->structures.size());
    std::function<bool(size_t)> visit;
    std::function<bool(const ValueType &)> visit_value = [&](const ValueType &type) {
        if (type.is_pointer())
            return true;
        if (type.is_array())
            return visit_value(*type.array_element);
        return !type.structure || visit(*type.structure);
    };
    visit = [&](size_t id) {
        if (state[id] == 1)
            return false;
        if (state[id] == 2)
            return true;
        state[id] = 1;
        for (const auto &field : recording->structures[id].fields)
            if (!visit_value(field.type))
                return false;
        state[id] = 2;
        return true;
    };
    for (const auto &[node, type] : recording->types) {
        if (!dynamic_cast<const StructDefinition *>(node) || !type.structure)
            continue;
        if (!visit(*type.structure)) {
            DiagnosticScope location(current_span, node->span);
            log_error("Recursive by-value struct has infinite size: " +
                      recording->structures[*type.structure].name);
            // Break semantic shared_ptr cycles before returning a failed program.
            for (auto &structure : collected_struct_types)
                structure->fields.clear();
            break;
        }
    }
    if (!has_error())
        for (const auto &specialization : generic_specializations) {
            const auto id = *specialization.type->identity;
            if (!visit(id)) {
                DiagnosticScope location(current_span, specialization.definition->span);
                log_error("Recursive by-value struct has infinite size: " +
                          recording->structures[id].name);
                for (auto &structure : collected_struct_types)
                    structure->fields.clear();
                break;
            }
        }
}

std::shared_ptr<Type> Sema::check_struct_literal(const StructLiteral *literal) {
    auto type = literal->name ? resolve_type_from_string(literal->name->value) : nullptr;
    auto structure = std::dynamic_pointer_cast<StructType>(type);
    if (!structure || !structure->identity) {
        log_error("Unknown or inaccessible struct type in literal");
        return nullptr;
    }
    const auto &fields = recording->structures.at(*structure->identity).fields;
    std::set<size_t> initialized;
    std::vector<size_t> indices;
    for (const auto &[name, expression] : literal->fields) {
        DiagnosticScope location(current_span, expression ? expression->span : literal->span);
        const auto *field = structure->get_field(name);
        if (!field) {
            log_error("Unknown field '" + name + "' in " + structure->name);
            continue;
        }
        size_t index = field - structure->fields.data();
        if (!initialized.insert(index).second)
            log_error("Duplicate initializer for field '" + name + "'");
        if (!field->is_public && structure->owner != current_module)
            log_error("Private field '" + name + "'");
        auto initializer = check_typed_expression(expression.get(), field->type);
        if (initializer && !initializer->equals(*field->type))
            log_error("Type mismatch in initializer for field '" + name + "'");
        indices.push_back(index);
    }
    for (size_t i = 0; i < fields.size(); ++i)
        if (!initialized.contains(i))
            log_error("Missing initializer for field '" + fields[i].name + "'");
    recording->literal_fields[literal] = std::move(indices);
    return structure;
}

std::shared_ptr<Type> Sema::check_field(const MemberAccessExpression *member) {
    auto base = check_expression(member->left.get());
    if (auto pointer = std::dynamic_pointer_cast<PointerType>(base)) {
        base = pointer->pointee;
        recording->indirect_members.insert(member);
    }
    auto structure = std::dynamic_pointer_cast<StructType>(base);
    const auto *name = dynamic_cast<const Identifier *>(member->member.get());
    if (!base)
        return nullptr;
    if (!structure || !name) {
        log_error("Field access requires an ordinary struct value");
        return nullptr;
    }
    const auto *field = structure->get_field(name->value);
    if (!field) {
        log_error("Unknown field '" + name->value + "' in " + structure->name);
        return nullptr;
    }
    if (!field->is_public && structure->owner != current_module) {
        log_error("Private field '" + name->value + "'");
        return nullptr;
    }
    recording->field_indices[member] = field - structure->fields.data();
    return field->type;
}

// Obtain numeric context without evaluating expressions or changing initialization state.
std::shared_ptr<Type> Sema::expression_type_hint(const Expression *expression) {
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression)) {
        if (auto *symbol = current_scope->resolve(identifier->value))
            return symbol->type;
    } else if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
        if (prefix->op == "*") {
            if (auto pointer = std::dynamic_pointer_cast<PointerType>(
                    expression_type_hint(prefix->right.get())))
                return pointer->pointee;
        } else if (prefix->op == "&") {
            if (auto type = expression_type_hint(prefix->right.get()))
                return std::make_shared<PointerType>(type, false, false);
        }
    } else if (const auto *binary = dynamic_cast<const InfixExpression *>(expression)) {
        if (binary->op == "+")
            return expression_type_hint(binary->left.get());
    } else if (const auto *literal = dynamic_cast<const StructLiteral *>(expression)) {
        if (literal->name)
            return resolve_type_from_string(literal->name->value);
    } else if (const auto *call = dynamic_cast<const CallExpression *>(expression)) {
        if (const auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get())) {
            if (const auto *method = method_target(member);
                method && arena_methods.contains(method) && call->arguments.size() == 1) {
                if (auto type = arena_value_type_hint(call->arguments.front().get()))
                    return std::make_shared<PointerType>(type, arena_methods.at(method), false);
                return nullptr;
            }
        }
        auto type = expression_type_hint(call->function.get());
        if (auto function = std::dynamic_pointer_cast<FunctionType>(type))
            return function->return_type;
    } else if (const auto *member = dynamic_cast<const MemberAccessExpression *>(expression)) {
        if (const auto *method = method_target(member)) {
            auto found = collected_functions.find(method);
            if (found != collected_functions.end())
                return found->second;
        }
        const auto *name = dynamic_cast<const Identifier *>(member->member.get());
        if (!name)
            return nullptr;
        auto base = expression_type_hint(member->left.get());
        if (auto pointer = std::dynamic_pointer_cast<PointerType>(base))
            base = pointer->pointee;
        if (auto structure = std::dynamic_pointer_cast<StructType>(base))
            if (auto *field = structure->get_field(name->value))
                return field->type;
        bool handled = false;
        if (auto *symbol = module_member(member, handled, false))
            return symbol->type;
    }
    return nullptr;
}
