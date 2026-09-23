#include "codegen.h"
#include "mlir/IR/Diagnostics.h"
#include "standard_runtime.h"
#include "target_layout.h"
#include <iostream>

// using namespace mlir; // Removed to avoid conflict with gloin::Type
// using namespace gloin;

CodeGen::CodeGen(mlir::MLIRContext &ctx, std::shared_ptr<Diagnostics> diagnostics)
    : context(ctx), builder(&ctx), diagnostics_(std::move(diagnostics)) {

    // Load necessary dialects
    context.getOrLoadDialect<gloin::GloinDialect>();
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::memref::MemRefDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();

    theModule = mlir::ModuleOp::create(builder.getUnknownLoc());
    current_scope = std::make_shared<GenScope>();
}

void CodeGen::initialize_unchecked_types() {
    // Initialize basic types
    type_table["i32"] = builder.getI32Type();
    type_table["i64"] = builder.getI64Type();
    type_table["f32"] = builder.getF32Type();
    type_table["bool"] = builder.getI1Type();
    type_table["void"] = builder.getNoneType(); // Or empty tuple?

    // Alias for u32 -> i32 in MLIR for now, or use IntegerType with unsigned semantics where needed
    type_table["u32"] = builder.getI32Type();
    type_table["u8"] = builder.getI8Type();
    type_table["i8"] = builder.getI8Type();
    type_table["u16"] = builder.getI16Type();
    type_table["i16"] = builder.getI16Type();

    (void)string_type();

    create_runtime_functions();
}

mlir::LLVM::LLVMStructType CodeGen::string_type() {
    auto type = mlir::LLVM::LLVMStructType::getIdentified(&context, "gloin.string");
    if (!type.isInitialized()) {
        auto ptr = mlir::LLVM::LLVMPointerType::get(&context);
        (void)type.setBody({ptr, builder.getI64Type()}, false);
    }
    type_table["string"] = type;
    struct_field_indices["string"]["ptr"] = 0;
    struct_field_indices["string"]["len"] = 1;
    struct_field_types["string"]["ptr"] = mlir::LLVM::LLVMPointerType::get(&context);
    struct_field_types["string"]["len"] = builder.getI64Type();
    return type;
}

int64_t CodeGen::get_type_size(mlir::Type type) {
    auto target = native_target_layout();
    if (!target)
        fail(llvm::toString(target.takeError()));
    auto layout = measure_type_layout(type, llvm::DataLayout(target->data_layout));
    if (!layout)
        fail(llvm::toString(layout.takeError()));
    return layout->size;
}

