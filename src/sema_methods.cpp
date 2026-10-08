#include "sema.h"
#include "ast_clone.h"
#include <set>

void Sema::collect_methods(const std::vector<std::unique_ptr<Statement>> &program) {
    for (const auto &statement : program) {
        const auto *definition = dynamic_cast<const StructDefinition *>(statement.get());
        if (!definition || !recording->types.contains(definition))
            continue;
        auto structure = std::dynamic_pointer_cast<StructType>(
            current_scope->resolve_type(definition->name->value));
        for (const auto &method : definition->methods)
            collect_method(structure, method.get());
    }
}

void Sema::collect_method(const std::shared_ptr<StructType> &structure,
                          const FunctionDefinition *method) {
    DiagnosticScope location(current_span, method->span);
    if (!method->name)
        return;
    const auto &name = method->name->value;
    if (structure->get_field(name) || !structure->methods.emplace(name, method).second) {
        log_error("Duplicate field or method '" + name + "'");
        return;
    }
    if (!method->generic_params.empty()) {
        std::set<std::string> parameters;
        for (const auto &parameter : method->generic_params)
            if (is_const_size_parameter(parameter))
                log_error("Size parameters are currently supported only on generic structs");
            else if (!parameters.insert(parameter).second || get_builtin_type(parameter) ||
                     current_scope->resolve_type(parameter) ||
                     current_scope->resolve_generic_struct(parameter))
                log_error("Duplicate or reserved generic method parameter '" + parameter + "'");
        return;
    }
    // Method symbols have their own namespace, not the file's function namespace.
    enter_scope();
    auto signature = collect_function(method);
    leave_scope();
    if (!signature)
        return;
    if (!validate_method_signature(structure, method, signature))
        return;
    collected_functions.emplace(method, signature);
    register_arena_method(structure, method, signature);
    recording->linkage_names[recording->bindings.at(method->name.get())] =
        "gloin.method." + std::to_string(*structure->identity) + "." + name;
}

bool Sema::validate_method_signature(const std::shared_ptr<StructType> &structure,
                                     const FunctionDefinition *method,
                                     const std::shared_ptr<FunctionType> &signature) {
    bool valid = true;
    for (size_t i = 0; i < method->parameters.size(); ++i) {
        if (method->parameters[i].name->value == "self" && (method->is_static || i != 0)) {
            log_error("self is allowed only as the first parameter of an instance method");
            valid = false;
        }
    }
    if (!method->is_static) {
        auto receiver = signature->param_types.empty()
                            ? nullptr
                            : std::dynamic_pointer_cast<PointerType>(signature->param_types[0]);
        if (method->parameters.empty() || method->parameters[0].name->value != "self" ||
            !receiver || !receiver->pointee->equals(*structure)) {
            log_error("Instance method requires first parameter self: *" + structure->name +
                      " or &" + structure->name + " (optionally const)");
            valid = false;
        }
    }
    return valid;
}

void Sema::bind_enclosing_type_params(const std::shared_ptr<StructType> &structure) {
    for (const auto &specialization : generic_specializations) {
        if (specialization.type->identity != structure->identity)
            continue;
        for (size_t i = 0; i < specialization.resolved_arguments.size(); ++i)
            current_scope->define_type(generic_parameter_name(specialization.definition->generic_params[i]),
                                       specialization.resolved_arguments[i]);
        return;
    }
}

