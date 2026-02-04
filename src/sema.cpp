#include "sema.h"
#include <iostream>

void Scope::define(const std::string& name, Symbol symbol) {
    symbols[name] = symbol;
}

Symbol* Scope::resolve(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return &it->second;
    }
    if (parent) {
        return parent->resolve(name);
    }
    return nullptr;
}

void Scope::define_type(const std::string& name, std::shared_ptr<Type> type) {
    types[name] = type;
}

std::shared_ptr<Type> Scope::resolve_type(const std::string& name) {
    auto it = types.find(name);
    if (it != types.end()) {
        return it->second;
    }
    if (parent) {
        return parent->resolve_type(name);
    }
    return nullptr;
}

Sema::Sema() {
    current_scope = std::make_shared<Scope>();
    
    // Define built-in types
    builtin_types["i8"] = std::make_shared<PrimitiveType>("i8");
    builtin_types["i16"] = std::make_shared<PrimitiveType>("i16");
    builtin_types["i32"] = std::make_shared<PrimitiveType>("i32");
    builtin_types["i64"] = std::make_shared<PrimitiveType>("i64");
    
    builtin_types["u8"] = std::make_shared<PrimitiveType>("u8");
    builtin_types["u16"] = std::make_shared<PrimitiveType>("u16");
    builtin_types["u32"] = std::make_shared<PrimitiveType>("u32");
    builtin_types["u64"] = std::make_shared<PrimitiveType>("u64");

    builtin_types["f32"] = std::make_shared<PrimitiveType>("f32");
    builtin_types["f64"] = std::make_shared<PrimitiveType>("f64");

    builtin_types["bool"] = std::make_shared<PrimitiveType>("bool");
    builtin_types["string"] = std::make_shared<PrimitiveType>("string");
    builtin_types["void"] = std::make_shared<VoidType>();
}

std::shared_ptr<Type> Sema::get_builtin_type(const std::string& name) {
    auto it = builtin_types.find(name);
    if (it != builtin_types.end()) {
        return it->second;
    }
    return nullptr;
}

void Sema::log_error(const std::string& msg) {
    errors.push_back(msg);
    has_errors = true;
    std::cerr << msg; // Keep printing to stderr for now as well
}