void CodeGen::create_runtime_functions() {

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(theModule.getBody());

    auto ptrType = mlir::LLVM::LLVMPointerType::get(&context);
    auto i64Type = builder.getI64Type();
    auto voidType = mlir::LLVM::LLVMVoidType::get(&context);

    // malloc
    // if (!theModule.lookupSymbol("malloc")) {

    auto mallocType = mlir::LLVM::LLVMFunctionType::get(ptrType, {i64Type}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "malloc", mallocType);
    // }

    // free
    // if (!theModule.lookupSymbol("free")) {

    auto freeType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "free", freeType);
    // }

    // Async Runtime Functions
    // Task* gloin_spawn_task(void* (*func)(void*), void* arg)
    auto taskPtrType = ptrType; // Opaque for now
    auto threadFuncType = mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType}, false);
    auto threadFuncPtrType = mlir::LLVM::LLVMPointerType::get(&context);

    auto spawnType =
        mlir::LLVM::LLVMFunctionType::get(taskPtrType, {threadFuncPtrType, ptrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_spawn_task", spawnType);

    // void* gloin_await_task(Task* task)
    auto awaitType = mlir::LLVM::LLVMFunctionType::get(ptrType, {taskPtrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_await_task", awaitType);
}

void CodeGen::dump() { theModule.dump(); }

void CodeGen::enter_scope() {
    auto new_scope = std::make_shared<GenScope>();
    new_scope->parent = current_scope;
    current_scope = new_scope;
}

void CodeGen::leave_scope() {
    if (current_scope->parent) {
        current_scope = current_scope->parent;
    }
}


void CodeGen::declare(const std::string &name, mlir::Value value, bool is_address, mlir::Type type,
                      const std::string &source_type) {
    SymbolInfo info = {value, is_address, type, source_type};
    current_scope->values[name] = info;
}

mlir::ModuleOp CodeGen::generate_impl(const std::vector<std::unique_ptr<Statement>> &program) {
    if (generated) {
        diagnostics_->error(DiagnosticStage::Codegen, current_span,
                            "CodeGen instances generate one module");
        return {};
    }
    generated = true;
    if (diagnostics_->has_errors()) {
        theModule.erase();
        theModule = {};
        return {};
    }
    mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            diagnostics_->error(DiagnosticStage::Codegen, current_span, diagnostic.str());
        return mlir::success();
    });
    try {
        auto target = native_target_layout();
        if (!target)
            fail(llvm::toString(target.takeError()));
        if (checked_data && llvm::DataLayout(target->data_layout).getPointerSizeInBits() !=
                                checked_data->target.pointer_bits)
            fail("Native target pointer width disagrees with checked types");
        theModule->setAttr("llvm.data_layout", builder.getStringAttr(target->data_layout));
        theModule->setAttr("llvm.target_triple", builder.getStringAttr(target->triple));
        if (!checked_data)
            initialize_unchecked_types();
        builder.setInsertionPointToEnd(theModule.getBody());
        if (checked_data) {
            for (const auto &stmt : program) {
                if (const auto *import = dynamic_cast<const ImportStatement *>(stmt.get()))
                    for (const auto &declaration : import->declarations) {
                        if (const auto *function = dynamic_cast<const FunctionDefinition *>(declaration.get()))
                            declare_function(function);
                        if (const auto *structure = dynamic_cast<const StructDefinition *>(declaration.get()))
                            for (const auto &method : structure->methods)
                                declare_function(method.get());
                    }
                if (const auto *structure = dynamic_cast<const StructDefinition *>(stmt.get()))
                    for (const auto &method : structure->methods)
                        declare_function(method.get());
                if (const auto *function = dynamic_cast<const FunctionDefinition *>(stmt.get()))
                    declare_function(function);
            }
        }
        for (const auto &stmt : program) {
            DiagnosticScope source(current_span, stmt ? stmt->span : SourceSpan{});
            bool checked_constant = checked_data &&
                                    dynamic_cast<const VariableDeclaration *>(stmt.get()) &&
                                    static_cast<const VariableDeclaration *>(stmt.get())->is_const;
            if (!checked_constant && !dynamic_cast<const FunctionDefinition *>(stmt.get()) &&
                !dynamic_cast<const StructDefinition *>(stmt.get()) &&
                !dynamic_cast<const ImportStatement *>(stmt.get()))
                fail("Unsupported top-level statement in code generation");
            gen_statement(stmt.get());
        }
        if (!diagnostics_->has_errors()) {
            module_transferred = true;
            return theModule;
        }
    } catch (const GenerationFailure &) {
        // A failed module must never reach lowering or execution.
    }
    theModule.erase();
    theModule = {};
    return {};
}

[[noreturn]] void CodeGen::fail(const std::string &message) {
    diagnostics_->error(DiagnosticStage::Codegen, current_span, message);
    throw GenerationFailure{};
}

mlir::Location CodeGen::location() {
    if (!current_span.source)
        return builder.getUnknownLoc();
    auto [line, column] = current_span.source->line_column(current_span.begin);
    return mlir::FileLineColLoc::get(&context, current_span.source->name, line, column);
}

mlir::func::FuncOp CodeGen::declare_function(const FunctionDefinition *func_def) {
    DiagnosticScope source(current_span, func_def->span);
    std::vector<mlir::Type> argTypes;
    for (const auto &param : func_def->parameters) {
        argTypes.push_back(checked_data ? checked_type(param.type.get())
                                        : resolve_type(param.type->value));
    }

    mlir::Type retType = builder.getNoneType();
    if (func_def->return_type) {
        retType = checked_data ? checked_type(func_def->return_type.get())
                               : resolve_type(func_def->return_type->value);
    }

    std::vector<mlir::Type> resultTypes;
    if (!llvm::isa<mlir::NoneType>(retType)) {
        resultTypes.push_back(retType);
    }

    auto funcType = builder.getFunctionType(argTypes, resultTypes);
    std::string emitted_name = func_def->name->value;
    if (checked_data) {
        auto found = checked_data->linkage_names.find(checked_binding(func_def->name.get()));
        if (found != checked_data->linkage_names.end())
            emitted_name = found->second;
    }
    auto funcOp = builder.create<mlir::func::FuncOp>(location(), emitted_name, funcType);

    // Register function in table
    if (checked_data)
        checked_functions[checked_binding(func_def->name.get())] = funcOp;
    else
        function_table[func_def->name->value] = {funcOp, func_def};
    return funcOp;
}

void CodeGen::gen_statement(const Statement *stmt) {
    DiagnosticScope source(current_span, stmt ? stmt->span : SourceSpan{});
    if (!stmt)
        fail("Missing statement in code generation");
    if (!has_open_block())
        fail("Unreachable statement in code generation");

    if (auto *func_def = dynamic_cast<const FunctionDefinition *>(stmt)) {
        if (checked_data)
            checked_return_type = checked_data->symbols[checked_binding(func_def->name.get())].type;
        current_function_defers.clear();
        defer_record_types.clear();
        defer_head = {};
        auto funcOp = checked_data ? checked_functions.at(checked_binding(func_def->name.get()))
                                   : declare_function(func_def);
        auto argTypes = funcOp.getFunctionType().getInputs();

        if (!func_def->body)
            return;

        auto *entryBlock = funcOp.addEntryBlock();
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(entryBlock);

        enter_scope();

        for (size_t i = 0; i < func_def->parameters.size(); ++i) {
            auto argVal = entryBlock->getArgument(i);
            const auto *name = func_def->parameters[i].name.get();
            if (checked_data && checked_data->address_taken.contains(checked_binding(name))) {
                auto slot = create_entry_alloca(argTypes[i]);
                builder.create<mlir::LLVM::StoreOp>(location(), argVal, slot);
                declare_binding(name, slot, true, argTypes[i], func_def->parameters[i].type->value);
            } else {
                declare_binding(name, argVal, false, argTypes[i],
                                func_def->parameters[i].type->value);
            }
        }

        if (checked_data)
            prepare_defers(func_def);
        gen_statement(func_def->body.get());

        if (has_open_block()) {
            if (funcOp.getFunctionType().getNumResults() == 0) {
                emit_deferred();
                builder.create<mlir::func::ReturnOp>(location());
            } else {
                if (checked_data)
                    fail("Checked non-void function reaches its end without returning");
                builder.create<mlir::LLVM::UnreachableOp>(location());
            }
            builder.clearInsertionPoint();
        }

        leave_scope();

    } else if (auto *struct_def = dynamic_cast<const StructDefinition *>(stmt)) {
        if (checked_data) {
            (void)checked_type(struct_def);
            for (const auto &method : struct_def->methods)
                gen_statement(method.get());
            return;
        }
        std::string name = struct_def->name->value;

        // If it's a generic template, just store it for later instantiation
        if (!struct_def->generic_params.empty()) {
            generic_struct_defs[name] = struct_def;
            return;
        }

        std::vector<mlir::Type> elementTypes;
        int index = 0;

        auto structType = mlir::LLVM::LLVMStructType::getIdentified(&context, name);
        type_table[name] = structType;

        for (const auto &field : struct_def->fields) {
            std::string fieldName = field.name->value;
            std::string typeName = field.type->value;
            mlir::Type fieldType = resolve_type(typeName);
            if (!fieldType) {
                if (typeName == name + "*") {
                    fieldType = mlir::LLVM::LLVMPointerType::get(&context);
                } else {
                    fail("Unsupported field type: " + typeName);
                }
            }
            elementTypes.push_back(fieldType);

            struct_field_indices[name][fieldName] = index++;
            struct_field_types[name][fieldName] = fieldType;
        }

        if (mlir::failed(structType.setBody(elementTypes, struct_def->is_packed))) {
            fail(diagnostic_text("Failed to set body for struct ", name, '\n'));
        }

        if (!struct_def->methods.empty())
            fail("Methods require checked semantic analysis");

    } else if (auto *var_decl = dynamic_cast<const VariableDeclaration *>(stmt)) {
        if (var_decl->is_const) {
            if (!checked_data ||
                !checked_data->constants.contains(checked_binding(var_decl->name.get())))
                fail("Constants require checked semantic evaluation");
            return;
        }
        std::string name = var_decl->name->value;
        mlir::Type type = builder.getI32Type();
        if (var_decl->type) {
            type = checked_data ? checked_type(var_decl->type.get())
                                : resolve_type(var_decl->type->value);
        }

        mlir::Value initVal;
        if (var_decl->initializer) {
            initVal = gen_expression(var_decl->initializer.get());
        }

        if (var_decl->is_mutable ||
            (checked_data &&
             (!var_decl->initializer ||
              checked_data->address_taken.contains(checked_binding(var_decl->name.get()))))) {
            auto alloca = create_entry_alloca(type);
            if (initVal) {
                builder.create<mlir::LLVM::StoreOp>(location(), initVal, alloca);
            }
            declare_binding(var_decl->name.get(), alloca, true, type,
                            var_decl->type ? var_decl->type->value : "");
        } else {
            if (initVal) {
                declare_binding(var_decl->name.get(), initVal, false, type,
                                var_decl->type ? var_decl->type->value : "");
            } else {
                fail("Uninitialized immutable binding in code generation");
            }
        }

    } else if (auto *return_stmt = dynamic_cast<const ReturnStatement *>(stmt)) {
        if (return_stmt->return_value) {
            auto val = gen_expression(return_stmt->return_value.get());
            if (checked_data) {
                auto function =
                    llvm::dyn_cast<mlir::func::FuncOp>(builder.getBlock()->getParentOp());
                if (checked_data->types.at(return_stmt->return_value.get()) !=
                        checked_return_type ||
                    !function || function.getFunctionType().getNumResults() != 1 ||
                    function.getFunctionType().getResult(0) != val.getType())
                    fail("Return value disagrees with the checked function signature (SPEC-014)");
            }
            emit_deferred();
            builder.create<mlir::func::ReturnOp>(location(), val);
        } else {
            if (checked_data) {
                auto function =
                    llvm::dyn_cast<mlir::func::FuncOp>(builder.getBlock()->getParentOp());
                if (!function || function.getFunctionType().getNumResults() != 0)
                    fail("Missing value for checked return signature (SPEC-014)");
            }
            emit_deferred();
            builder.create<mlir::func::ReturnOp>(location());
        }

        builder.clearInsertionPoint();

    } else if (auto *block = dynamic_cast<const BlockStatement *>(stmt)) {
        enter_scope();
        for (const auto &s : block->statements) {
            gen_statement(s.get());
        }
        leave_scope();

    } else if (auto *if_stmt = dynamic_cast<const IfStatement *>(stmt)) {
        gen_branch(if_stmt->condition.get(), if_stmt->consequence.get(),
                   if_stmt->alternative.get());
    } else if (auto *unless_stmt = dynamic_cast<const UnlessStatement *>(stmt)) {
        gen_branch(unless_stmt->condition.get(), unless_stmt->consequence.get(), nullptr, true);
    } else if (auto *for_stmt = dynamic_cast<const ForStatement *>(stmt)) {
        gen_for(for_stmt);

    } else if (auto *while_stmt = dynamic_cast<const WhileStatement *>(stmt)) {
        gen_while(while_stmt);

    } else if (auto *expr_stmt = dynamic_cast<const ExpressionStatement *>(stmt)) {
        gen_expression(expr_stmt->expression.get(), true);

    } else if (auto *import_stmt = dynamic_cast<const ImportStatement *>(stmt)) {
        if (!checked_data || !import_stmt->loaded)
            handle_import(import_stmt->path);
        for (const auto &declaration : import_stmt->declarations)
            gen_statement(declaration.get());
    } else if (auto *defer_stmt = dynamic_cast<const DeferStatement *>(stmt)) {
        register_defer(defer_stmt);
    } else {
        fail("Unsupported statement in code generation");
    }
}

mlir::Value CodeGen::gen_expression(const Expression *expr, bool allow_void) {
    DiagnosticScope source(current_span, expr ? expr->span : SourceSpan{});
    if (!expr)
        fail("Missing expression in code generation");
    if (!has_open_block())
        fail("Expression has no live continuation in code generation");
    auto value = gen_expression_impl(expr);
    if (checked_data) {
        auto expected = checked_type(expr);
        if ((value && value.getType() != expected) ||
            (!value && !llvm::isa<mlir::NoneType>(expected)))
            fail("Generated value disagrees with its checked type");
    }
    if (!value && !allow_void)
        fail("A void call cannot be used as a value");
    return value;
}

mlir::Value CodeGen::gen_expression_impl(const Expression *expr) {
    if (checked_data) {
        auto literal = checked_data->literals.find(expr);
        if (literal != checked_data->literals.end())
            return emit_constant(literal->second);
    }
    if (auto *int_lit = dynamic_cast<const IntegerLiteral *>(expr)) {
        if (checked_data)
            fail("Missing checked integer literal");
        std::string error;
        auto value = parse_numeric_literal(int_lit->literal, false, CoreType::I32, false, error);
        if (!value)
            fail(error);
        return emit_constant(*value);
    } else if (auto *float_lit = dynamic_cast<const FloatLiteral *>(expr)) {
        if (checked_data)
            fail("Missing checked floating literal");
        std::string error;
        auto value = parse_numeric_literal(float_lit->literal, true, CoreType::F32, false, error);
        if (!value)
            fail(error);
        return emit_constant(*value);
    } else if (dynamic_cast<const NullLiteral *>(expr)) {
        if (!checked_data || !checked_data->types.at(expr).is_pointer())
            fail("Untyped null literal");
        return builder.create<mlir::LLVM::ZeroOp>(location(), checked_type(expr));
    } else if (auto *bool_lit = dynamic_cast<const BooleanLiteral *>(expr)) {
        return builder.create<mlir::arith::ConstantIntOp>(location(), bool_lit->value ? 1 : 0, 1);
    } else if (auto *str_lit = dynamic_cast<const StringLiteral *>(expr)) {
        return emit_constant(ConstantValue{CoreType::String, str_lit->value});
    } else if (auto *array_lit = dynamic_cast<const ArrayLiteral *>(expr)) {
        if (array_lit->elements.empty())
            fail("Unsupported expression or unresolved value in code generation");
        auto firstElem = gen_expression(array_lit->elements[0].get());
        if (!firstElem)
            fail("Unsupported expression or unresolved value in code generation");
        mlir::Type elemType = firstElem.getType();
        auto arrayType = mlir::LLVM::LLVMArrayType::get(elemType, array_lit->elements.size());
        mlir::Value currentArray = builder.create<mlir::LLVM::UndefOp>(location(), arrayType);
        currentArray = builder.create<mlir::LLVM::InsertValueOp>(
            location(), currentArray, firstElem, llvm::ArrayRef<int64_t>{0});
        for (size_t i = 1; i < array_lit->elements.size(); ++i) {
            auto elem = gen_expression(array_lit->elements[i].get());
            if (!elem || elem.getType() != elemType)
                fail("Array element type mismatch in code generation");
            currentArray = builder.create<mlir::LLVM::InsertValueOp>(
                location(), currentArray, elem, llvm::ArrayRef<int64_t>{static_cast<int64_t>(i)});
        }
        return currentArray;
    } else if (auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
        if (checked_data) {
            if (prefix->op == "&")
                return gen_address(prefix->right.get());
            if (prefix->op == "*") {
                auto address = gen_pointer_address(prefix->right.get());
                return builder.create<mlir::LLVM::LoadOp>(location(), checked_type(prefix),
                                                          address);
            }
            return gen_checked_unary(prefix);
        }
        if (prefix->op == "*") {
            auto ptr = gen_expression(prefix->right.get());
            if (!ptr)
                fail("Unsupported expression or unresolved value in code generation");
            const auto *name = dynamic_cast<const Identifier *>(prefix->right.get());
            auto spelling = name ? lookup_binding(name).source_type : std::string{};
            if (spelling.empty() || (spelling[0] != '*' && spelling[0] != '&'))
                fail("Unchecked dereference requires a known pointee type");
            spelling.erase(0, 1);
            if (spelling.starts_with("const "))
                spelling.erase(0, 6);
            return builder.create<mlir::LLVM::LoadOp>(location(), resolve_type(spelling), ptr);
        } else if (prefix->op == "&") {
            return gen_address(prefix->right.get());
        }
    } else if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        if (checked_data) {
            auto found = checked_data->constants.find(checked_binding(ident));
            if (found != checked_data->constants.end())
                return emit_constant(found->second);
        }
        auto sym = lookup_binding(ident);
        if (sym.value) {
            if (sym.is_address) {
                if (llvm::isa<mlir::LLVM::LLVMPointerType>(sym.value.getType())) {
                    return builder.create<mlir::LLVM::LoadOp>(location(), sym.type, sym.value);
                }
                return builder.create<mlir::memref::LoadOp>(location(), sym.value);
            } else {
                return sym.value;
            }
        }
        fail("Unsupported expression or unresolved value in code generation");
    } else if (auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
        if (checked_data) {
            if (checked_data->indirect_members.contains(member_access)) {
                auto address = gen_address(member_access);
                return builder.create<mlir::LLVM::LoadOp>(location(), checked_type(member_access),
                                                          address);
            }
            auto value = gen_expression(member_access->left.get());
            return builder.create<mlir::LLVM::ExtractValueOp>(
                location(), value,
                llvm::ArrayRef<int64_t>{
                    static_cast<int64_t>(checked_data->field_indices.at(member_access))});
        }
        // Try to get address of the member if possible (e.g. if base is addressable)
        auto addr = gen_address(expr);
        if (addr) {
            auto type = get_expression_type(expr);
            return builder.create<mlir::LLVM::LoadOp>(location(), type, addr);
        }

        // If base is NOT addressable (e.g. value type struct in register/SSA value),
        // we extract the value using ExtractValueOp.
        auto baseVal = gen_expression(member_access->left.get());
        if (baseVal) {
            auto baseType = baseVal.getType();
            if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(baseType)) {
                auto ident = dynamic_cast<const Identifier *>(member_access->member.get());
                std::string structName = structType.getName().str();

                // Check if structName is empty (literal struct?) or needs resolution
                // For generics, name is "Box<i32>".

                if (struct_field_indices.count(structName) &&
                    struct_field_indices[structName].count(ident->value)) {

                    int index = struct_field_indices[structName][ident->value];
                    return builder.create<mlir::LLVM::ExtractValueOp>(
                        location(), baseVal, llvm::ArrayRef<int64_t>{index});
                }
            }
        }

        fail("Unsupported expression or unresolved value in code generation");
    } else if (auto *bin = dynamic_cast<const InfixExpression *>(expr)) {
        if (checked_data)
            return gen_checked_binary(bin);
        auto left = gen_expression(bin->left.get());
        auto right = gen_expression(bin->right.get());

        if (!left || !right)
            fail("Unsupported expression or unresolved value in code generation");

        // Legacy stage-only promotion; checked operators use canonical types above.
        if (left.getType() != right.getType()) {
            if (left.getType().isInteger(32) && right.getType().isInteger(64)) {
                left = builder.create<mlir::arith::ExtSIOp>(location(), builder.getI64Type(), left);
            } else if (left.getType().isInteger(64) && right.getType().isInteger(32)) {
                right =
                    builder.create<mlir::arith::ExtSIOp>(location(), builder.getI64Type(), right);
            }
            // Add more cases as needed (float, etc)
        }

        if (bin->op == "+")
            return builder.create<mlir::arith::AddIOp>(location(), left, right);
        if (bin->op == "-")
            return builder.create<mlir::arith::SubIOp>(location(), left, right);
        if (bin->op == "*")
            return builder.create<mlir::arith::MulIOp>(location(), left, right);
        if (bin->op == "/")
            return builder.create<mlir::arith::DivSIOp>(location(), left, right);
        if (bin->op == "==")
            return builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::eq,
                                                       left, right);
        if (bin->op == "<")
            return builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::slt,
                                                       left, right);
        if (bin->op == ">")
            return builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::sgt,
                                                       left, right);
    } else if (auto *assign = dynamic_cast<const AssignmentExpression *>(expr)) {
        auto lhsAddr = gen_address(assign->left.get());
        auto right = gen_expression(assign->right.get());
        if (lhsAddr) {
            if (llvm::isa<mlir::LLVM::LLVMPointerType>(lhsAddr.getType())) {
                builder.create<mlir::LLVM::StoreOp>(location(), right, lhsAddr);
            } else {
                builder.create<mlir::memref::StoreOp>(location(), right, lhsAddr);
            }
            return right;
        }
    } else if (auto *struct_lit = dynamic_cast<const StructLiteral *>(expr)) {
        if (checked_data) {
            auto type = checked_type(struct_lit);
            mlir::Value value = builder.create<mlir::LLVM::ZeroOp>(location(), type);
            const auto &indices = checked_data->literal_fields.at(struct_lit);
            for (size_t i = 0; i < struct_lit->fields.size(); ++i) {
                auto field = gen_expression(struct_lit->fields[i].second.get());
                value = builder.create<mlir::LLVM::InsertValueOp>(
                    location(), value, field,
                    llvm::ArrayRef<int64_t>{static_cast<int64_t>(indices.at(i))});
            }
            return value;
        }
        std::string structName = struct_lit->name->value;
        if (!type_table.count(structName))
            fail("Unsupported expression or unresolved value in code generation");

        mlir::Type type = type_table[structName];
        auto undef = builder.create<mlir::LLVM::UndefOp>(location(), type);
        mlir::Value current = undef;

        for (const auto &field : struct_lit->fields) {
            std::string fieldName = field.first;
            auto val = gen_expression(field.second.get());
            if (!val)
                fail("Unsupported expression or unresolved value in code generation");

            auto fields = struct_field_indices.find(structName);
            if (fields == struct_field_indices.end() || !fields->second.contains(fieldName))
                fail("Unknown struct field: " + fieldName);
            int index = fields->second.at(fieldName);
            current = builder.create<mlir::LLVM::InsertValueOp>(location(), current, val,
                                                                llvm::ArrayRef<int64_t>{index});
        }
        return current;
    } else if (auto *call = dynamic_cast<const CallExpression *>(expr)) {
        if (checked_data)
            return emit_checked_call(call, gen_call_arguments(call));
        std::string funcName;
        mlir::Value selfArg = nullptr;

        // Check if it's a method call: obj.method(...)
        if (auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get())) {
            if (auto *methodId = dynamic_cast<const Identifier *>(member->member.get())) {
                // It is a method call.
                // 1. Get the address of the object (self)
                selfArg = gen_address(member->left.get());

                // If address generation fails (e.g. temporary), try gen_expression and store to
                // temp
                if (!selfArg) {
                    auto val = gen_expression(member->left.get());
                    if (val) {
                        auto one = builder.create<mlir::LLVM::ConstantOp>(
                            location(), builder.getI64Type(), builder.getI64IntegerAttr(1));
                        auto alloca = builder.create<mlir::LLVM::AllocaOp>(
                            location(), mlir::LLVM::LLVMPointerType::get(&context), val.getType(),
                            one, 0);
                        builder.create<mlir::LLVM::StoreOp>(location(), val, alloca);
                        selfArg = alloca;
                    }
                }

                if (selfArg) {
                    // 2. Resolve struct type to get the name
                    // We need the type of the expression, not the pointer type (which is opaque)
                    mlir::Type objType = get_expression_type(member->left.get());

                    if (auto structType =
                            llvm::dyn_cast_or_null<mlir::LLVM::LLVMStructType>(objType)) {
                        std::string structName = structType.getName().str();
                        std::string methodName = methodId->value;
                        funcName = structName + "_" + methodName;
                    }
                }
            }
        }

        if (funcName.empty()) {
            if (auto *id = dynamic_cast<const Identifier *>(call->function.get())) {
                funcName = id->value;
            } else {
                fail(diagnostic_text("Indirect calls not supported yet\n"));
                fail("Unsupported expression or unresolved value in code generation");
            }
        }

        std::vector<mlir::Value> args;
        if (selfArg) {
            args.push_back(selfArg);
        }

        for (const auto &arg : call->arguments) {
            auto val = gen_expression(arg.get());
            if (!val)
                fail("Unsupported expression or unresolved value in code generation");
            args.push_back(val);
        }

        if (function_table.count(funcName)) {
            const auto &info = function_table[funcName];
            auto funcOp = info.funcOp; // Copy handle to avoid const issues
            if (info.definition->is_deferred) {
                // The result of async_call is Deferred<T>
                mlir::Type returnType = builder.getNoneType();
                auto funcResults = funcOp.getFunctionType().getResults();
                if (!funcResults.empty()) {
                    returnType = funcResults[0];
                }
                auto deferredType = gloin::GloinDeferredType::get(&context, returnType);

                auto callOp = builder.create<gloin::AsyncCallOp>(
                    location(), deferredType, mlir::FlatSymbolRefAttr::get(&context, funcName),
                    args);
                return callOp.getHandle();
            } else {
                auto callOp = builder.create<mlir::func::CallOp>(location(), funcOp, args);
                if (callOp.getNumResults() > 0)
                    return callOp.getResult(0);
                return {}; // Resolved void call.
            }
        } else {
            // Check for LLVM intrinsics or runtime functions declared in module but not in
            // function_table
            if (auto funcOp = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(funcName)) {
                auto funcType = funcOp.getFunctionType();
                if (funcType.getNumParams() == args.size()) {
                    for (size_t i = 0; i < args.size(); ++i) {
                        mlir::Type expected = funcType.getParamType(i);
                        mlir::Type actual = args[i].getType();
                        if (expected != actual) {
                            if (actual.isInteger(32) && expected.isInteger(64)) {
                                args[i] = builder.create<mlir::LLVM::SExtOp>(location(), expected,
                                                                             args[i]);
                            }
                        }
                    }
                }

                auto callOp = builder.create<mlir::LLVM::CallOp>(location(), funcOp, args);
                if (callOp.getNumResults() > 0)
                    return callOp.getResult();
                return {}; // Resolved void runtime call.
            }

            fail("Unknown function: " + funcName);
        }
    } else if (auto *spawn = dynamic_cast<const SpawnExpression *>(expr)) {
        if (auto *call = dynamic_cast<const CallExpression *>(spawn->call.get())) {
            std::string funcName;
            if (auto *id = dynamic_cast<const Identifier *>(call->function.get())) {
                funcName = id->value;
            } else {
                fail("Unsupported expression or unresolved value in code generation");
            }

            // Simplification: Assume single pointer argument for now
            // or cast the single argument to void*
            // This is a temporary limitation to get the runtime working.

            mlir::Value argVal;
            if (call->arguments.empty()) {
                // Pass null
                argVal = builder.create<mlir::LLVM::ZeroOp>(
                    location(), mlir::LLVM::LLVMPointerType::get(&context));
            } else if (call->arguments.size() == 1) {
                argVal = gen_expression(call->arguments[0].get());
                if (!argVal)
                    fail("Unsupported expression or unresolved value in code generation");

                // Cast to void* if needed
                if (!llvm::isa<mlir::LLVM::LLVMPointerType>(argVal.getType())) {
                    // Try to inttoptr if integer
                    if (argVal.getType().isInteger(64)) {
                        argVal = builder.create<mlir::LLVM::IntToPtrOp>(
                            location(), mlir::LLVM::LLVMPointerType::get(&context), argVal);
                    } else if (argVal.getType().isInteger(32)) {
                        auto ext = builder.create<mlir::LLVM::ZExtOp>(location(),
                                                                      builder.getI64Type(), argVal);
                        argVal = builder.create<mlir::LLVM::IntToPtrOp>(
                            location(), mlir::LLVM::LLVMPointerType::get(&context),
                            mlir::ValueRange{ext.getResult()});
                    } else {
                        // Fallback: bitcast or error
                        // For now, assume pointer compatible
                        argVal = builder.create<mlir::LLVM::BitcastOp>(
                            location(), mlir::LLVM::LLVMPointerType::get(&context), argVal);
                    }
                } else {
                    argVal = builder.create<mlir::LLVM::BitcastOp>(
                        location(), mlir::LLVM::LLVMPointerType::get(&context), argVal);
                }
            } else {
                fail(diagnostic_text("Spawn only supports 0 or 1 argument currently\n"));
                fail("Unsupported expression or unresolved value in code generation");
            }

            // Get function pointer
            auto funcOp = theModule.lookupSymbol<mlir::func::FuncOp>(funcName);
            if (!funcOp) {
                // Try LLVM func
                auto llvmFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(funcName);
                if (!llvmFunc) {
                    fail(diagnostic_text("Function ", funcName, " not found for spawn\n"));
                    fail("Unsupported expression or unresolved value in code generation");
                }
                // We need a pointer to this function.
                // AddressOfOp works on GlobalOp or FuncOp if we use LLVM dialect.
                // But mlir::func::FuncOp is not directly addressable as an SSA value in standard
                // MLIR unless we convert to LLVM dialect first or use ConstantOp with function
                // type?

                // Actually, we need to look up the LLVM function declaration.
                // If it's a standard FuncOp, we might need a wrapper or ensure it's converted.
                // For now, let's assume we can get the address.

                auto funcPtr = builder.create<mlir::LLVM::AddressOfOp>(location(), llvmFunc);

                auto voidFuncPtr = builder.create<mlir::LLVM::BitcastOp>(
                    location(), mlir::LLVM::LLVMPointerType::get(&context), funcPtr);

                auto spawnFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_spawn_task");
                auto callOp = builder.create<mlir::LLVM::CallOp>(
                    location(), spawnFunc, mlir::ValueRange{voidFuncPtr, argVal});

                return callOp.getResult();
            }

            // If it is a mlir::func::FuncOp, we can't easily take its address until lowering.
            // But we can create a declared LLVM function that aliases it, or relying on lowering.
            // HACK: Declare an LLVM function with same name if it doesn't exist as LLVMFuncOp yet?
            // Or better: ensure all spawned functions are LLVMFuncOps or compatible.

            // Let's assume for this test that the user defines functions that will be lowered.
            // We can emit a gloin.spawn and lower it later, OR
            // We can try to emit LLVM IR directly.

            // To take address of FuncOp, we usually need `mlir::func::ConstantOp`.
            auto funcConst = builder.create<mlir::func::ConstantOp>(
                location(), funcOp.getFunctionType(),
                mlir::FlatSymbolRefAttr::get(&context, funcName));

            // This returns a value of function type. We need to cast this to void*.
            // We use UnrealizedConversionCastOp to bridge the gap until lowering.
            auto cast = builder.create<mlir::UnrealizedConversionCastOp>(
                location(), mlir::LLVM::LLVMPointerType::get(&context), funcConst.getResult());

            auto spawnFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_spawn_task");

            // gloin_spawn_task expects (void* func, void* arg)
            auto callOp = builder.create<mlir::LLVM::CallOp>(
                location(), spawnFunc, mlir::ValueRange{cast.getResult(0), argVal});

            return callOp.getResult();
        }
    } else if (auto *awaitExpr = dynamic_cast<const AwaitExpression *>(expr)) {
        auto handle = gen_expression(awaitExpr->expr.get());
        if (!handle)
            fail("Unsupported expression or unresolved value in code generation");

        auto awaitFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_await_task");
        auto callOp =
            builder.create<mlir::LLVM::CallOp>(location(), awaitFunc, mlir::ValueRange{handle});

        auto resPtr = callOp.getResult();

        // Result is void*. Cast back to expected type.
        // We assume the result is an integer (pointer-sized or smaller) for now.
        // If it's a pointer, we can just bitcast.
        // If it's an integer, we cast ptr -> int -> trunc.

        auto intType = builder.getI64Type(); // void* is 64-bit usually
        auto ptrToInt = builder.create<mlir::LLVM::PtrToIntOp>(location(), intType, resPtr);

        // Truncate to i32 if needed (assuming i32 return for test case)
        auto i32Type = builder.getI32Type();
        auto trunc = builder.create<mlir::LLVM::TruncOp>(location(), i32Type, ptrToInt);

        return trunc;
    }
    fail("Unsupported expression or unresolved value in code generation");
}

