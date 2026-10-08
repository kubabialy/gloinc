#include "sema.h"
#include "ast_clone.h"
#include "operators.h"
#include <charconv>
#include <iostream>

namespace {
bool zeroable_array_element(const std::shared_ptr<Type> &type) {
    if (const auto *array = dynamic_cast<const ArrayType *>(type.get()))
        return array->length == 0 || zeroable_array_element(array->element);
    if (const auto *pointer = dynamic_cast<const PointerType *>(type.get()))
        return pointer->nullable;
    if (const auto core = resolve_core_type(type->to_string()))
        return *core != CoreType::Void;
    return false;
}

std::string_view trim_type(std::string_view text) {
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    return text;
}
} // namespace

std::vector<std::string> Sema::split_type_arguments(std::string_view text) {
    std::vector<std::string> arguments;
    size_t start = 0;
    int angle = 0;
    int square = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        switch (text[i]) {
        case '<': ++angle; break;
        case '>': --angle; break;
        case '[': ++square; break;
        case ']': --square; break;
        case ',':
        case ';':
            if (angle == 0 && square == 0) {
                arguments.emplace_back(trim_type(text.substr(start, i - start)));
                start = i + 1;
            }
            break;
        default: break;
        }
        if (angle < 0 || square < 0)
            return {};
    }
    if (angle != 0 || square != 0)
        return {};
    auto last = trim_type(text.substr(start));
    if (!last.empty())
        arguments.emplace_back(last);
    return arguments;
}

void Scope::define(const std::string &name, Symbol symbol) { symbols[name] = symbol; }

Symbol *Scope::resolve(const std::string &name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return &it->second;
    }
    if (parent) {
        return parent->resolve(name);
    }
    return nullptr;
}

void Scope::define_type(const std::string &name, std::shared_ptr<Type> type) { types[name] = type; }

std::shared_ptr<Type> Scope::resolve_type(const std::string &name) {
    auto it = types.find(name);
    if (it != types.end()) {
        return it->second;
    }
    if (parent) {
        return parent->resolve_type(name);
    }
    return nullptr;
}

const StructDefinition *Scope::resolve_generic_struct(const std::string &name, size_t arity) {
    if (auto it = generic_structs.find(name); it != generic_structs.end()) {
        if (arity == 0)
            return it->second.front();
        for (auto *definition : it->second)
            if (definition->generic_params.size() == arity)
                return definition;
        // Let the caller report the expected arity, even for overloaded names.
        return it->second.front();
    }
    return parent ? parent->resolve_generic_struct(name, arity) : nullptr;
}

const FunctionDefinition *Scope::resolve_generic_function(const std::string &name) {
    if (auto it = generic_functions.find(name); it != generic_functions.end())
        return it->second;
    return parent ? parent->resolve_generic_function(name) : nullptr;
}

Sema::Sema(std::shared_ptr<Diagnostics> diagnostics) : diagnostics_(std::move(diagnostics)) {
    current_scope = std::make_shared<Scope>();

    for (const auto &type : core_types) {
        if (type.id == CoreType::Void)
            builtin_types[std::string(type.name)] = std::make_shared<VoidType>();
        else
            builtin_types[std::string(type.name)] =
                std::make_shared<PrimitiveType>(std::string(type.name));
    }
    builtin_types["int"] = builtin_types["i32"];
    builtin_types["usize"] = builtin_types["u64"];
    // Available only to the legacy stage checker, never to check_for_codegen.
    builtin_types["string"] = std::make_shared<PrimitiveType>("string");
}

std::shared_ptr<Type> Sema::get_builtin_type(const std::string &name) {
    auto it = builtin_types.find(name);
    if (it != builtin_types.end()) {
        return it->second;
    }
    return nullptr;
}

void Sema::log_error(const std::string &msg) {
    errors.push_back(msg);
    has_errors = true;
    diagnostics_->error(DiagnosticStage::Semantic, current_span, msg);
}

