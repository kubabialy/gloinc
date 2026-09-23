#include "sema.h"
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
        if (!definition->name || definition->is_packed || definition->backing_type ||
            !definition->generic_params.empty()) {
            log_error("Only ordinary non-generic structs are supported (SPEC-024)");
            continue;
        }
        const auto &name = definition->name->value;
        if (get_builtin_type(name) || current_scope->types.contains(name) ||
            current_scope->symbols.contains(name) || (!current_module && imports.contains(name)) ||
            (current_module && name == "__write_stdout")) {
            log_error("Duplicate or reserved struct name '" + name + "'");
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

void Sema::validate_struct_cycles() {
    std::vector<unsigned> state(recording->structures.size());
    std::function<bool(size_t)> visit = [&](size_t id) {
        if (state[id] == 1)
            return false;
        if (state[id] == 2)
            return true;
        state[id] = 1;
        for (const auto &field : recording->structures[id].fields)
            if (!field.type.is_pointer() && field.type.structure && !visit(*field.type.structure))
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
    } else if (const auto *literal = dynamic_cast<const StructLiteral *>(expression)) {
        if (literal->name)
            return resolve_type_from_string(literal->name->value);
    } else if (const auto *call = dynamic_cast<const CallExpression *>(expression)) {
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
        const auto *module = dynamic_cast<const Identifier *>(member->left.get());
        if (!current_module && module && imports.contains(module->value) &&
            !current_scope->resolve(module->value)) {
            if (auto *symbol = module_scopes.at(imports.at(module->value))->resolve(name->value))
                return symbol->type;
        }
    }
    return nullptr;
}