mlir::Type CodeGen::get_expression_type(const Expression *expr) {
    if (checked_data)
        return checked_type(expr);
    if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        return lookup(ident->value).type;
    }
    if (dynamic_cast<const IntegerLiteral *>(expr))
        return builder.getI32Type();
    if (dynamic_cast<const BooleanLiteral *>(expr))
        return builder.getI1Type();
    if (dynamic_cast<const FloatLiteral *>(expr))
        return builder.getF32Type();
    if (dynamic_cast<const StringLiteral *>(expr))
        return string_type();

    if (auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
        mlir::Type baseType = get_expression_type(member_access->left.get());

        // Handle pointer to struct (implicit dereference)
        if (llvm::isa<mlir::LLVM::LLVMPointerType>(baseType)) {
            std::string sourceTypeName;
            if (auto *ident = dynamic_cast<const Identifier *>(member_access->left.get())) {
                sourceTypeName = lookup(ident->value).source_type;
            }
            if (!sourceTypeName.empty() && sourceTypeName[0] == '*') {
                std::string structName = sourceTypeName.substr(1);
                if (auto *ident = dynamic_cast<const Identifier *>(member_access->member.get())) {
                    if (struct_field_types.count(structName) &&
                        struct_field_types[structName].count(ident->value)) {
                        return struct_field_types[structName][ident->value];
                    }
                }
            }
        }

        if (auto structType = llvm::dyn_cast_or_null<mlir::LLVM::LLVMStructType>(baseType)) {
            if (auto *ident = dynamic_cast<const Identifier *>(member_access->member.get())) {
                std::string structName = structType.getName().str();
                if (struct_field_types.count(structName) &&
                    struct_field_types[structName].count(ident->value)) {
                    return struct_field_types[structName][ident->value];
                }
            }
        }
    }

    fail("Unknown expression type in code generation");
}