std::shared_ptr<FunctionType>
Sema::specialize_method(const std::shared_ptr<StructType> &structure,
                        const FunctionDefinition *definition,
                        const std::vector<std::shared_ptr<Type>> &arguments, SymbolId &symbol) {
    std::vector<ValueType> keys;
    for (const auto &argument : arguments) {
        auto value = value_type(argument);
        if (!value)
            return nullptr;
        keys.push_back(*value);
    }
    for (const auto &specialization : generic_method_specializations)
        if (specialization.definition == definition &&
            specialization.receiver->identity == structure->identity &&
            specialization.arguments == keys) {
            symbol = recording->bindings.at(specialization.instance->name.get());
            return specialization.signature;
        }
    if (generic_method_specializations.size() >= 256) {
        log_error("Generic method specialization limit of 256 exceeded");
        return nullptr;
    }
    auto instance = clone_function(*definition);
    instance->generic_params.clear();
    const auto *concrete = instance.get();
    recording->specialized_methods.push_back(std::move(instance));

    auto saved_scope = current_scope;
    auto saved_module = current_module;
    auto saved_imports = imports;
    current_module = structure->owner;
    current_scope = std::make_shared<Scope>(module_scopes.at(current_module));
    imports = module_imports.at(current_module);
    bind_enclosing_type_params(structure);
    for (size_t i = 0; i < arguments.size(); ++i)
        current_scope->define_type(definition->generic_params[i], arguments[i]);
    auto signature = collect_function(concrete);
    bool valid = signature && validate_method_signature(structure, concrete, signature);
    current_scope = saved_scope;
    current_module = saved_module;
    imports = std::move(saved_imports);
    if (!valid)
        return nullptr;
    collected_functions.emplace(concrete, signature);
    symbol = recording->bindings.at(concrete->name.get());
    recording->linkage_names[symbol] = "gloin.method." + std::to_string(*structure->identity) +
                                       "." + definition->name->value + ".generic." +
                                       std::to_string(symbol);
    generic_method_specializations.push_back(
        {definition, structure, std::move(keys), arguments, concrete, signature});
    return signature;
}

void Sema::check_methods(const StructDefinition *definition) {
    for (const auto &method : definition->methods)
        if (collected_functions.contains(method.get()))
            check_statement(method.get());
}

std::shared_ptr<StructType> Sema::method_type_receiver(const Expression *expression) {
    if (const auto *name = dynamic_cast<const Identifier *>(expression)) {
        if (!current_scope->resolve(name->value))
            return std::dynamic_pointer_cast<StructType>(resolve_type_from_string(name->value));
    }
    if (const auto *qualified = dynamic_cast<const MemberAccessExpression *>(expression)) {
        const auto *module = dynamic_cast<const Identifier *>(qualified->left.get());
        const auto *name = dynamic_cast<const Identifier *>(qualified->member.get());
        if (module && name && imports.contains(module->value) &&
            !current_scope->resolve(module->value))
            return std::dynamic_pointer_cast<StructType>(
                resolve_type_from_string(module->value + "." + name->value));
    }
    return nullptr;
}

const FunctionDefinition *Sema::method_target(const MemberAccessExpression *member) {
    const auto *name = dynamic_cast<const Identifier *>(member->member.get());
    if (!name)
        return nullptr;
    auto structure = method_type_receiver(member->left.get());
    if (!structure) {
        auto type = expression_type_hint(member->left.get());
        if (auto pointer = std::dynamic_pointer_cast<PointerType>(type))
            type = pointer->pointee;
        structure = std::dynamic_pointer_cast<StructType>(type);
    }
    const auto open = name->value.find('<');
    const auto method_name = open == std::string::npos ? name->value : name->value.substr(0, open);
    if (structure && structure->methods.contains(method_name))
        return structure->methods.at(method_name);
    return nullptr;
}

