#include "sema.h"
#include "operators.h"
#include <iostream>

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
        auto core = resolve_core_type(name, recording->target);
        return core ? get_builtin_type(std::string(core_type_info(*core).name)) : nullptr;
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

void Sema::leave_scope() {
    if (current_scope->parent) {
        for (const auto &[name, symbol] : current_scope->symbols)
            initialization.erase(symbol.id);
        current_scope = current_scope->parent;
    }
}

bool Sema::check_program(const std::vector<std::unique_ptr<Statement>> &program) {
    if (diagnostics_->has_errors())
        return false;
    current_scope = std::make_shared<Scope>();
    collected_functions.clear();
    initialization.clear();
    falls_through = true;
    loop_depth = 0;
    current_return_type.reset();
    // Core types already exist in the registry. Collect every supported signature
    // before visiting any body, so bindings do not depend on declaration order.
    if (recording) {
        for (const auto &stmt : program) {
            if (const auto *function = dynamic_cast<const FunctionDefinition *>(stmt.get())) {
                if (auto type = collect_function(function))
                    collected_functions.emplace(function, std::move(type));
            }
        }
        // File constants are evaluated in lexical order before all bodies.
        for (const auto &stmt : program) {
            if (const auto *constant = dynamic_cast<const VariableDeclaration *>(stmt.get());
                constant && constant->is_const)
                check_constant(constant);
        }
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
        if (has_error()) {
            initialization.clear();
            collected_functions.clear();
            current_scope = std::make_shared<Scope>();
            return false;
        }
    }
    for (const auto &stmt : program) {
        if (recording) {
            if (const auto *constant = dynamic_cast<const VariableDeclaration *>(stmt.get());
                constant && constant->is_const)
                continue;
        }
        if (recording && !dynamic_cast<const FunctionDefinition *>(stmt.get()) &&
            !(dynamic_cast<const VariableDeclaration *>(stmt.get()) &&
              static_cast<const VariableDeclaration *>(stmt.get())->is_const)) {
            DiagnosticScope location(current_span, stmt ? stmt->span : SourceSpan{});
            log_error("Only functions and constants are allowed in a checked core program");
            continue;
        }
        check_statement(stmt.get());
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
    if (recording &&
        (func_def->is_deferred || func_def->is_spawnable || !func_def->generic_params.empty())) {
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
    return func_type;
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
            auto init_type = check_expression(decl->initializer.get(),
                                              var_type ? resolve_core_type(var_type->to_string())
                                                       : std::nullopt);
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
        current_return_type = resolve_core_type(func_type->return_type->to_string());
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
        if (recording && current_return_type != CoreType::Void && falls_through)
            log_error("Non-void function '" + func_def->name->value +
                      "' can reach the end without returning a value");
        leave_scope();
        current_return_type.reset();
        falls_through = true;

    } else if (const auto *ret = dynamic_cast<const ReturnStatement *>(stmt)) {
        if (recording && !current_return_type) {
            log_error("Return is only allowed inside a function");
            return;
        }
        if (ret->return_value) {
            if (recording && current_return_type == CoreType::Void)
                log_error("Void function cannot return a value; use return;");
            auto type = check_expression(ret->return_value.get(), current_return_type);
            if (recording && type && current_return_type != CoreType::Void &&
                resolve_core_type(type->to_string()) != current_return_type)
                log_error("Return type mismatch. Expected " +
                          std::string(core_type_info(*current_return_type).name) + ", got " +
                          type->to_string());
        } else if (recording && current_return_type != CoreType::Void) {
            log_error("Non-void function must return a value");
        }
        falls_through = false;
    } else if (const auto *if_stmt = dynamic_cast<const IfStatement *>(stmt)) {
        auto cond_type = check_expression(if_stmt->condition.get());
        if (cond_type && !cond_type->equals(*get_builtin_type("bool"))) {
            DiagnosticScope condition_location(current_span, if_stmt->condition->span);
            log_error("If condition must be bool");
        }
        auto before = initialization;
        bool before_reaches = falls_through;
        check_statement(if_stmt->consequence.get());
        auto then_state = initialization;
        bool then_reaches = falls_through;
        initialization = before;
        falls_through = before_reaches;
        if (if_stmt->alternative)
            check_statement(if_stmt->alternative.get());
        merge_initialization(before, then_state, then_reaches, initialization, falls_through);
    } else if (const auto *while_stmt = dynamic_cast<const WhileStatement *>(stmt)) {
        auto cond_type = check_expression(while_stmt->condition.get());
        if (cond_type && !cond_type->equals(*get_builtin_type("bool"))) {
            DiagnosticScope condition_location(current_span, while_stmt->condition->span);
            log_error("While condition must be bool");
        }
        auto before = initialization;
        bool before_reaches = falls_through;
        ++loop_depth;
        check_statement(while_stmt->body.get());
        --loop_depth;
        // The body may execute zero times. Repeated immutable stores to an outer
        // declaration are rejected at the assignment, even on the first iteration.
        merge_initialization(before, before, before_reaches, initialization, falls_through);
    } else if (const auto *expr_stmt = dynamic_cast<const ExpressionStatement *>(stmt)) {
        check_expression(expr_stmt->expression.get(), std::nullopt, true);
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
    if (const auto *ident = dynamic_cast<const Identifier *>(expr)) {
        Symbol *sym = current_scope->resolve(ident->value);
        if (!sym) {
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
        return get_builtin_type("string");
    } else if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
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
        auto [left_type, right_type] = check_binary_operands(bin);

        if (!left_type || !right_type)
            return nullptr;

        if (!left_type->equals(*right_type)) {
            log_error("Error: Type mismatch in binary expression. Left: " + left_type->to_string() +
                      ", Right: " + right_type->to_string() + "\n");
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
        auto right_type =
            check_expression(assign->right.get(), resolve_core_type(left_type->to_string()));
        if (left_type && right_type && !left_type->equals(*right_type)) {
            log_error("Error: Type mismatch in assignment\n");
        } else if (recording && writable && right_type) {
            initialization[symbol->id] = Initialization::Initialized;
        }
        return left_type;

    } else if (const auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
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
        if (recording && !dynamic_cast<const Identifier *>(call->function.get())) {
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
                check_expression(call->arguments[i].get(),
                                 resolve_core_type(func_type->param_types[i]->to_string()));
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
        log_error("Only the 64-bit Apple Silicon target is supported");
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
        auto core = resolve_core_type(annotation->value, recording->target);
        if (!core) {
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
    if (current_scope->symbols.contains(name->value)) {
        log_error("Duplicate declaration '" + name->value + "'");
        return false;
    }
    if (get_builtin_type(name->value)) {
        log_error("Cannot redeclare built-in type '" + name->value + "'");
        return false;
    }
    if (recording) {
        auto value_type = symbol.type;
        std::vector<CoreType> parameters;
        if (auto *function = dynamic_cast<FunctionType *>(value_type.get())) {
            for (auto &param : function->param_types)
                parameters.push_back(
                    resolve_core_type(param->to_string(), recording->target).value());
            value_type = function->return_type;
        }
        auto core = resolve_core_type(value_type->to_string(), recording->target);
        if (!core) {
            log_error("Unsupported symbol type");
            return false;
        }
        symbol.id = recording->symbols.size();
        recording->symbols.push_back({symbol.id, name->value, kind, *core, std::move(parameters),
                                      symbol.is_mutable, name->span});
        recording->bindings[name] = symbol.id;
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
        } else if (auto core = resolve_core_type(type->to_string(), recording->target)) {
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
