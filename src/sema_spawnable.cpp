#include "sema.h"
#include <algorithm>
#include <functional>

// Inline array and record layout cycles have already been rejected. Stop at
// pointer/slice boundaries rather than following recursive pointee types.
bool Sema::worker_value(const ValueType &type) const {
    if (type.is_pointer() || type.is_slice() || type.scalar == CoreType::String)
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