std::shared_ptr<Type> Sema::resolve_type_from_string(const std::string &name) {
    if (recording) {
        if (name == "error")
            return std::make_shared<ErrorType>();
        if (name == "result") {
            log_error("result requires exactly one type argument");
            return nullptr;
        }
        if (name.starts_with("*") || name.starts_with("&")) {
            auto inner = name.substr(1);
            bool read_only = inner.starts_with("const ");
            if (read_only)
                inner = inner.substr(6);
            auto pointee = resolve_type_from_string(inner);
            if (!pointee || dynamic_cast<VoidType *>(pointee.get()) ||
                dynamic_cast<ConstSizeType *>(pointee.get()) ||
                dynamic_cast<ResultType *>(pointee.get()))
                return nullptr;
            return std::make_shared<PointerType>(pointee, name[0] == '*', read_only);
        }
        if (name.starts_with("[") && name.ends_with("]")) {
            const auto separator = name.rfind("; ");
            if (separator == std::string::npos) {
                auto inner = name.substr(1, name.size() - 2);
                const bool read_only = inner.starts_with("const ");
                if (read_only)
                    inner = inner.substr(6);
                auto element = resolve_type_from_string(inner);
                if (!element || dynamic_cast<VoidType *>(element.get()) ||
                    dynamic_cast<ConstSizeType *>(element.get()) ||
                    dynamic_cast<FunctionType *>(element.get()) ||
                    dynamic_cast<ResultType *>(element.get()))
                    return nullptr;
                return std::make_shared<SliceType>(element, read_only);
            }
            if (separator < 2)
                return nullptr;
            auto element = resolve_type_from_string(name.substr(1, separator - 1));
            if (!element || dynamic_cast<VoidType *>(element.get()) ||
                dynamic_cast<ConstSizeType *>(element.get()) ||
                dynamic_cast<FunctionType *>(element.get()) ||
                dynamic_cast<ResultType *>(element.get()))
                return nullptr;
            const auto digits = std::string_view(name).substr(separator + 2,
                                                       name.size() - separator - 3);
            size_t length = 0;
            auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                                length);
            if (error != std::errc{} || end != digits.data() + digits.size()) {
                auto bound = current_scope->resolve_type(std::string(digits));
                auto *size = dynamic_cast<ConstSizeType *>(bound.get());
                if (!size)
                    return nullptr;
                length = size->value;
            }
            if (length > 1048576)
                return nullptr;
            return std::make_shared<ArrayType>(element, length);
        }
        const auto open = name.find('<');
        if (open != std::string::npos && name.ends_with('>')) {
            const auto base = name.substr(0, open);
            const auto type_arguments =
                split_type_arguments(std::string_view(name).substr(open + 1,
                                                                   name.size() - open - 2));
            if (base == "result") {
                if (type_arguments.size() != 1) {
                    log_error("result requires exactly one type argument");
                    return nullptr;
                }
                auto value = resolve_type_from_string(type_arguments.front());
                if (!value || !value_type(value) ||
                    dynamic_cast<ErrorType *>(value.get()) ||
                    dynamic_cast<ConstSizeType *>(value.get()) ||
                    dynamic_cast<ResultType *>(value.get())) {
                    log_error("result value must be a supported type other than error");
                    return nullptr;
                }
                return std::make_shared<ResultType>(value);
            }
            const StructDefinition *definition = nullptr;
            if (const auto dot = base.find('.'); dot != std::string::npos) {
                auto imported = imports.find(base.substr(0, dot));
                if (imported != imports.end()) {
                    definition = module_scopes.at(imported->second)->resolve_generic_struct(
                        base.substr(dot + 1), type_arguments.size());
                    if (definition && !definition->is_public)
                        definition = nullptr;
                }
            } else {
                definition = current_scope->resolve_generic_struct(base, type_arguments.size());
            }
            if (!definition) {
                log_error("Unknown or inaccessible generic struct '" + base + "'");
                return nullptr;
            }
            if (type_arguments.size() != definition->generic_params.size()) {
                log_error("Generic struct '" + base + "' expects " +
                          std::to_string(definition->generic_params.size()) + " type arguments");
                return nullptr;
            }
            std::vector<std::shared_ptr<Type>> arguments;
            for (size_t index = 0; index < type_arguments.size(); ++index) {
                const auto &argument = type_arguments[index];
                std::shared_ptr<Type> type;
                if (is_const_size_parameter(definition->generic_params[index])) {
                    size_t size = 0;
                    auto [end, error] = std::from_chars(argument.data(),
                                                        argument.data() + argument.size(), size);
                    if (error == std::errc{} && end == argument.data() + argument.size())
                        type = std::make_shared<ConstSizeType>(size);
                    else
                        type = current_scope->resolve_type(argument);
                    auto *constant = dynamic_cast<ConstSizeType *>(type.get());
                    if (!constant || constant->value > 1048576)
                        type = nullptr;
                } else {
                    type = resolve_type_from_string(argument);
                    if (dynamic_cast<ConstSizeType *>(type.get()))
                        type = nullptr;
                }
                if (!type || !value_type(type) || dynamic_cast<VoidType *>(type.get())) {
                    log_error("Unknown or invalid generic type argument '" + argument + "'");
                    return nullptr;
                }
                arguments.push_back(std::move(type));
            }
            return specialize_struct(definition, arguments);
        }
        auto core = resolve_core_type(name, recording->target);
        if (core)
            return get_builtin_type(std::string(core_type_info(*core).name));
        const auto dot = name.find('.');
        if (dot != std::string::npos) {
            auto found = imports.find(name.substr(0, dot));
            if (found != imports.end() && module_scopes.contains(found->second)) {
                auto type = module_scopes.at(found->second)->resolve_type(name.substr(dot + 1));
                auto structure = std::dynamic_pointer_cast<StructType>(type);
                if (structure && structure->is_public)
                    return structure;
            }
            return nullptr;
        }
        if (auto type = current_scope->resolve_type(name))
            return type;
        if (auto *definition = current_scope->resolve_generic_struct(name)) {
            log_error("Generic struct '" + name + "' requires " +
                      std::to_string(definition->generic_params.size()) + " type arguments");
            return nullptr;
        }
        return nullptr;
    }
    // Check for generics: Deferred<T>, Result<T, E>, Spawn<T>
    if (name.find("Deferred<") == 0 && name.back() == '>') {
        std::string inner_name = name.substr(9, name.length() - 10);
        auto inner_type = resolve_type_from_string(inner_name);
        if (inner_type) {
            return std::make_shared<DeferredType>(inner_type);
        }
    }

    if (auto builtin = get_builtin_type(name)) {
        return builtin;
    }
    return current_scope->resolve_type(name);
}

void Sema::enter_scope() { current_scope = std::make_shared<Scope>(current_scope); }

void Sema::check_live_results_handled() {
    if (!recording)
        return;
    for (auto scope = current_scope; scope && scope->parent; scope = scope->parent)
        for (const auto &[name, symbol] : scope->symbols)
            if (dynamic_cast<ResultType *>(symbol.type.get()) &&
                !result_handled[symbol.id])
                log_error("Result '" + name + "' must be checked with .erroneous");
}

void Sema::leave_scope() {
    if (current_scope->parent) {
        for (const auto &[name, symbol] : current_scope->symbols) {
            if (recording && falls_through &&
                dynamic_cast<ResultType *>(symbol.type.get()) &&
                !result_handled[symbol.id])
                log_error("Result '" + name + "' must be checked with .erroneous");
            initialization.erase(symbol.id);
            result_proof.erase(symbol.id);
            result_handled.erase(symbol.id);
        }
        current_scope = current_scope->parent;
    }
}

bool Sema::check_program(const std::vector<std::unique_ptr<Statement>> &program) {
    if (diagnostics_->has_errors())
        return false;
    current_scope = std::make_shared<Scope>();
    collected_functions.clear();
    arena_methods.clear();
    arena_reserved_methods.clear();
    arena_many_methods.clear();
    for (auto &structure : collected_struct_types)
        structure->fields.clear();
    collected_struct_types.clear();
    generic_struct_owners.clear();
    generic_specializations.clear();
    generic_method_specializations.clear();
    generic_function_owners.clear();
    generic_function_specializations.clear();
    generic_specialization_depth = 0;
    initialization.clear();
    result_proof.clear();
    result_handled.clear();
    falls_through = true;
    loop_depth = 0;
    current_return_type.reset();
    current_function = nullptr;
    imports.clear();
    module_imports.clear();
    module_scopes.clear();
    current_module = nullptr;
    if (recording) {
        module_scopes[nullptr] = current_scope;
        if (!prepare_modules(program))
            return false;
        for (const auto *module : recording->modules) {
            select_module(module);
            collect_declarations(module->declarations);
            if (has_error())
                return false;
        }
        select_module(nullptr);
        collect_declarations(program);
        validate_struct_cycles();
        if (has_error())
            return false;
        auto *entry = current_scope->resolve("main");
        if (entry) {
            DiagnosticScope location(current_span, recording->symbols[entry->id].span);
            auto *signature = dynamic_cast<FunctionType *>(entry->type.get());
            if (!signature || !signature->param_types.empty() ||
                !signature->return_type->equals(*get_builtin_type("i32")))
                log_error("Entry point must be a function main() -> i32");
            else
                recording->entry_point = entry->id;
        } else if (recording->mode == CompilationMode::Executable) {
            DiagnosticScope location(current_span,
                                     program.empty() ? SourceSpan{} : program.front()->span);
            log_error("Executable requires an entry point main() -> i32");
        }
        if (has_error())
            return false;
        for (const auto *module : recording->modules) {
            select_module(module);
            check_bodies(module->declarations);
        }
        select_module(nullptr);
        check_bodies(program);
        // A body can instantiate another generic struct. Keep checking until
        // every concrete method body has its own semantic bindings.
        size_t struct_index = 0;
        size_t function_index = 0;
        size_t method_index = 0;
        while (struct_index < generic_specializations.size() ||
               function_index < generic_function_specializations.size() ||
               method_index < generic_method_specializations.size()) {
            if (struct_index < generic_specializations.size()) {
                const auto specialization = generic_specializations[struct_index++];
                const auto *owner = generic_struct_owners.at(specialization.definition);
                select_module(owner);
                current_scope = std::make_shared<Scope>(module_scopes.at(owner));
                for (size_t argument = 0; argument < specialization.resolved_arguments.size(); ++argument)
                    current_scope->define_type(generic_parameter_name(specialization.definition->generic_params[argument]),
                                               specialization.resolved_arguments[argument]);
                for (const auto *method : specialization.method_instances)
                    if (collected_functions.contains(method))
                        check_statement(method);
            } else if (function_index < generic_function_specializations.size()) {
                const auto specialization = generic_function_specializations[function_index++];
                const auto *owner = generic_function_owners.at(specialization.definition);
                select_module(owner);
                current_scope = std::make_shared<Scope>(module_scopes.at(owner));
                for (size_t argument = 0; argument < specialization.resolved_arguments.size(); ++argument)
                    current_scope->define_type(specialization.definition->generic_params[argument],
                                               specialization.resolved_arguments[argument]);
                check_statement(specialization.instance);
            } else {
                const auto specialization = generic_method_specializations[method_index++];
                select_module(specialization.receiver->owner);
                current_scope = std::make_shared<Scope>(module_scopes.at(specialization.receiver->owner));
                bind_enclosing_type_params(specialization.receiver);
                for (size_t argument = 0; argument < specialization.resolved_arguments.size(); ++argument)
                    current_scope->define_type(specialization.definition->generic_params[argument],
                                               specialization.resolved_arguments[argument]);
                check_statement(specialization.instance);
            }
        }
        select_module(nullptr);
        if (!has_error())
            validate_struct_cycles();
    } else {
        for (const auto &statement : program)
            check_statement(statement.get());
    }
    collected_functions.clear();
    current_scope = std::make_shared<Scope>();
    return !has_error();
}

