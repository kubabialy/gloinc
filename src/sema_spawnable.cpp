#include "sema.h"
#include <algorithm>
#include <functional>

// Inline array and record layout cycles have already been rejected. Stop at
// pointer/slice boundaries rather than following recursive pointee types.
bool Sema::worker_value(const ValueType &type) const {
    if (type.owns_thread() || type.is_pointer() || type.is_slice() ||
        type.scalar == CoreType::String)
        return false;
    if (type.is_array())
        return worker_value(*type.array_element);
    if (type.is_result())
        return worker_value(*type.result_value);
    if (type.structure)
        return std::ranges::all_of(
            recording->structures.at(*type.structure).fields,
            [&](const CheckedField &field) { return worker_value(field.type); });
    return true;
}

void Sema::validate_spawnable_functions() {
    std::unordered_map<SymbolId, const FunctionDefinition *> functions;
    for (const auto &[function, signature] : collected_functions)
        functions.emplace(recording->bindings.at(function->name.get()), function);

    // Inspect resolved specializations and method calls. A spelling such as
    // "write" alone tells us nothing about the effects of a user's function.
    for (const auto &root : recording->symbols) {
        if (!recording->spawnable_functions.contains(root.id))
            continue;
        std::unordered_set<SymbolId> visited;
        std::function<void(SymbolId, std::string)> visit = [&](SymbolId id, std::string path) {
            if (!visited.insert(id).second)
                return;
            const auto *function = functions.at(id);
            const auto &symbol = recording->symbols.at(id);
            auto reject = [&](const Node *node, const std::string &why) {
                DiagnosticScope where(current_span, node->span);
                log_error("spawnable " + path + ": " + why);
            };
            if (!worker_value(symbol.type)) {
                reject(function->return_type.get(), "return type can carry borrowed storage");
                return;
            }
            for (size_t i = 0; i < symbol.parameters.size(); ++i) {
                const auto &type = symbol.parameters[i];
                // Helpers may borrow a caller-local value for the call. No
                // helper can return the reference or save it in a local owner.
                if (id != root.id && type.is_pointer() && !type.pointers.front().nullable &&
                    worker_value(type.pointee()))
                    continue;
                if (!worker_value(type)) {
                    reject(function->parameters[i].type.get(),
                           "parameter can carry borrowed storage");
                    return;
                }
            }
            for (auto local : worker_locals[function]) {
                const auto &binding = recording->symbols.at(local);
                if (binding.type.owns_thread()) {
                    DiagnosticScope where(current_span, binding.span);
                    log_error("spawnable " + path +
                              ": a worker cannot launch or collect another worker");
                    return;
                }
                if (!worker_value(binding.type)) {
                    DiagnosticScope where(current_span, binding.span);
                    log_error("spawnable " + path + ": stored borrow in local " + binding.name);
                    return;
                }
            }
            if (auto found = worker_pointer_offsets.find(function);
                found != worker_pointer_offsets.end()) {
                reject(found->second, "raw pointer offsets are not checked for worker lifetimes");
                return;
            }
            if (auto found = recording->defers.find(function); found != recording->defers.end()) {
                reject(found->second.front(),
                       "deferred captures are not yet checked for worker lifetimes");
                return;
            }
            for (const auto *call : worker_calls[function]) {
                if (recording->integer_conversions.contains(call) ||
                    recording->error_constructors.contains(call))
                    continue;
                const auto *name = dynamic_cast<const Identifier *>(call->function.get());
                if (auto *member =
                        dynamic_cast<const MemberAccessExpression *>(call->function.get()))
                    name = dynamic_cast<const Identifier *>(member->member.get());
                auto binding = recording->bindings.find(name);
                if (binding == recording->bindings.end() || !functions.contains(binding->second)) {
                    reject(call, "unaudited worker operation '" + (name ? name->value : "?") + "'");
                    return;
                }
                visit(binding->second, path + " -> " + name->value);
            }
        };
        visit(root.id, root.name);
    }
}

