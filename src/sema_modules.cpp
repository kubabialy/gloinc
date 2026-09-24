#include "sema.h"
#include <functional>
#include <unordered_set>

bool Sema::prepare_modules(const std::vector<std::unique_ptr<Statement>> &program) {
    std::unordered_set<const SourceModule *> visited;
    std::function<void(const std::vector<std::unique_ptr<Statement>> &, const SourceModule *)>
        visit;
    visit = [&](const auto &declarations, const SourceModule *owner) {
        module_imports.try_emplace(owner);
        for (const auto &statement : declarations) {
            const auto *import = dynamic_cast<const ImportStatement *>(statement.get());
            if (!import)
                continue;
            DiagnosticScope location(current_span, import->span);
            if (!import->module) {
                log_error("Module must be loaded before semantic checking: " + import->path);
                continue;
            }
            const auto *module = import->module.get();
            if (!module_imports[owner].emplace(import->module_name, module).second)
                log_error("Duplicate import '" + import->path + "'");
            if (visited.insert(module).second) {
                module_scopes[module] = std::make_shared<Scope>();
                visit(module->declarations, module);
                recording->modules.push_back(module);
            }
        }
    };
    visit(program, nullptr);
    return !has_error();
}

void Sema::select_module(const SourceModule *module) {
    current_module = module;
    current_scope = module_scopes.at(module);
    imports = module_imports.at(module);
    falls_through = true;
}

void Sema::collect_declarations(const std::vector<std::unique_ptr<Statement>> &program) {
    collect_structs(program);
    for (const auto &declaration : program) {
        if (const auto *function = dynamic_cast<const FunctionDefinition *>(declaration.get())) {
            if (auto type = collect_function(function)) {
                collected_functions.emplace(function, std::move(type));
                if (current_module)
                    recording->linkage_names[recording->bindings.at(function->name.get())] =
                        current_module->linkage_prefix + "." + function->name->value;
            }
        }
    }
    for (const auto &declaration : program)
        if (const auto *constant = dynamic_cast<const VariableDeclaration *>(declaration.get());
            constant && constant->is_const)
            check_constant(constant);
    collect_methods(program);
}

void Sema::check_bodies(const std::vector<std::unique_ptr<Statement>> &program) {
    for (const auto &statement : program) {
        if (dynamic_cast<const ImportStatement *>(statement.get()))
            continue;
        if (const auto *constant = dynamic_cast<const VariableDeclaration *>(statement.get());
            constant && constant->is_const)
            continue;
        if (const auto *structure = dynamic_cast<const StructDefinition *>(statement.get())) {
            check_methods(structure);
        } else if (dynamic_cast<const FunctionDefinition *>(statement.get())) {
            check_statement(statement.get());
        } else {
            DiagnosticScope location(current_span, statement->span);
            log_error("Only functions, structs, constants, and imports are allowed at file scope");
        }
    }
}

Symbol *Sema::module_member(const MemberAccessExpression *member, bool &handled, bool diagnose) {
    const auto *base = dynamic_cast<const Identifier *>(member->left.get());
    const auto *name = dynamic_cast<const Identifier *>(member->member.get());
    handled = base && name && imports.contains(base->value) && !current_scope->resolve(base->value);
    if (!handled)
        return nullptr;
    const auto *module = imports.at(base->value);
    bool exported = false;
    for (const auto &declaration : module->declarations) {
        if (const auto *function = dynamic_cast<const FunctionDefinition *>(declaration.get());
            function && function->name->value == name->value)
            exported = function->is_public;
        if (const auto *constant = dynamic_cast<const VariableDeclaration *>(declaration.get());
            constant && constant->is_const && constant->name->value == name->value)
            exported = constant->is_public;
    }
    auto *symbol = exported ? module_scopes.at(module)->resolve(name->value) : nullptr;
    if (!symbol) {
        if (diagnose)
            log_error("Unknown or private member '" + name->value + "' in " +
                      module->canonical_path);
        return nullptr;
    }
    if (diagnose) {
        recording->bindings[name] = symbol->id;
        if (symbol->kind == SymbolKind::Constant)
            recording->module_constants[member] = symbol->id;
    }
    return symbol;
}