mlir::Value CodeGen::gen_address(const Expression *expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        auto sym = lookup_binding(ident);
        if (sym.value && sym.is_address)
            return sym.value;
    } else if (auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
        if (checked_data) {
            bool indirect = checked_data->indirect_members.contains(member_access);
            auto baseAddr = indirect ? gen_pointer_address(member_access->left.get())
                                     : gen_address(member_access->left.get());
            if (!baseAddr)
                return {};
            auto type = checked_data->types.at(member_access->left.get());
            if (indirect)
                type = type.pointee();
            return builder.create<mlir::LLVM::GEPOp>(
                location(), mlir::LLVM::LLVMPointerType::get(&context), lower_type(type), baseAddr,
                llvm::ArrayRef<mlir::LLVM::GEPArg>{
                    0, static_cast<int32_t>(checked_data->field_indices.at(member_access))});
        }

        auto baseAddr = gen_address(member_access->left.get());
        // If base is not addressable, check if it is a pointer
        if (!baseAddr) {
            auto val = gen_expression(member_access->left.get());
            if (val && llvm::isa<mlir::LLVM::LLVMPointerType>(val.getType())) {
                baseAddr = val;
            }
        }

        if (!baseAddr)
            return nullptr;

        auto baseExprType = get_expression_type(member_access->left.get());

        // Handle pointer to struct (implicit dereference)
        if (llvm::isa<mlir::LLVM::LLVMPointerType>(baseExprType)) {
            std::string sourceTypeName;
            if (auto *ident = dynamic_cast<const Identifier *>(member_access->left.get())) {
                auto sym = lookup_binding(ident);
                sourceTypeName = sym.source_type;
            }

            if (!sourceTypeName.empty() && sourceTypeName[0] == '*') {
                std::string structName = sourceTypeName.substr(1);
                if (auto *ident = dynamic_cast<const Identifier *>(member_access->member.get())) {
                    if (struct_field_indices.count(structName) &&
                        struct_field_indices[structName].count(ident->value)) {
                        int index = struct_field_indices[structName][ident->value];
                        auto structType = type_table[structName];
                        return builder.create<mlir::LLVM::GEPOp>(
                            location(), mlir::LLVM::LLVMPointerType::get(&context), structType,
                            baseAddr, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, index});
                    }
                }
            }
        }

        if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(baseExprType)) {

            auto ident = dynamic_cast<const Identifier *>(member_access->member.get());
            std::string structName = structType.getName().str();
            auto fields = struct_field_indices.find(structName);
            if (fields == struct_field_indices.end() || !fields->second.contains(ident->value))
                fail("Unknown struct field: " + ident->value);
            int index = fields->second.at(ident->value);

            return builder.create<mlir::LLVM::GEPOp>(
                location(), mlir::LLVM::LLVMPointerType::get(&context), structType, baseAddr,
                llvm::ArrayRef<mlir::LLVM::GEPArg>{0, index});
        }
    } else if (auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
        if (prefix->op == "*") {
            return checked_data ? gen_pointer_address(prefix->right.get())
                                : gen_expression(prefix->right.get());
        }
    }
    return nullptr;
}