std::shared_ptr<Type> Sema::resolve_type_from_string(const std::string& name) {
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

void Sema::enter_scope() {
    current_scope = std::make_shared<Scope>(current_scope);
}

void Sema::leave_scope() {
    if (current_scope->parent) {
        current_scope = current_scope->parent;
    }
}

void Sema::check_program(const std::vector<std::unique_ptr<Statement>>& program) {
    for (const auto& stmt : program) {
        check_statement(stmt.get());
    }
}

void Sema::check_statement(const Statement* stmt) {
    if (const auto* decl = dynamic_cast<const VariableDeclaration*>(stmt)) {
        // Resolve variable type
        std::shared_ptr<Type> var_type = nullptr;
        if (decl->type) {
             var_type = resolve_type_from_string(decl->type->value);
             if (!var_type) {
                 log_error("Error: Unknown type '" + decl->type->value + "'\n");
                 return;
             }
        }
        
        // Check initializer
        if (decl->initializer) {
            auto init_type = check_expression(decl->initializer.get());
            if (init_type) {
                if (var_type) {
                    // Type mismatch check
                    if (!var_type->equals(*init_type)) {
                        log_error("Error: Type mismatch in variable declaration. Expected " 
                                  + var_type->to_string() + " but got " + init_type->to_string() + "\n");
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
        
        current_scope->define(decl->name->value, sym);
        
    } else if (const auto* block = dynamic_cast<const BlockStatement*>(stmt)) {
        enter_scope();
        for (const auto& s : block->statements) {
            check_statement(s.get());
        }
        leave_scope();
    } else if (const auto* func_def = dynamic_cast<const FunctionDefinition*>(stmt)) {
        // Resolve return type
        auto ret_type = resolve_type_from_string(func_def->return_type->value);
        if (!ret_type) {
             log_error("Error: Unknown return type '" + func_def->return_type->value + "'\n");
             // Fallback to void?
             ret_type = get_builtin_type("void"); 
        }

        if (func_def->is_deferred) {
             if (!dynamic_cast<DeferredType*>(ret_type.get())) {
                 log_error("Error: Deferred function '" + func_def->name->value + "' must return Deferred<T>\n");
             }
        }
        
        // Resolve parameter types
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& param : func_def->parameters) {
             auto param_type = resolve_type_from_string(param.type->value);
             if (!param_type) {
                 log_error("Error: Unknown parameter type '" + param.type->value + "'\n");
                 param_type = get_builtin_type("void"); // fallback
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
        current_scope->define(func_def->name->value, sym);
        
        // Check body
        enter_scope();
        // Register parameters in local scope
        for (size_t i = 0; i < func_def->parameters.size(); ++i) {
             Symbol param_sym;
             param_sym.name = func_def->parameters[i].name->value;
             param_sym.type = param_types[i];
             current_scope->define(func_def->parameters[i].name->value, param_sym);
        }
        
        if (const auto* block = dynamic_cast<const BlockStatement*>(func_def->body.get())) {
             for (const auto& s : block->statements) {
                 check_statement(s.get());
             }
        }
        leave_scope();
        
    } else if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt)) {
        if (ret->return_value) {
            check_expression(ret->return_value.get());
            // TODO: Check against function return type
        }
    } else if (const auto* if_stmt = dynamic_cast<const IfStatement*>(stmt)) {
        auto cond_type = check_expression(if_stmt->condition.get());
        if (cond_type && !cond_type->equals(*get_builtin_type("bool"))) {
             std::cerr << "Error: If condition must be bool\n";
        }
        check_statement(if_stmt->consequence.get());
        if (if_stmt->alternative) {
            check_statement(if_stmt->alternative.get());
        }
    } else if (const auto* while_stmt = dynamic_cast<const WhileStatement*>(stmt)) {
        auto cond_type = check_expression(while_stmt->condition.get());
        if (cond_type && !cond_type->equals(*get_builtin_type("bool"))) {
             std::cerr << "Error: While condition must be bool\n";
        }
        check_statement(while_stmt->body.get());
    } else if (const auto* expr_stmt = dynamic_cast<const ExpressionStatement*>(stmt)) {
        check_expression(expr_stmt->expression.get());
    } else if (const auto* struct_def = dynamic_cast<const StructDefinition*>(stmt)) {
        std::vector<StructType::Field> fields;
        
        // Check backing type for packed structs
        if (struct_def->is_packed) {
            if (!struct_def->backing_type) {
                std::cerr << "Error: Packed struct '" << struct_def->name->value << "' must specify a backing integer type\n";
            } else {
                // Verify backing type is valid integer
                // For now just checking it's a known type
                if (!resolve_type_from_string(struct_def->backing_type->value)) {
                     std::cerr << "Error: Unknown backing type '" << struct_def->backing_type->value << "'\n";
                }
            }
        }

        for (const auto& field : struct_def->fields) {
            std::shared_ptr<Type> field_type = nullptr;
            if (field.type) {
                field_type = resolve_type_from_string(field.type->value);
                if (!field_type) {
                     std::cerr << "Error: Unknown type '" << field.type->value << "' in struct field '" << field.name->value << "'\n";
                     // Continue?
                }
            } else {
                 // Implicit type not supported yet for fields? Or bitfields?
                 if (struct_def->is_packed) {
                     // Assume bitfield or similar?
                     // For now just error
                     std::cerr << "Error: Field '" << field.name->value << "' missing type\n";
                 }
            }
            
            fields.push_back({field.name->value, field_type, field.is_public});
        }
        
        auto struct_type = std::make_shared<StructType>(struct_def->name->value, fields, struct_def->is_packed);
        current_scope->define_type(struct_def->name->value, struct_type);
        
        // TODO: Handle methods. 
        // We should register them as functions but with modified names?
        // Or keep them in StructType?
        // The spec says they are syntactic sugar for functions with self pointer.
        // So we should probably check them as functions.
        
        for (const auto& method : struct_def->methods) {
            // Check method body, etc.
            // We need to inject 'self' into method scope.
            
            // For now, just recursive check?
            // But verify signature.
            // Method signature: def name(self, ...)
            
            // We need to construct a FunctionDefinition for the method that includes 'self' with correct type.
            // But the parser already parsed it.
            // If parser handled 'self' keyword as I implemented, we have a parameter named 'self' with type 'Self' or implicit.
            
            // We need to resolve 'Self' to this struct type.
            // So we might need to introduce a type alias 'Self' -> struct_type in the scope of method.
            
            // Also, we should probably register the method name in a way that it doesn't conflict?
            // User said: Foo::bar -> foo_bar(foo: *Foo)
            
            // Ideally we register "StructName_MethodName" in the global scope (or current scope).
            
            // Let's defer method checking implementation details for a moment and focus on StructType definition.
        }
    }
}

std::shared_ptr<Type> Sema::check_expression(const Expression* expr) {
    if (const auto* ident = dynamic_cast<const Identifier*>(expr)) {
        Symbol* sym = current_scope->resolve(ident->value);
        if (!sym) {
            log_error("Error: Undefined variable '" + ident->value + "'\n");
            return nullptr;
        }
        return sym->type;
    } else if (const auto* int_lit = dynamic_cast<const IntegerLiteral*>(expr)) {
        return get_builtin_type("i32"); // Default integer type
    } else if (const auto* bool_lit = dynamic_cast<const BooleanLiteral*>(expr)) {
        return get_builtin_type("bool");
    } else if (const auto* str_lit = dynamic_cast<const StringLiteral*>(expr)) {
        return get_builtin_type("string");
    } else if (const auto* bin = dynamic_cast<const InfixExpression*>(expr)) {
        auto left_type = check_expression(bin->left.get());
        auto right_type = check_expression(bin->right.get());
        
        if (!left_type || !right_type) return nullptr;
        
        if (!left_type->equals(*right_type)) {
             log_error("Error: Type mismatch in binary expression. Left: " 
                       + left_type->to_string() + ", Right: " + right_type->to_string() + "\n");
             return nullptr;
        }
        
        // Return type depends on operator
        if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == ">") {
            return get_builtin_type("bool");
        }
        return left_type; // For arithmetic
        
    } else if (const auto* assign = dynamic_cast<const AssignmentExpression*>(expr)) {
        // Left must be lvalue (identifier for now)
        // And must be mutable
        auto left_type = check_expression(assign->left.get());
        auto right_type = check_expression(assign->right.get());
        
        if (const auto* ident = dynamic_cast<const Identifier*>(assign->left.get())) {
             Symbol* sym = current_scope->resolve(ident->value);
             if (sym && !sym->is_mutable) {
                 log_error("Error: Cannot assign to immutable variable '" + ident->value + "'\n");
             }
        }
        
        if (left_type && right_type && !left_type->equals(*right_type)) {
            log_error("Error: Type mismatch in assignment\n");
        }
        
        return left_type;
        
    } else if (const auto* member_access = dynamic_cast<const MemberAccessExpression*>(expr)) {
        auto obj_type = check_expression(member_access->left.get());
        if (!obj_type) return nullptr;
        
        // Ensure member is an identifier
        const auto* member_ident = dynamic_cast<const Identifier*>(member_access->member.get());
        if (!member_ident) {
             log_error("Error: Member access must use an identifier\n");
             return nullptr;
        }

        if (const auto* struct_type = dynamic_cast<const StructType*>(obj_type.get())) {
             const auto* field = struct_type->get_field(member_ident->value);
             if (field) {
                 return field->type;
             }
             
             log_error("Error: Struct '" + struct_type->name + "' has no field '" + member_ident->value + "'\n");
             return nullptr;
        }
        
        log_error("Error: Accessing member '" + member_ident->value + "' on non-struct type '" + obj_type->to_string() + "'\n");
        return nullptr;
        
    } else if (const auto* call = dynamic_cast<const CallExpression*>(expr)) {
        auto func_expr_type = check_expression(call->function.get());
        if (!func_expr_type) return nullptr;

        const auto* func_type = dynamic_cast<const FunctionType*>(func_expr_type.get());
        if (!func_type) {
             log_error("Error: Expression is not callable (type: " + func_expr_type->to_string() + ")\n");
             return nullptr;
        }

        if (call->arguments.size() != func_type->param_types.size()) {
             log_error("Error: Incorrect number of arguments. Expected " + std::to_string(func_type->param_types.size()) 
                       + ", got " + std::to_string(call->arguments.size()) + "\n");
             return nullptr;
        }

        for (size_t i = 0; i < call->arguments.size(); ++i) {
             auto arg_type = check_expression(call->arguments[i].get());
             if (!arg_type) return nullptr;
             
             if (!arg_type->equals(*func_type->param_types[i])) {
                 log_error("Error: Argument " + std::to_string(i+1) + " type mismatch. Expected " 
                           + func_type->param_types[i]->to_string() + ", got " + arg_type->to_string() + "\n");
                 return nullptr;
             }
        }
        
        return func_type->return_type; 
    } else if (const auto* spawn = dynamic_cast<const SpawnExpression*>(expr)) {
        // Check inner call
        const auto* call = dynamic_cast<const CallExpression*>(spawn->call.get());
        if (!call) {
             log_error("Error: 'spawn' must be applied to a function call\n");
             return nullptr;
        }
        
        auto ret_type = check_expression(spawn->call.get()); 
        if (ret_type) {
             return std::make_shared<DeferredType>(ret_type);
        }
        return nullptr;
        
    } else if (const auto* await_expr = dynamic_cast<const AwaitExpression*>(expr)) {
         auto inner_type = check_expression(await_expr->expr.get());
         if (!inner_type) return nullptr;
         
         if (const auto* deferred = dynamic_cast<const DeferredType*>(inner_type.get())) {
             return deferred->value_type;
         }
         
         log_error("Error: 'await' applied to non-deferred type '" + inner_type->to_string() + "'\n");
         return nullptr;
    }
    
    return nullptr;
}