std::shared_ptr<FunctionType> Sema::collect_function(const FunctionDefinition *func_def) {
    DiagnosticScope location(current_span, func_def->span);
    // Resolve return type
    if (!func_def->name || !func_def->return_type || !func_def->body) {
        log_error("Incomplete function declaration");
        return nullptr;
    }
    if (recording && !func_def->generic_params.empty()) {
        log_error("Generic function template requires explicit type arguments at a call");
        return nullptr;
    }
    if (recording && (func_def->is_deferred || func_def->is_spawnable)) {
        log_error("Unsupported function in checked core program");
        return nullptr;
    }
    auto ret_type = resolve_annotation(func_def->return_type.get(), true);
    if (!ret_type) {
        log_error("Error: Unknown return type '" + func_def->return_type->value + "'\n");
        return nullptr;
    }

    if (func_def->is_deferred) {
        if (!dynamic_cast<DeferredType *>(ret_type.get())) {
            log_error("Error: Deferred function '" + func_def->name->value +
                      "' must return Deferred<T>\n");
        }
    }

    // Resolve parameter types
    std::vector<std::shared_ptr<Type>> param_types;
    for (const auto &param : func_def->parameters) {
        if (!param.name || !param.type) {
            log_error("Incomplete parameter declaration");
            return nullptr;
        }
        auto param_type = resolve_annotation(param.type.get());
        if (!param_type) {
            log_error("Error: Unknown parameter type '" + param.type->value + "'\n");
            return nullptr;
        }
        param_types.push_back(param_type);
    }

    // Construct FunctionType
    auto func_type = std::make_shared<FunctionType>(ret_type, param_types);

    // Register function symbol
    Symbol sym;
    sym.name = func_def->name->value;
    sym.is_mutable = false;
    sym.type = func_type;
    if (!define_symbol(func_def->name.get(), sym, SymbolKind::Function))
        return nullptr;
    // Keep source functions independent of the defer bookkeeping's native ABI.
    if (recording && !current_module &&
        (sym.name == "malloc" || sym.name == "free" ||
         arena_operation(sym.name, arena_runtime_names) ||
         memory_operation(sym.name, memory_runtime_names) ||
         standard_operation(sym.name, standard_runtime_names)))
        recording->linkage_names[recording->bindings.at(func_def->name.get())] =
            "gloin.user." + sym.name;
    return func_type;
}

std::shared_ptr<FunctionType>
Sema::specialize_function(const FunctionDefinition *definition,
                          const std::vector<std::shared_ptr<Type>> &arguments,
                          SymbolId &symbol) {
    std::vector<ValueType> keys;
    for (const auto &argument : arguments) {
        auto value = value_type(argument);
        if (!value)
            return nullptr;
        keys.push_back(*value);
    }
    for (const auto &specialization : generic_function_specializations)
        if (specialization.definition == definition && specialization.arguments == keys) {
            symbol = recording->bindings.at(specialization.instance->name.get());
            return specialization.signature;
        }
    if (generic_function_specializations.size() >= 256) {
        log_error("Generic function specialization limit of 256 exceeded");
        return nullptr;
    }

    auto instance = clone_function(*definition);
    instance->generic_params.clear();
    const auto *concrete = instance.get();
    recording->specialized_functions.push_back(std::move(instance));

    const auto *owner = generic_function_owners.at(definition);
    auto saved_scope = current_scope;
    auto saved_module = current_module;
    auto saved_imports = imports;
    current_scope = std::make_shared<Scope>(module_scopes.at(owner));
    current_module = owner;
    imports = module_imports.at(owner);
    for (size_t i = 0; i < arguments.size(); ++i)
        current_scope->define_type(definition->generic_params[i], arguments[i]);
    auto signature = collect_function(concrete);
    current_scope = saved_scope;
    current_module = saved_module;
    imports = std::move(saved_imports);
    if (!signature)
        return nullptr;
    collected_functions.emplace(concrete, signature);
    symbol = recording->bindings.at(concrete->name.get());
    recording->linkage_names[symbol] = "gloin.generic.function." + std::to_string(symbol);
    generic_function_specializations.push_back(
        {definition, std::move(keys), arguments, concrete, signature});
    return signature;
}