mlir::Value CodeGen::gen_pointer_address(const Expression *expression) {
    auto value = gen_expression(expression);
    const auto type = checked_data->types.at(expression);
    if (!type.is_pointer())
        fail("Address requires a checked pointer type");
    if (type.pointers.front().nullable) {
        auto null = builder.create<mlir::LLVM::ZeroOp>(location(), value.getType());
        auto nonnull = builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne,
                                                          value, null);
        require_runtime(nonnull);
    }
    return value;
}

void CodeGen::handle_import(const std::string &import_path) {
    fail("Import lowering is not implemented: " + import_path);
}

void CodeGen::handle_std_import(const std::string &module_name) {
    fail("Standard module lowering is not implemented: " + module_name);
}

CodeGen::SymbolInfo CodeGen::lookup(const std::string &name) {
    if (checked_data)
        fail("Unchecked name lookup reached checked codegen");
    auto scope = current_scope;
    while (scope) {
        if (scope->values.count(name)) {
            return scope->values[name];
        }
        scope = scope->parent;
    }
    return {nullptr, false, nullptr};
}

mlir::Type CodeGen::resolve_type(const std::string &type_name) {
    if (checked_data)
        fail("Unchecked type resolution reached checked codegen");
    if (type_table.count(type_name)) {
        return type_table[type_name];
    }
    if (type_name == "i32")
        return builder.getI32Type();
    if (type_name == "i64")
        return builder.getI64Type();
    if (type_name == "f32")
        return builder.getF32Type();
    if (type_name == "bool")
        return builder.getI1Type();
    if (type_name == "void")
        return builder.getNoneType();

    // Handle pointer types (*T)
    if (!type_name.empty() && (type_name[0] == '*' || type_name[0] == '&')) {
        return mlir::LLVM::LLVMPointerType::get(&context);
    }

    // Handle legacy pointer types (T*) just in case
    if (!type_name.empty() && type_name.back() == '*') {
        return mlir::LLVM::LLVMPointerType::get(&context);
    }

    // Check for Generic Instantiation: Base<Arg1, Arg2>
    auto lt = type_name.find('<');
    if (lt != std::string::npos && type_name.back() == '>') {
        std::string base_name = type_name.substr(0, lt);
        std::string args_str = type_name.substr(lt + 1, type_name.length() - lt - 2);

        // Split args by comma (handling nested brackets crudely for now)
        std::vector<std::string> type_args;
        std::string current_arg;
        int bracket_depth = 0;
        for (char c : args_str) {
            if (c == '<')
                bracket_depth++;
            else if (c == '>')
                bracket_depth--;

            if (c == ',' && bracket_depth == 0) {
                // Trim leading space
                while (!current_arg.empty() && current_arg[0] == ' ')
                    current_arg.erase(0, 1);
                type_args.push_back(current_arg);
                current_arg.clear();
            } else {
                current_arg += c;
            }
        }
        if (!current_arg.empty()) {
            while (!current_arg.empty() && current_arg[0] == ' ')
                current_arg.erase(0, 1);
            type_args.push_back(current_arg);
        }

        if (base_name == "Deferred" && type_args.size() == 1) {
            mlir::Type valueType = resolve_type(type_args[0]);
            if (!valueType)
                fail("Unknown or unsupported type: " + type_name);
            return gloin::GloinDeferredType::get(&context, valueType);
        }

        if (base_name == "Spawn" && type_args.size() == 1) {
            // Treat Spawn<T> as a pointer (Task*) for now, similar to void*
            // Ideally we should use gloin::GloinSpawnType and lower it,
            // but since we lack the conversion pass, we alias it to avoid crashes in mutable vars.
            return mlir::LLVM::LLVMPointerType::get(&context);
        }

        // Instantiate
        if (generic_struct_defs.count(base_name)) {
            const auto *def = generic_struct_defs[base_name];

            if (def->generic_params.size() != type_args.size()) {
                fail(diagnostic_text("Generic arg count mismatch for ", base_name, "\n"));
                fail("Unknown or unsupported type: " + type_name);
            }

            // Create concrete struct type
            auto structType = mlir::LLVM::LLVMStructType::getIdentified(&context, type_name);
            type_table[type_name] = structType; // Register immediately to handle recursive refs?

            std::vector<mlir::Type> fieldTypes;
            int fieldIdx = 0;

            for (const auto &field : def->fields) {
                std::string fieldTypeName = field.type->value;

                // Substitute generic param with concrete arg
                for (size_t i = 0; i < def->generic_params.size(); ++i) {
                    if (fieldTypeName == def->generic_params[i]) {
                        fieldTypeName = type_args[i];
                        break;
                    }
                    // Handle pointers to generics: T* -> i32*
                    // Simplistic check:
                    if (fieldTypeName == def->generic_params[i] + "*") {
                        fieldTypeName = type_args[i] + "*";
                        break;
                    }
                }

                mlir::Type fieldType = resolve_type(fieldTypeName);
                if (!fieldType) {
                    fail(diagnostic_text("Failed to resolve field type ", fieldTypeName,
                                         " in generic instantiation\n"));
                    fail("Unknown or unsupported type: " + type_name);
                }

                fieldTypes.push_back(fieldType);
                struct_field_indices[type_name][field.name->value] = fieldIdx++;
                struct_field_types[type_name][field.name->value] = fieldType;
            }

            if (mlir::failed(structType.setBody(fieldTypes, def->is_packed))) {
                fail(diagnostic_text("Failed to set body for generic struct ", type_name, "\n"));
                fail("Unknown or unsupported type: " + type_name);
            }

            return structType;
        }
    }

    fail("Unknown or unsupported type: " + type_name);
}

