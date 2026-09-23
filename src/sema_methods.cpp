#include "sema.h"

void Sema::collect_methods(const std::vector<std::unique_ptr<Statement>> &program) {
    for (const auto &statement : program) {
        const auto *definition = dynamic_cast<const StructDefinition *>(statement.get());
        if (!definition || !recording->types.contains(definition))
            continue;
        auto structure = std::dynamic_pointer_cast<StructType>(
            current_scope->resolve_type(definition->name->value));
        for (const auto &method : definition->methods) {
            DiagnosticScope location(current_span, method->span);
            if (!method->name)
                continue;
            const auto &name = method->name->value;
            if (structure->get_field(name) ||
                !structure->methods.emplace(name, method.get()).second) {
                log_error("Duplicate field or method '" + name + "'");
                continue;
            }
            // Method symbols have their own namespace, not the file's function namespace.
            enter_scope();
            auto signature = collect_function(method.get());
            leave_scope();
            if (!signature)
                continue;
            bool valid = true;
            for (size_t i = 0; i < method->parameters.size(); ++i) {
                if (method->parameters[i].name->value == "self" && (method->is_static || i != 0)) {
                    log_error("self is allowed only as the first parameter of an instance method");
                    valid = false;
                }
            }
            if (!method->is_static) {
                auto receiver =
                    signature->param_types.empty()
                        ? nullptr
                        : std::dynamic_pointer_cast<PointerType>(signature->param_types[0]);
                if (method->parameters.empty() || method->parameters[0].name->value != "self" ||
                    !receiver || !receiver->pointee->equals(*structure)) {
                    log_error("Instance method requires first parameter self: *" + structure->name +
                              " or &" + structure->name + " (optionally const)");
                    valid = false;
                }
            }
            if (!valid)
                continue;
            collected_functions.emplace(method.get(), signature);
            register_arena_method(structure, method.get(), signature);
            recording->linkage_names[recording->bindings.at(method->name.get())] =
                "gloin.method." + std::to_string(*structure->identity) + "." + name;
        }
    }
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
        if (module && name && !current_module && imports.contains(module->value) &&
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
    if (structure && structure->methods.contains(name->value))
        return structure->methods.at(name->value);
    return nullptr;
}

std::shared_ptr<Type> Sema::check_method_call(const CallExpression *call, bool &handled) {
    const auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get());
    if (!member)
        return nullptr;
    const auto *method = method_target(member);
    auto structure = method_type_receiver(member->left.get());
    const bool type_receiver = bool(structure);
    handled = method || type_receiver;
    if (!handled)
        return nullptr;
    if (!method) {
        log_error("Unknown method on type '" + structure->name + "'");
        return nullptr;
    }
    if (type_receiver != method->is_static) {
        log_error(method->is_static ? "Static method requires Type.method(...)"
                                    : "Instance method requires an object receiver");
        return nullptr;
    }
    auto found = collected_functions.find(method);
    if (found == collected_functions.end())
        return nullptr; // Invalid declaration already diagnosed.
    auto signature = found->second;
    size_t offset = method->is_static ? 0 : 1;
    if (!type_receiver) {
        auto receiver_type = check_expression(member->left.get());
        if (!receiver_type)
            return nullptr;
        auto pointer = std::dynamic_pointer_cast<PointerType>(receiver_type);
        const bool take_address = !pointer;
        if (!pointer) {
            auto place = check_place(member->left.get(), true);
            if (!place.type || !place.addressable)
                return nullptr;
            pointer = std::make_shared<PointerType>(place.type, false, !place.writable);
        }
        structure = std::dynamic_pointer_cast<StructType>(pointer->pointee);
        auto expected = std::static_pointer_cast<PointerType>(signature->param_types[0]);
        if (!pointer_conversion(*pointer, *expected)) {
            log_error("Method receiver requires " + expected->to_string() + ", got " +
                      pointer->to_string());
            return nullptr;
        }
        recording->method_receivers[call] = take_address;
    }
    if (!method->is_public && structure->owner != current_module) {
        log_error("Private method '" + method->name->value + "'");
        return nullptr;
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
        recording->bindings[static_cast<const Identifier *>(member->member.get())] =
            recording->bindings.at(method->name.get());
        return std::make_shared<PointerType>(value, bridge->second, false);
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
    recording->bindings[static_cast<const Identifier *>(member->member.get())] =
        recording->bindings.at(method->name.get());
    return signature->return_type;
}