std::shared_ptr<Type> Sema::check_generic_function_call(const CallExpression *call, bool &handled) {
    const auto *callee = dynamic_cast<const Identifier *>(call->function.get());
    const SourceModule *imported_module = nullptr;
    std::string qualifier;
    if (const auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get())) {
        const auto *module = dynamic_cast<const Identifier *>(member->left.get());
        if (!module || !imports.contains(module->value) || current_scope->resolve(module->value))
            return nullptr;
        callee = dynamic_cast<const Identifier *>(member->member.get());
        imported_module = imports.at(module->value);
        qualifier = module->value + ".";
    }
    if (!callee)
        return nullptr;
    const auto open = callee->value.find('<');
    if (open == std::string::npos || !callee->value.ends_with('>'))
        return nullptr;
    handled = true;
    const auto name = callee->value.substr(0, open);
    const auto base = qualifier + name;
    const auto argument_names = split_type_arguments(
        std::string_view(callee->value).substr(open + 1, callee->value.size() - open - 2));
    if (!imported_module && current_module && current_module->standard_name == "memory" &&
        (name == "__memory_size_of" || name == "__memory_align_of" ||
         name == "__memory_padding" ||
         name == "__memory_place"))
        return check_memory_intrinsic(call, name, argument_names);
    const FunctionDefinition *definition = nullptr;
    if (imported_module) {
        definition = module_scopes.at(imported_module)->resolve_generic_function(name);
        if (definition && !definition->is_public)
            definition = nullptr;
    } else {
        if (!current_scope->resolve(name))
            definition = current_scope->resolve_generic_function(name);
    }
    if (!definition) {
        log_error("Unknown or inaccessible generic function '" + base + "'");
        return nullptr;
    }
    if (argument_names.size() != definition->generic_params.size()) {
        log_error("Generic function '" + base + "' expects " +
                  std::to_string(definition->generic_params.size()) + " type arguments");
        return nullptr;
    }
    std::vector<std::shared_ptr<Type>> arguments;
    for (const auto &name : argument_names) {
        auto type = resolve_type_from_string(name);
        if (!type || !value_type(type) || dynamic_cast<VoidType *>(type.get())) {
            log_error("Unknown or invalid generic type argument '" + name + "'");
            return nullptr;
        }
        arguments.push_back(std::move(type));
    }
    SymbolId symbol = invalid_symbol;
    auto signature = specialize_function(definition, arguments, symbol);
    if (!signature)
        return nullptr;
    recording->bindings[callee] = symbol;
    if (call->arguments.size() != signature->param_types.size()) {
        log_error("Error: Incorrect number of arguments. Expected " +
                  std::to_string(signature->param_types.size()) + ", got " +
                  std::to_string(call->arguments.size()));
        return nullptr;
    }
    for (size_t i = 0; i < call->arguments.size(); ++i) {
        auto actual = check_typed_expression(call->arguments[i].get(), signature->param_types[i]);
        if (!actual)
            return nullptr;
        if (!actual->equals(*signature->param_types[i])) {
            log_error("Error: Argument " + std::to_string(i + 1) + " type mismatch. Expected " +
                      signature->param_types[i]->to_string() + ", got " + actual->to_string());
            return nullptr;
        }
    }
    return signature->return_type;
}