CodeGen::~CodeGen() {
    if (theModule && !module_transferred)
        theModule.erase();
}

mlir::OwningOpRef<mlir::ModuleOp> CodeGen::generate(const CheckedProgram &program) {
    checked_data = &program.data;
    auto result = generate_impl(program.program);
    checked_data = nullptr;
    checked_values.clear();
    checked_functions.clear();
    return mlir::OwningOpRef<mlir::ModuleOp>(result);
}

mlir::ModuleOp
CodeGen::generate_unchecked_for_testing(const std::vector<std::unique_ptr<Statement>> &program) {
    return generate_impl(program);
}

mlir::Type CodeGen::lower_type(ValueType value_type) {
    if (value_type.is_pointer())
        return mlir::LLVM::LLVMPointerType::get(&context);
    if (value_type.structure) {
        const auto id = *value_type.structure;
        if (auto found = checked_struct_types.find(id); found != checked_struct_types.end())
            return found->second;
        const auto &structure = checked_data->structures.at(id);
        llvm::SmallVector<mlir::Type> fields;
        for (const auto &field : structure.fields)
            fields.push_back(lower_type(field.type));
        // Nominality belongs to Sema; literal backend types avoid context-global named-type
        // collisions.
        auto type = mlir::LLVM::LLVMStructType::getLiteral(&context, fields, false);
        checked_struct_types[id] = type;
        return type;
    }
    auto type = value_type.builtin();
    const auto &info = core_type_info(type);
    if (type == CoreType::Void)
        return builder.getNoneType();
    if (type == CoreType::F32)
        return builder.getF32Type();
    if (type == CoreType::F64)
        return builder.getF64Type();
    if (type == CoreType::String)
        return string_type();
    return builder.getIntegerType(info.bits);
}