std::shared_ptr<Type> Sema::check_method_call(const CallExpression *call, bool &handled) {
    const auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get());
    if (!member)
        return nullptr;
    const auto *name = dynamic_cast<const Identifier *>(member->member.get());
    if (!name)
        return nullptr;
    const auto open = name->value.find('<');
    const bool explicit_types = open != std::string::npos && name->value.ends_with('>');
    auto structure = method_type_receiver(member->left.get());
    const bool type_receiver = bool(structure);
    std::shared_ptr<PointerType> receiver_pointer;
    if (!type_receiver) {
        if (const auto *module = dynamic_cast<const Identifier *>(member->left.get());
            module && imports.contains(module->value) &&
            !current_scope->resolve(module->value))
            return nullptr; // A module function call is handled separately.
        auto receiver_type = check_expression(member->left.get());
        if (!receiver_type) {
            handled = true;
            return nullptr;
        }
        receiver_pointer = std::dynamic_pointer_cast<PointerType>(receiver_type);
        structure = std::dynamic_pointer_cast<StructType>(
            receiver_pointer ? receiver_pointer->pointee : receiver_type);
    }
    const auto method_name = open == std::string::npos ? name->value : name->value.substr(0, open);
    const auto *method = structure && structure->methods.contains(method_name)
                             ? structure->methods.at(method_name)
                             : nullptr;
    handled = method || type_receiver || explicit_types;
    if (!handled)
        return nullptr;
    if (!method) {
        log_error(structure ? "Unknown method on type '" + structure->name + "'"
                            : "Unknown generic method '" + name->value + "'");
        return nullptr;
    }
    if (type_receiver != method->is_static) {
        log_error(method->is_static ? "Static method requires Type.method(...)"
                                    : "Instance method requires an object receiver");
        return nullptr;
    }
    if (!method->is_public && structure->owner != current_module) {
        log_error("Private method '" + method->name->value + "'");
        return nullptr;
    }
    std::shared_ptr<FunctionType> signature;
    SymbolId symbol = invalid_symbol;
    if (!method->generic_params.empty()) {
        if (!explicit_types) {
            log_error("Generic method '" + method->name->value +
                      "' requires explicit type arguments");
            return nullptr;
        }
        auto names = split_type_arguments(std::string_view(name->value).substr(
            open + 1, name->value.size() - open - 2));
        if (names.size() != method->generic_params.size()) {
            log_error("Generic method '" + method->name->value + "' expects " +
                      std::to_string(method->generic_params.size()) + " type arguments");
            return nullptr;
        }
        std::vector<std::shared_ptr<Type>> arguments;
        for (const auto &argument : names) {
            auto type = resolve_type_from_string(argument);
            if (!type || !value_type(type) || dynamic_cast<VoidType *>(type.get())) {
                log_error("Unknown or invalid generic type argument '" + argument + "'");
                return nullptr;
            }
            arguments.push_back(std::move(type));
        }
        signature = specialize_method(structure, method, arguments, symbol);
        if (!signature)
            return nullptr;
    } else {
        if (explicit_types) {
            log_error("Non-generic method '" + method->name->value +
                      "' cannot take type arguments");
            return nullptr;
        }
        auto found = collected_functions.find(method);
        if (found == collected_functions.end())
            return nullptr; // Invalid declaration already diagnosed.
        signature = found->second;
        symbol = recording->bindings.at(method->name.get());
    }
    size_t offset = method->is_static ? 0 : 1;
    if (!type_receiver) {
        const bool take_address = !receiver_pointer;
        if (!receiver_pointer) {
            auto place = check_place(member->left.get(), true);
            if (!place.type || !place.addressable)
                return nullptr;
            receiver_pointer =
                std::make_shared<PointerType>(place.type, false, !place.writable);
        }
        auto expected = std::static_pointer_cast<PointerType>(signature->param_types[0]);
        if (!pointer_conversion(*receiver_pointer, *expected)) {
            log_error("Method receiver requires " + expected->to_string() + ", got " +
                      receiver_pointer->to_string());
            return nullptr;
        }
        recording->method_receivers[call] = take_address;
    }
    if (auto bridge = arena_methods.find(method); bridge != arena_methods.end()) {
        if (call->arguments.size() != 1) {
            log_error("Arena allocation expects exactly one initialized value");
            return nullptr;
        }
        // Infer T from the value itself, not from an expected pointer result.
        auto previous = expected_pointer;
        expected_pointer.reset();
        auto value = check_expression(call->arguments.front().get());
        expected_pointer = previous;
        if (!value || !value_type(value) || value_type(value) == ValueType(CoreType::Void))
            return nullptr;
        recording->arena_allocations[call] = bridge->second;
        recording->bindings[name] = symbol;
        return std::make_shared<PointerType>(value, bridge->second, false);
    }
    if (arena_many_methods.contains(method)) {
        if (call->arguments.size() != 2) {
            log_error("Arena alloc_many expects a fill value and i64 capacity");
            return nullptr;
        }
        auto previous = expected_pointer;
        expected_pointer.reset();
        auto value = check_expression(call->arguments[0].get());
        expected_pointer = previous;
        if (!value || !value_type(value) || value_type(value) == ValueType(CoreType::Void))
            return nullptr;
        auto capacity = check_typed_expression(call->arguments[1].get(), get_builtin_type("i64"));
        if (!capacity || !capacity->equals(*get_builtin_type("i64"))) {
            log_error("Arena alloc_many capacity must be i64");
            return nullptr;
        }
        recording->arena_many_allocations.insert(call);
        recording->bindings[name] = symbol;
        return std::make_shared<PointerType>(value, false, false);
    }
    if (arena_reserved_methods.contains(method)) {
        if (!current_module || current_module->standard_name != "vector") {
            log_error("Arena reserved allocation is internal to @vector");
            return nullptr;
        }
        if (call->arguments.size() != 2) {
            log_error("Arena reserved allocation expects a type witness and i64 capacity");
            return nullptr;
        }
        auto value = check_expression(call->arguments[0].get());
        auto capacity = check_typed_expression(call->arguments[1].get(), get_builtin_type("i64"));
        if (!value || !value_type(value) || !capacity ||
            !capacity->equals(*get_builtin_type("i64")))
            return nullptr;
        recording->arena_reserved_allocations.insert(call);
        recording->bindings[name] = symbol;
        return std::make_shared<PointerType>(value, false, false);
    }
    if (structure->owner && structure->owner->standard_name == "arena" &&
        structure->name == "GeneralArena" && method_name == "reserve_typed") {
        if (!current_module || current_module->standard_name != "vector") {
            log_error("Typed arena reservation is internal to @vector");
            return nullptr;
        }
        auto bytes = std::make_shared<PointerType>(get_builtin_type("u8"), true, false);
        if (!explicit_types || method->generic_params.size() != 1 ||
            call->arguments.size() != 1 || signature->param_types.size() != 3 ||
            !signature->param_types[1]->equals(*get_builtin_type("u64")) ||
            !signature->param_types[2]->equals(*get_builtin_type("u64")) ||
            !signature->return_type->equals(*bytes)) {
            log_error("Typed arena reservation expects one type and i64 capacity");
            return nullptr;
        }
        auto names = split_type_arguments(std::string_view(name->value).substr(
            open + 1, name->value.size() - open - 2));
        auto element = resolve_type_from_string(names.front());
        auto capacity = check_typed_expression(call->arguments[0].get(), get_builtin_type("i64"));
        if (!element || !value_type(element) || !capacity ||
            !capacity->equals(*get_builtin_type("i64")))
            return nullptr;
        recording->arena_typed_reservations.insert(call);
        recording->bindings[name] = symbol;
        return std::make_shared<PointerType>(element, false, false);
    }
    if (call->arguments.size() != signature->param_types.size() - offset) {
        log_error("Incorrect number of method arguments");
        return nullptr;
    }
    for (size_t i = 0; i < call->arguments.size(); ++i) {
        auto expected = signature->param_types[i + offset];
        auto actual = check_typed_expression(call->arguments[i].get(), expected);
        if (!actual)
            return nullptr;
        if (!actual->equals(*expected)) {
            log_error("Method argument type mismatch: expected " + expected->to_string());
            return nullptr;
        }
    }
    recording->bindings[name] = symbol;
    return signature->return_type;
}