void Sema::check_statement(const Statement *stmt) {
    DiagnosticScope location(current_span, stmt ? stmt->span : SourceSpan{});
    if (!stmt) {
        log_error("Missing statement");
        return;
    }
    if (recording && !falls_through) {
        log_error("Unreachable statement after an unconditional return");
        return;
    }
    if (const auto *decl = dynamic_cast<const VariableDeclaration *>(stmt)) {
        if (decl->is_const) {
            check_constant(decl);
            return;
        }
        if (!decl->name || (recording && !decl->type)) {
            log_error("Binding requires a name and explicit type");
            return;
        }
        // Resolve variable type
        std::shared_ptr<Type> var_type = nullptr;
        if (decl->type) {
            var_type = resolve_annotation(decl->type.get());
            if (!var_type) {
                log_error("Error: Unknown type '" + decl->type->value + "'\n");
                return;
            }
        }

        // Check initializer
        if (decl->initializer) {
            auto init_type = check_typed_expression(decl->initializer.get(), var_type);
            if (init_type) {
                if (var_type) {
                    // Type mismatch check
                    if (!var_type->equals(*init_type)) {
                        log_error("Error: Type mismatch in variable declaration. Expected " +
                                  var_type->to_string() + " but got " + init_type->to_string() +
                                  "\n");
                    }
                } else {
                    // Inference
                    var_type = init_type;
                }
            }
        }

        if (!var_type) {
            log_error("Error: Cannot infer type for variable '" + decl->name->value + "'\n");
            return; // Or fallback to error type
        }

        // Define variable in scope
        Symbol sym;
        sym.name = decl->name->value;
        sym.is_mutable = decl->is_mutable;
        sym.type = var_type;

        if (define_symbol(decl->name.get(), sym, SymbolKind::Variable) && recording)
            initialization[recording->bindings.at(decl->name.get())] =
                decl->initializer ? Initialization::Initialized : Initialization::Uninitialized;

    } else if (const auto *block = dynamic_cast<const BlockStatement *>(stmt)) {
        enter_scope();
        for (const auto &s : block->statements) {
            check_statement(s.get());
        }
        leave_scope();
    } else if (const auto *func_def = dynamic_cast<const FunctionDefinition *>(stmt)) {
        std::shared_ptr<FunctionType> func_type;
        if (recording) {
            auto found = collected_functions.find(func_def);
            if (found == collected_functions.end()) {
                log_error("Nested or uncollected function in checked core program");
                return;
            }
            func_type = found->second;
        } else {
            func_type = collect_function(func_def);
            if (!func_type)
                return;
        }
        // Each body starts with independent control-flow and initialization state.
        falls_through = true;
        loop_depth = 0;
        current_return_type = func_type->return_type;
        current_function = func_def;
        enter_scope();
        // Register parameters in local scope
        for (size_t i = 0; i < func_def->parameters.size(); ++i) {
            Symbol param_sym;
            param_sym.name = func_def->parameters[i].name->value;
            param_sym.type = func_type->param_types[i];
            param_sym.is_mutable = false;
            if (define_symbol(func_def->parameters[i].name.get(), param_sym,
                              SymbolKind::Parameter) &&
                recording)
                initialization[recording->bindings.at(func_def->parameters[i].name.get())] =
                    Initialization::Initialized;
        }

        if (const auto *block = dynamic_cast<const BlockStatement *>(func_def->body.get())) {
            for (const auto &s : block->statements) {
                check_statement(s.get());
            }
        }
        if (recording && !current_return_type->equals(*get_builtin_type("void")) && falls_through)
            log_error("Non-void function '" + func_def->name->value +
                      "' can reach the end without returning a value");
        leave_scope();
        current_return_type.reset();
        current_function = nullptr;
        falls_through = true;

    } else if (const auto *defer = dynamic_cast<const DeferStatement *>(stmt)) {
        if (!recording || !current_function) {
            log_error("defer requires a checked function body");
            return;
        }
        if (!dynamic_cast<const CallExpression *>(defer->call.get())) {
            log_error("defer requires a function or method call");
            return;
        }
        if (auto type = check_expression(defer->call.get(), std::nullopt, true)) {
            if (dynamic_cast<ResultType *>(type.get()))
                log_error("A deferred result cannot be discarded");
            else
                recording->defers[current_function].push_back(defer);
        }

    } else if (const auto *ret = dynamic_cast<const ReturnStatement *>(stmt)) {
        if (recording && !current_return_type) {
            log_error("Return is only allowed inside a function");
            return;
        }
        if (ret->return_value) {
            if (recording && current_return_type->equals(*get_builtin_type("void")))
                log_error("Void function cannot return a value; use return;");
            auto *result = recording ? dynamic_cast<ResultType *>(current_return_type.get())
                                     : nullptr;
            auto type = check_typed_expression(ret->return_value.get(),
                                               result ? result->value : current_return_type);
            if (recording && type && result) {
                if (!type->equals(*result->value) && !dynamic_cast<ErrorType *>(type.get()))
                    log_error("result return must be a value of " + result->value->to_string() +
                              " or an error");
            } else if (recording && type &&
                       !current_return_type->equals(*get_builtin_type("void")) &&
                       !type->equals(*current_return_type))
                log_error("Return type mismatch. Expected " + current_return_type->to_string() +
                          ", got " + type->to_string());
        } else if (recording && !current_return_type->equals(*get_builtin_type("void")) &&
                   !(dynamic_cast<ResultType *>(current_return_type.get()) &&
                     dynamic_cast<ResultType *>(current_return_type.get())->value->equals(
                         *get_builtin_type("void")))) {
            log_error("Non-void function must return a value");
        }
        check_live_results_handled();
        falls_through = false;
    } else if (const auto *if_stmt = dynamic_cast<const IfStatement *>(stmt)) {
        check_conditional(if_stmt->condition.get(), if_stmt->consequence.get(),
                          if_stmt->alternative.get(), "If");
    } else if (const auto *unless_stmt = dynamic_cast<const UnlessStatement *>(stmt)) {
        check_conditional(unless_stmt->condition.get(), unless_stmt->consequence.get(), nullptr,
                          "Unless");
    } else if (const auto *for_stmt = dynamic_cast<const ForStatement *>(stmt)) {
        check_for(for_stmt);
    } else if (const auto *while_stmt = dynamic_cast<const WhileStatement *>(stmt)) {
        auto cond_type = check_expression(while_stmt->condition.get());
        if (cond_type && !cond_type->equals(*get_builtin_type("bool"))) {
            DiagnosticScope condition_location(current_span, while_stmt->condition->span);
            log_error("While condition must be bool");
        }
        auto before = initialization;
        bool before_reaches = falls_through;
        auto before_proof = result_proof;
        auto before_handled = result_handled;
        auto refinement = recording ? result_condition(while_stmt->condition.get())
                                    : std::nullopt;
        if (refinement) {
            before_handled[refinement->first] = true;
            result_handled[refinement->first] = true;
            result_proof[refinement->first] = refinement->second;
        }
        ++loop_depth;
        check_statement(while_stmt->body.get());
        --loop_depth;
        bool body_reaches = falls_through;
        // The body may execute zero times. Repeated immutable stores to an outer
        // declaration are rejected at the assignment, even on the first iteration.
        merge_initialization(before, before, before_reaches, initialization, falls_through);
        auto body_proof = result_proof;
        auto body_handled = result_handled;
        result_proof = before_proof;
        result_handled = before_handled;
        merge_result_flow(before_proof, before_handled, before_reaches,
                          body_proof, body_handled, body_reaches);
        for (auto &[id, proof] : result_proof)
            proof = ResultProof::Unknown;
    } else if (const auto *expr_stmt = dynamic_cast<const ExpressionStatement *>(stmt)) {
        auto type = check_expression(expr_stmt->expression.get(), std::nullopt, true);
        if (recording && dynamic_cast<ResultType *>(type.get()))
            log_error("A result cannot be discarded; bind it and check .erroneous");
    } else if (const auto *struct_def = dynamic_cast<const StructDefinition *>(stmt)) {
        if (recording) {
            log_error("Structs are not supported in checked core programs");
            return;
        }
        std::vector<StructType::Field> fields;

        // Check backing type for packed structs
        if (struct_def->is_packed) {
            if (!struct_def->backing_type) {
                log_error(diagnostic_text("Error: Packed struct '", struct_def->name->value,
                                          "' must specify a backing integer type\n"));
            } else {
                // Verify backing type is valid integer
                // For now just checking it's a known type
                if (!resolve_type_from_string(struct_def->backing_type->value)) {
                    log_error(diagnostic_text("Error: Unknown backing type '",
                                              struct_def->backing_type->value, "'\n"));
                }
            }
        }

        for (const auto &field : struct_def->fields) {
            std::shared_ptr<Type> field_type = nullptr;
            if (field.type) {
                field_type = resolve_type_from_string(field.type->value);
                if (!field_type) {
                    log_error(diagnostic_text("Error: Unknown type '", field.type->value,
                                              "' in struct field '", field.name->value, "'\n"));
                    // Continue?
                }
            } else {
                // Implicit type not supported yet for fields? Or bitfields?
                {
                    // Assume bitfield or similar?
                    // For now just error
                    log_error(
                        diagnostic_text("Error: Field '", field.name->value, "' missing type\n"));
                }
            }

            fields.push_back({field.name->value, field_type, field.is_public});
        }

        auto struct_type =
            std::make_shared<StructType>(struct_def->name->value, fields, struct_def->is_packed);
        current_scope->define_type(struct_def->name->value, struct_type);

        for (const auto &method : struct_def->methods) {
            DiagnosticScope method_location(current_span, method->span);
            log_error("Struct method checking is not implemented (SPEC-026)");
        }
    } else {
        log_error("Unsupported statement in semantic analysis");
    }
}