mlir::Value CodeGen::create_entry_alloca(mlir::Type type) {
    auto *block = builder.getBlock();
    if (!block)
        fail("Cannot allocate a local without an active function");
    auto function = llvm::dyn_cast<mlir::func::FuncOp>(block->getParentOp());
    if (!function)
        fail("Cannot allocate a local outside a function");

    mlir::OpBuilder::InsertionGuard guard(builder);
    auto &entry = function.getBody().front();
    builder.setInsertionPointToStart(&entry);
    auto one = builder.create<mlir::LLVM::ConstantOp>(
        location(), builder.getI64Type(), builder.getI64IntegerAttr(1));
    return builder.create<mlir::LLVM::AllocaOp>(
        location(), mlir::LLVM::LLVMPointerType::get(&context), type, one, 0);
}

mlir::Type CodeGen::checked_type(const Node *node) {
    auto found = checked_data->types.find(node);
    if (found == checked_data->types.end())
        fail("Missing checked type");
    return lower_type(found->second);
}

SymbolId CodeGen::checked_binding(const Identifier *name) {
    auto found = checked_data->bindings.find(name);
    if (found == checked_data->bindings.end() || found->second >= checked_data->symbols.size())
        fail("Missing checked declaration binding");
    return found->second;
}

void CodeGen::declare_binding(const Identifier *name, mlir::Value value, bool address,
                              mlir::Type type, const std::string &source_type) {
    if (!checked_data) {
        declare(name->value, value, address, type, source_type);
        return;
    }
    auto id = checked_binding(name);
    if (lower_type(checked_data->symbols[id].type) != type || (!address && value.getType() != type))
        fail("Generated binding disagrees with its checked type");
    checked_values[id] = {value, address, type, source_type};
}