std::shared_ptr<Type> Sema::worker_binding_type(const VariableDeclaration *decl) {
    std::string_view name = decl->type->value;
    constexpr std::string_view resultPrefix = "result<";
    constexpr std::string_view handlePrefix = ".Thread<";
    bool launch = name.starts_with(resultPrefix) && name.ends_with('>');
    if (launch) {
        name.remove_prefix(resultPrefix.size());
        name.remove_suffix(1);
    }
    std::optional<size_t> length;
    bool slots = !launch && name.starts_with('[') && name.ends_with(']');
    if (slots) {
        auto separator = name.rfind("; ");
        if (separator == std::string_view::npos)
            return nullptr;
        length = fixed_array_length(name.substr(separator + 2, name.size() - separator - 3));
        if (!length)
            return nullptr;
        name = name.substr(1, separator - 1);
    }
    auto dot = name.find('.');
    if (dot == std::string_view::npos || !name.substr(dot).starts_with(handlePrefix) ||
        !name.ends_with('>'))
        return nullptr;
    auto module = imports.find(std::string(name.substr(0, dot)));
    if (module == imports.end() || module->second->standard_name != "threads")
        return nullptr;
    name.remove_prefix(dot + handlePrefix.size());
    name.remove_suffix(1);
    auto payload = resolve_type_from_string(std::string(name));
    auto core = value_type(payload);
    if (!core || core->is_result() || core->is_error() || core->const_size)
        return nullptr;
    std::shared_ptr<Type> type = std::make_shared<ThreadHandleType>(payload);
    if (slots) {
        type = std::make_shared<ArrayType>(type, *length);
        recording->types[decl->type.get()] = *value_type(type);
        if (!current_function || !decl->is_mutable ||
            !dynamic_cast<const ZeroedLiteral *>(decl->initializer.get()))
            log_error("A thread handle array requires a mutable local initialized with zeroed");
        else
            worker_owner_uses.insert(decl->initializer.get());
        return type;
    }
    if (launch)
        type = std::make_shared<ResultType>(type);
    recording->types[decl->type.get()] = *value_type(type);
    const auto *init = decl->initializer.get();
    auto *member = dynamic_cast<const MemberAccessExpression *>(init);
    auto *field = member ? dynamic_cast<const Identifier *>(member->member.get()) : nullptr;
    bool transfer =
        field && field->value == "value" && dynamic_cast<const Identifier *>(member->left.get());
    if (!current_function || decl->is_mutable || decl->is_const ||
        (launch ? !dynamic_cast<const SpawnExpression *>(init) : !transfer))
        log_error(launch ? "A launch owner requires an immutable local initialized by spawn"
                         : "A thread handle requires an immutable local initialized from a launch "
                           "result's .value");
    else
        worker_owner_uses.insert(init);
    return type;
}

std::shared_ptr<Type> Sema::check_worker_launch(const SpawnExpression *spawn) {
    const auto *call = dynamic_cast<const CallExpression *>(spawn->call.get());
    if (!call || spawn->op != GLOIN_TOKEN_SPAWN) {
        log_error("spawn requires a direct spawnable function call");
        return nullptr;
    }
    auto returned = check_expression(call, std::nullopt, true);
    if (!returned)
        return nullptr;
    auto *name = dynamic_cast<const Identifier *>(call->function.get());
    if (auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get()))
        name = dynamic_cast<const Identifier *>(member->member.get());
    auto target = recording->bindings.find(name);
    if (target == recording->bindings.end() ||
        !recording->spawnable_functions.contains(target->second)) {
        log_error("spawn requires a direct spawnable function call");
        return nullptr;
    }
    recording->worker_launches.emplace(spawn, target->second);
    auto result = std::dynamic_pointer_cast<ResultType>(returned);
    return std::make_shared<ResultType>(
        std::make_shared<ThreadHandleType>(result ? result->value : returned));
}

std::shared_ptr<ThreadHandleType> Sema::worker_slot(const IndexExpression *index) {
    auto *name = index ? dynamic_cast<const Identifier *>(index->left.get()) : nullptr;
    auto *binding = name ? current_scope->resolve(name->value) : nullptr;
    auto *array = binding ? dynamic_cast<ArrayType *>(binding->type.get()) : nullptr;
    auto handle = array ? std::dynamic_pointer_cast<ThreadHandleType>(array->element) : nullptr;
    if (handle) {
        worker_owner_uses.insert(name);
        worker_owner_uses.insert(index);
    }
    return handle;
}