std::shared_ptr<Type> Sema::check_expression_impl(const Expression *expr) {
    DiagnosticScope location(current_span, expr ? expr->span : SourceSpan{});
    if (!expr) {
        log_error("Missing expression");
        return nullptr;
    }
    if (checking_constant)
        return check_constant_expression(expr);
    if (dynamic_cast<const NullLiteral *>(expr)) {
        if (!expected_pointer || !expected_pointer->nullable) {
            log_error("null requires a nullable pointer type (*T), not a reference");
            return nullptr;
        }
        return expected_pointer;
    }
    if (dynamic_cast<const ZeroedLiteral *>(expr)) {
        if (!recording || !expected_array) {
            log_error("zeroed requires a contextual fixed-array type [T; N]");
            return nullptr;
        }
        if (expected_array->length != 0 &&
            !zeroable_array_element(expected_array->element)) {
            log_error("zeroed requires zeroable elements (not non-null references or structs)");
            return nullptr;
        }
        return expected_array;
    }
    if (const auto *ident = dynamic_cast<const Identifier *>(expr)) {
        Symbol *sym = current_scope->resolve(ident->value);
        if (recording && !sym) {
            auto bound = current_scope->resolve_type(ident->value);
            if (auto *size = dynamic_cast<ConstSizeType *>(bound.get())) {
                recording->literals[expr] =
                    ConstantValue{CoreType::I64, llvm::APInt(64, size->value)};
                return get_builtin_type("i64");
            }
        }
        if (!sym) {
            if (recording && current_scope->resolve_generic_function(ident->value)) {
                log_error("Generic function '" + ident->value +
                          "' requires explicit type arguments");
                return nullptr;
            }
            log_error("Error: Undefined variable '" + ident->value + "'\n");
            return nullptr;
        }
        if (recording) {
            recording->bindings[ident] = sym->id;
            if ((sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Parameter) &&
                initialization.at(sym->id) != Initialization::Initialized) {
                log_error("Read of uninitialized variable '" + ident->value + "'");
                return nullptr;
            }
        }
        return sym->type;
    } else if (const auto *bool_lit = dynamic_cast<const BooleanLiteral *>(expr)) {
        return get_builtin_type("bool");
    } else if (const auto *str_lit = dynamic_cast<const StringLiteral *>(expr)) {
        if (recording)
            recording->literals[expr] = ConstantValue{CoreType::String, str_lit->value};
        return get_builtin_type("string");
    } else if (const auto *array = dynamic_cast<const ArrayLiteral *>(expr)) {
        if (!recording || !array->braced || !expected_array) {
            log_error("Fixed-array initializer requires a declared [T; N] type and braces");
            return nullptr;
        }
        auto target = expected_array;
        if (array->repeated) {
            if (array->elements.size() != 2)
                return nullptr;
            size_t count = 0;
            bool valid = false;
            if (auto *literal = dynamic_cast<IntegerLiteral *>(array->elements[1].get())) {
                auto [end, error] = std::from_chars(literal->literal.data(),
                    literal->literal.data() + literal->literal.size(), count);
                valid = error == std::errc{} &&
                        end == literal->literal.data() + literal->literal.size();
            } else if (auto *name = dynamic_cast<Identifier *>(array->elements[1].get())) {
                auto bound = current_scope->resolve_type(name->value);
                if (auto *size = dynamic_cast<ConstSizeType *>(bound.get())) {
                    count = size->value;
                    valid = true;
                }
            }
            if (!valid || count != target->length)
                log_error("Repeated-array count must equal the declared length");
            auto actual = check_typed_expression(array->elements[0].get(), target->element);
            if (actual && !actual->equals(*target->element))
                log_error("Repeated-array element type mismatch");
            return target;
        }
        if (array->elements.size() != target->length)
            log_error("Fixed-array initializer element count does not match its length");
        for (const auto &element : array->elements) {
            auto actual = check_typed_expression(element.get(), target->element);
            if (actual && !actual->equals(*target->element))
                log_error("Fixed-array initializer element type mismatch");
        }
        return target;
    } else if (const auto *index = dynamic_cast<const IndexExpression *>(expr)) {
        auto base = check_expression(index->left.get());
        auto array = std::dynamic_pointer_cast<ArrayType>(base);
        auto slice = std::dynamic_pointer_cast<SliceType>(base);
        if (!array && !slice) {
            if (base)
                log_error("Indexing requires a fixed array or slice");
            return nullptr;
        }
        auto subscript = check_expression(index->index.get(), CoreType::U64);
        auto core = subscript ? resolve_core_type(subscript->to_string()) : std::nullopt;
        if (core && core_type_info(*core).is_integer)
            return array ? array->element : slice->element;
        if (subscript)
            log_error("Array or slice index must be an integer");
        return nullptr;
    } else if (const auto *range = dynamic_cast<const SliceExpression *>(expr)) {
        auto base = check_expression(range->left.get());
        auto array = std::dynamic_pointer_cast<ArrayType>(base);
        auto slice = std::dynamic_pointer_cast<SliceType>(base);
        auto pointer = std::dynamic_pointer_cast<PointerType>(base);
        const bool vector_internal = current_module && current_module->standard_name == "vector";
        if (!array && !slice && !(pointer && vector_internal)) {
            if (base)
                log_error("Slicing requires a fixed array or slice");
            return nullptr;
        }
        if (pointer && !array && !slice && !range->end) {
            log_error("Pointer slice requires an explicit end bound");
            return nullptr;
        }
        bool read_only = slice ? slice->read_only : pointer && pointer->read_only;
        if (array) {
            auto place = check_place(range->left.get(), true);
            if (!place.addressable) {
                log_error("Cannot borrow a temporary fixed array as a slice");
                return nullptr;
            }
            read_only = !place.writable;
        }
        for (const Expression *bound : {range->start.get(), range->end.get()}) {
            if (!bound)
                continue;
            auto type = check_expression(bound, CoreType::U64);
            auto core = type ? resolve_core_type(type->to_string()) : std::nullopt;
            if (core && core_type_info(*core).is_integer)
                continue;
            if (type)
                log_error("Slice range bound must be an integer");
            return nullptr;
        }
        return std::make_shared<SliceType>(array ? array->element :
                                           slice ? slice->element : pointer->pointee, read_only);
    } else if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
        if (recording && (prefix->op == "&" || prefix->op == "*"))
            return check_pointer_unary(prefix);
        auto operand = check_expression(prefix->right.get(), expected_type);
        if (!operand)
            return nullptr;
        auto core = resolve_core_type(operand->to_string());
        auto result = core ? unary_operator_type(prefix->op, *core) : std::nullopt;
        if (!result) {
            log_error("Invalid unary operator '" + prefix->op + "' for " + operand->to_string());
            return nullptr;
        }
        return get_builtin_type(std::string(core_type_info(*result).name));
    } else if (const auto *bin = dynamic_cast<const InfixExpression *>(expr)) {
        if (recording &&
            (std::dynamic_pointer_cast<PointerType>(expression_type_hint(bin->left.get())) ||
             std::dynamic_pointer_cast<PointerType>(expression_type_hint(bin->right.get())) ||
             dynamic_cast<const NullLiteral *>(bin->left.get()) ||
             dynamic_cast<const NullLiteral *>(bin->right.get())))
            return bin->op == "+" ? check_pointer_offset(bin) : check_pointer_comparison(bin);
        auto [left_type, right_type] = check_binary_operands(bin);

        if (!left_type || !right_type)
            return nullptr;

        if (!left_type->equals(*right_type)) {
            log_error("Error: Type mismatch in binary expression. Left: " + left_type->to_string() +
                      ", Right: " + right_type->to_string() + "\n");
            return nullptr;
        }

        if (recording && std::dynamic_pointer_cast<EnumType>(left_type)) {
            if (bin->op == "==" || bin->op == "!=")
                return get_builtin_type("bool");
            log_error("Only equality comparisons are supported for enum values");
            return nullptr;
        }

        auto core = resolve_core_type(left_type->to_string());
        auto result = core ? binary_operator_type(bin->op, *core) : std::nullopt;
        if (!result) {
            log_error("Invalid binary operator '" + bin->op + "' for " + left_type->to_string());
            return nullptr;
        }
        return get_builtin_type(std::string(core_type_info(*result).name));

    } else if (const auto *assign = dynamic_cast<const AssignmentExpression *>(expr)) {
        if (recording && !dynamic_cast<const Identifier *>(assign->left.get()))
            return check_indirect_assignment(assign);
        const auto *ident = dynamic_cast<const Identifier *>(assign->left.get());
        if (!ident) {
            log_error("Assignment target must be a local variable name in the scalar core");
            return nullptr;
        }
        Symbol *symbol = current_scope->resolve(ident->value);
        if (!symbol) {
            DiagnosticScope target_location(current_span, ident->span);
            log_error("Undefined variable '" + ident->value + "'");
            return nullptr;
        }
        auto left_type = symbol->type;
        bool writable = true;
        if (recording) {
            recording->bindings[ident] = symbol->id;
            if (symbol->kind != SymbolKind::Variable) {
                log_error("Cannot assign to immutable variable '" + ident->value + "'");
                return nullptr;
            }
            recording->types[ident] = recording->symbols[symbol->id].type;
            if (!symbol->is_mutable &&
                (initialization.at(symbol->id) != Initialization::Uninitialized ||
                 symbol->loop_depth < loop_depth))
                writable = false;
        } else {
            writable = symbol->is_mutable;
        }
        if (!writable)
            log_error("Cannot assign to immutable variable '" + ident->value + "'");
        // A target is a write, not a read. Check the RHS before changing its state.
        auto right_type = check_typed_expression(assign->right.get(), left_type);
        if (left_type && right_type && !left_type->equals(*right_type)) {
            log_error("Error: Type mismatch in assignment\n");
        } else if (recording && writable && right_type) {
            initialization[symbol->id] = Initialization::Initialized;
            if (dynamic_cast<ResultType *>(left_type.get())) {
                result_proof[symbol->id] = ResultProof::Unknown;
                result_handled[symbol->id] = false;
            }
        }
        return left_type;

    } else if (const auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
        if (recording) {
            bool handled = false;
            auto variant = check_enum_variant(member_access, handled);
            if (handled)
                return variant;
            auto *symbol = module_member(member_access, handled, true);
            if (handled)
                return symbol ? symbol->type : nullptr;
            return check_field(member_access);
        }
        auto obj_type = check_expression(member_access->left.get());
        if (!obj_type)
            return nullptr;

        // Ensure member is an identifier
        const auto *member_ident = dynamic_cast<const Identifier *>(member_access->member.get());
        if (!member_ident) {
            log_error("Error: Member access must use an identifier\n");
            return nullptr;
        }

        if (const auto *struct_type = dynamic_cast<const StructType *>(obj_type.get())) {
            const auto *field = struct_type->get_field(member_ident->value);
            if (field) {
                return field->type;
            }

            log_error("Error: Struct '" + struct_type->name + "' has no field '" +
                      member_ident->value + "'\n");
            return nullptr;
        }

        log_error("Error: Accessing member '" + member_ident->value + "' on non-struct type '" +
                  obj_type->to_string() + "'\n");
        return nullptr;

    } else if (const auto *call = dynamic_cast<const CallExpression *>(expr)) {
        if (recording) {
            const auto *name = dynamic_cast<const Identifier *>(call->function.get());
            if (name && name->value == "error") {
                if (call->arguments.size() != 1 ||
                    !dynamic_cast<const StringLiteral *>(call->arguments.front().get())) {
                    log_error("error construction requires one string literal");
                    return nullptr;
                }
                if (!check_expression(call->arguments.front().get(), CoreType::String))
                    return nullptr;
                recording->error_constructors.insert(call);
                return std::make_shared<ErrorType>();
            }
        }
        if (recording) {
            bool handled = false;
            auto generic = check_generic_function_call(call, handled);
            if (handled)
                return generic;
            auto result = check_method_call(call, handled);
            if (handled)
                return result;
        }
        const auto *direct = dynamic_cast<const Identifier *>(call->function.get());
        if (recording && current_module && direct) {
            if (auto kind = standard_operation(direct->value, standard_primitive_names);
                kind && standard_primitive_allowed(*kind, current_module->standard_name))
                return check_standard_primitive(call, *kind);
        }
        if (recording && current_module && current_module->standard_name == "arena" && direct) {
            if (auto kind = arena_operation(direct->value, arena_primitive_names))
                return check_arena_primitive(call, *kind);
        }
        if (recording && current_module && current_module->standard_name == "memory" && direct) {
            if (auto kind = memory_operation(direct->value, memory_primitive_names))
                return check_memory_primitive(call, *kind);
        }
        if (recording && current_module && !current_module->standard_name.empty() && direct && direct->value == "__write_stdout") {
            if (call->arguments.size() != 1) {
                log_error("__write_stdout expects exactly one string argument");
                return nullptr;
            }
            auto argument = check_expression(call->arguments.front().get(), CoreType::String);
            if (!argument)
                return nullptr;
            if (!argument->equals(*get_builtin_type("string"))) {
                log_error("__write_stdout expects a string argument");
                return nullptr;
            }
            recording->runtime_calls.insert(call);
            return get_builtin_type("void");
        }
        if (recording && !direct && !dynamic_cast<const MemberAccessExpression *>(call->function.get())) {
            log_error("Core calls require a direct function name");
            return nullptr;
        }
        bool previous_callee = resolving_callee;
        resolving_callee = true;
        auto func_expr_type = check_expression(call->function.get());
        resolving_callee = previous_callee;
        if (!func_expr_type)
            return nullptr;

        const auto *func_type = dynamic_cast<const FunctionType *>(func_expr_type.get());
        if (!func_type) {
            log_error("Error: Expression is not callable (type: " + func_expr_type->to_string() +
                      ")\n");
            return nullptr;
        }

        if (call->arguments.size() != func_type->param_types.size()) {
            log_error("Error: Incorrect number of arguments. Expected " +
                      std::to_string(func_type->param_types.size()) + ", got " +
                      std::to_string(call->arguments.size()) + "\n");
            return nullptr;
        }

        for (size_t i = 0; i < call->arguments.size(); ++i) {
            auto arg_type =
                check_typed_expression(call->arguments[i].get(), func_type->param_types[i]);
            if (!arg_type)
                return nullptr;

            if (!arg_type->equals(*func_type->param_types[i])) {
                log_error("Error: Argument " + std::to_string(i + 1) + " type mismatch. Expected " +
                          func_type->param_types[i]->to_string() + ", got " +
                          arg_type->to_string() + "\n");
                return nullptr;
            }
        }

        return func_type->return_type;
    } else if (const auto *literal = dynamic_cast<const StructLiteral *>(expr)) {
        if (recording)
            return check_struct_literal(literal);
        log_error("Struct literals require checked semantic analysis");
        return nullptr;
    } else if (const auto *spawn = dynamic_cast<const SpawnExpression *>(expr)) {
        // Check inner call
        const auto *call = dynamic_cast<const CallExpression *>(spawn->call.get());
        if (!call) {
            log_error("Error: 'spawn' must be applied to a function call\n");
            return nullptr;
        }

        auto ret_type = check_expression(spawn->call.get());
        if (ret_type) {
            return std::make_shared<DeferredType>(ret_type);
        }
        return nullptr;

    } else if (const auto *await_expr = dynamic_cast<const AwaitExpression *>(expr)) {
        auto inner_type = check_expression(await_expr->expr.get());
        if (!inner_type)
            return nullptr;

        if (const auto *deferred = dynamic_cast<const DeferredType *>(inner_type.get())) {
            return deferred->value_type;
        }

        log_error("Error: 'await' applied to non-deferred type '" + inner_type->to_string() +
                  "'\n");
        return nullptr;
    }

    log_error("Unsupported expression in semantic analysis");
    return nullptr;
}