CodeGen::SymbolInfo CodeGen::lookup_binding(const Identifier *name) {
    if (!checked_data)
        return lookup(name->value);
    auto found = checked_values.find(checked_binding(name));
    if (found == checked_values.end())
        fail("Checked binding has no generated value");
    return found->second;
}

mlir::Value CodeGen::emit_constant(const ConstantValue &constant) {
    if (auto text = std::get_if<std::string>(&constant.value)) {
        auto stringStructType = string_type();
        auto found = string_globals.find(*text);
        mlir::LLVM::GlobalOp globalStr;
        auto strType = mlir::LLVM::LLVMArrayType::get(builder.getI8Type(), text->size() + 1);
        if (found == string_globals.end()) {
            std::string bytes = *text;
            bytes.push_back('\0');
            std::string globalName;
            do {
                globalName = "str_" + std::to_string(next_string_global++);
            } while (theModule.lookupSymbol(globalName));
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(theModule.getBody());
            globalStr = builder.create<mlir::LLVM::GlobalOp>(
                location(), strType, true, mlir::LLVM::Linkage::Internal, globalName,
                builder.getStringAttr(llvm::StringRef(bytes.data(), bytes.size())));
            string_globals.emplace(*text, globalStr);
        } else {
            globalStr = found->second;
        }
        auto globalPtr = builder.create<mlir::LLVM::AddressOfOp>(location(), globalStr);
        auto undef = builder.create<mlir::LLVM::UndefOp>(location(), stringStructType);
        auto zero = builder.create<mlir::LLVM::ConstantOp>(location(), builder.getI64Type(), builder.getI64IntegerAttr(0));
        auto gep = builder.create<mlir::LLVM::GEPOp>(location(), mlir::LLVM::LLVMPointerType::get(&context), strType, globalPtr, mlir::ValueRange{zero, zero});
        auto ptrValue = builder.create<mlir::LLVM::InsertValueOp>(location(), undef, gep, llvm::ArrayRef<int64_t>{0});
        auto lenValue = builder.create<mlir::LLVM::ConstantOp>(location(), builder.getI64Type(), builder.getI64IntegerAttr(text->size()));
        return builder.create<mlir::LLVM::InsertValueOp>(location(), ptrValue, lenValue, llvm::ArrayRef<int64_t>{1});
    }
    auto type = lower_type(constant.type);
    if (auto integer = std::get_if<llvm::APInt>(&constant.value))
        return builder.create<mlir::arith::ConstantOp>(location(), type,
                                                       builder.getIntegerAttr(type, *integer));
    if (auto boolean = std::get_if<bool>(&constant.value))
        return builder.create<mlir::arith::ConstantIntOp>(location(), *boolean ? 1 : 0, 1);
    return builder.create<mlir::arith::ConstantOp>(
        location(), type, builder.getFloatAttr(type, std::get<llvm::APFloat>(constant.value)));
}