std::unique_ptr<CheckedProgram>
Sema::check_for_codegen(std::vector<std::unique_ptr<Statement>> program, TargetInfo target,
                        CompilationMode mode) {
    if (has_error())
        return nullptr;
    if (target.pointer_bits != 64) {
        log_error("Only 64-bit targets are supported");
        return nullptr;
    }
    recording.emplace();
    recording->target = target;
    recording->mode = mode;
    if (!check_program(program)) {
        recording.reset();
        return nullptr;
    }
    auto data = std::move(*recording);
    recording.reset();
    return std::unique_ptr<CheckedProgram>(new CheckedProgram(std::move(program), std::move(data)));
}

std::shared_ptr<Type> Sema::resolve_annotation(const Identifier *annotation, bool allow_void) {
    if (!annotation) {
        log_error("Missing type annotation");
        return nullptr;
    }
    auto type = resolve_type_from_string(annotation->value);
    if (recording) {
        DiagnosticScope location(current_span, annotation->span);
        auto core = value_type(type);
        if (!core || dynamic_cast<ConstSizeType *>(type.get())) {
            if (annotation->value.starts_with("["))
                log_error("Invalid fixed-array type '" + annotation->value +
                          "': use a supported element type and a decimal length up to 1048576");
            else
                log_error("Unknown or unsupported core type '" + annotation->value + "'");
            return nullptr;
        }
        if (*core == CoreType::Void && !allow_void) {
            log_error("void is only allowed as a function return type");
            return nullptr;
        }
        recording->types[annotation] = *core;
    }
    return type;
}

bool Sema::define_symbol(const Identifier *name, Symbol symbol, SymbolKind kind) {
    symbol.kind = kind;
    symbol.loop_depth = loop_depth;
    DiagnosticScope location(current_span, name->span);
    if (!current_scope->parent && imports.contains(name->value)) {
        log_error("Declaration conflicts with imported module '" + name->value + "'");
        return false;
    }
    if (current_module && !current_module->standard_name.empty() && name->value == "__write_stdout") {
        log_error("Cannot redeclare the native byte-output primitive");
        return false;
    }
    if (current_module) {
        if (auto kind = standard_operation(name->value, standard_primitive_names);
            kind && standard_primitive_allowed(*kind, current_module->standard_name)) {
            log_error("Cannot redeclare a native standard-library primitive");
            return false;
        }
    }
    if (current_module && current_module->standard_name == "arena" &&
        arena_operation(name->value, arena_primitive_names)) {
        log_error("Cannot redeclare a native arena primitive");
        return false;
    }
    if (current_scope->symbols.contains(name->value) ||
        current_scope->types.contains(name->value) ||
        current_scope->generic_structs.contains(name->value) ||
        current_scope->generic_functions.contains(name->value)) {
        log_error("Duplicate declaration '" + name->value + "'");
        return false;
    }
    if (get_builtin_type(name->value)) {
        log_error("Cannot redeclare built-in type '" + name->value + "'");
        return false;
    }
    if (recording) {
        auto symbol_type = symbol.type;
        std::vector<ValueType> parameters;
        if (auto *function = dynamic_cast<FunctionType *>(symbol_type.get())) {
            for (auto &param : function->param_types)
                parameters.push_back(value_type(param).value());
            symbol_type = function->return_type;
        }
        auto core = value_type(symbol_type);
        if (!core) {
            log_error("Unsupported symbol type");
            return false;
        }
        symbol.id = recording->symbols.size();
        recording->symbols.push_back({symbol.id, name->value, kind, *core, std::move(parameters),
                                      symbol.is_mutable, name->span});
        recording->bindings[name] = symbol.id;
        if (dynamic_cast<ResultType *>(symbol.type.get()) && kind != SymbolKind::Function) {
            result_proof[symbol.id] = ResultProof::Unknown;
            result_handled[symbol.id] = false;
        }
    }
    current_scope->define(name->value, std::move(symbol));
    return true;
}

std::shared_ptr<Type> Sema::check_expression(const Expression *expression,
                                             std::optional<CoreType> expected,
                                             bool statement_context) {
    if (recording && !checking_constant && !statement_context &&
        dynamic_cast<const AssignmentExpression *>(expression)) {
        DiagnosticScope location(current_span, expression->span);
        log_error("Assignment is only allowed as a statement");
        return nullptr;
    }
    auto previous = expected_type;
    expected_type = expected;
    const auto *prefix = dynamic_cast<const PrefixExpression *>(expression);
    bool numeric_literal = dynamic_cast<const IntegerLiteral *>(expression) ||
                           dynamic_cast<const FloatLiteral *>(expression) ||
                           (prefix && prefix->op == "-" &&
                            (dynamic_cast<const IntegerLiteral *>(prefix->right.get()) ||
                             dynamic_cast<const FloatLiteral *>(prefix->right.get())));
    auto type =
        numeric_literal ? check_numeric_literal(expression) : check_expression_impl(expression);
    expected_type = previous;
    if (recording && type) {
        DiagnosticScope location(current_span, expression->span);
        if (dynamic_cast<FunctionType *>(type.get())) {
            if (!resolving_callee)
                log_error("Function values are not supported in the core language");
        } else if (auto core = value_type(type)) {
            if (*core == CoreType::Void && !statement_context) {
                log_error("A void call cannot be used as a value");
                return nullptr;
            }
            recording->types[expression] = *core;
        } else {
            log_error("Unsupported expression type in checked core program");
        }
    }
    return type;
}

void Sema::merge_initialization(const InitializationState &before, const InitializationState &left,
                                bool left_reaches, const InitializationState &right,
                                bool right_reaches) {
    InitializationState merged;
    for (const auto &[id, state] : before) {
        auto left_state = left.at(id);
        auto right_state = right.at(id);
        if (!left_reaches)
            merged[id] = right_state;
        else if (!right_reaches)
            merged[id] = left_state;
        else
            merged[id] = left_state == right_state ? left_state : Initialization::MaybeInitialized;
    }
    initialization = std::move(merged);
    falls_through = left_reaches || right_reaches;
}
