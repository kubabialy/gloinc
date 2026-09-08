#include "codegen.h"
#include <iostream>

// using namespace mlir; // Removed to avoid conflict with gloin::Type
// using namespace gloin;

CodeGen::CodeGen(mlir::MLIRContext &ctx) : context(ctx), builder(&ctx) {

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

    // Initialize String struct: struct String { ptr: *u8, len: i64 }
    auto u8PtrType = mlir::LLVM::LLVMPointerType::get(&context);
    auto lenType = builder.getI64Type();
    auto stringType = mlir::LLVM::LLVMStructType::getIdentified(&context, "String");
    if (mlir::succeeded(stringType.setBody({u8PtrType, lenType}, /*isPacked=*/false))) {
        type_table["string"] = stringType;
        type_table["String"] = stringType;

        struct_field_indices["String"]["ptr"] = 0;
        struct_field_indices["String"]["len"] = 1;
        struct_field_types["String"]["ptr"] = u8PtrType;
        struct_field_types["String"]["len"] = lenType;
    }

    // Initialize Arena struct: struct Arena { ptr: *u8, end: *u8, blocks: *u8 }
    // ptr: Current bump pointer
    // end: End of current block
    // blocks: Pointer to current block (linked list head)

    auto arenaType = mlir::LLVM::LLVMStructType::getIdentified(&context, "Arena");
    if (mlir::succeeded(arenaType.setBody({u8PtrType, u8PtrType, u8PtrType}, /*isPacked=*/false))) {
        type_table["Arena"] = arenaType;
    }

    create_runtime_functions();
}

int64_t CodeGen::get_type_size(mlir::Type type) {
    if (type.isInteger(1) || type.isInteger(8))
        return 1;
    if (type.isInteger(16))
        return 2;
    if (type.isInteger(32))
        return 4;
    if (type.isInteger(64))
        return 8;
    if (llvm::isa<mlir::LLVM::LLVMPointerType>(type))
        return 8;
    if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(type)) {
        int64_t size = 0;
        for (auto t : structType.getBody()) {
            size += get_type_size(t);
        }
        return size;
    }
    return 8; // Default
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

    // Arena Runtime Functions
    auto arenaInitType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_arena_init",
                                           arenaInitType);

    auto arenaAllocType = mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType, i64Type}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_arena_alloc",
                                           arenaAllocType);

    auto arenaResetType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_arena_reset",
                                           arenaResetType);

    auto arenaDestroyType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType}, false);
    builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "gloin_arena_destroy",
                                           arenaDestroyType);

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

void CodeGen::emit_deferred() {
    for (auto it = current_function_defers.rbegin(); it != current_function_defers.rend(); ++it) {
        const auto *stmt = *it;
        if (stmt->call) {
            gen_expression(stmt->call.get());
        }
    }
}

void CodeGen::declare(const std::string &name, mlir::Value value, bool is_address, mlir::Type type,
                      const std::string &source_type) {
    SymbolInfo info = {value, is_address, type, source_type};
    current_scope->values[name] = info;
}

mlir::ModuleOp CodeGen::generate(const std::vector<std::unique_ptr<Statement>> &program) {
    // Set insertion point to end of module body
    if (theModule.getBody()) {
        builder.setInsertionPointToEnd(theModule.getBody());
    } else {
        // Should exist, but ensure
        mlir::Block *block = new mlir::Block();
        theModule.getBodyRegion().push_back(block);
        builder.setInsertionPointToEnd(block);
    }

    for (const auto &stmt : program) {
        if (auto *func_def = dynamic_cast<const FunctionDefinition *>(stmt.get())) {
            gen_statement(stmt.get());
        } else if (auto *struct_def = dynamic_cast<const StructDefinition *>(stmt.get())) {
            gen_statement(stmt.get());
        } else if (auto *import_stmt = dynamic_cast<const ImportStatement *>(stmt.get())) {
            gen_statement(stmt.get());
        }
    }
    return theModule;
}

void CodeGen::gen_statement(const Statement *stmt) {
    if (!stmt)
        return;

    if (auto *func_def = dynamic_cast<const FunctionDefinition *>(stmt)) {
        current_function_defers.clear();
        std::vector<mlir::Type> argTypes;
        std::vector<std::string> argNames;
        for (const auto &param : func_def->parameters) {
            argTypes.push_back(resolve_type(param.type->value));
            argNames.push_back(param.name->value);
        }

        mlir::Type retType = builder.getNoneType();
        if (func_def->return_type) {
            retType = resolve_type(func_def->return_type->value);
        }

        std::vector<mlir::Type> resultTypes;
        if (!llvm::isa<mlir::NoneType>(retType)) {
            resultTypes.push_back(retType);
        }

        auto funcType = builder.getFunctionType(argTypes, resultTypes);
        auto funcOp = builder.create<mlir::func::FuncOp>(builder.getUnknownLoc(),
                                                         func_def->name->value, funcType);

        // Register function in table
        function_table[func_def->name->value] = {funcOp, func_def};

        if (!func_def->body)
            return;

        auto *entryBlock = funcOp.addEntryBlock();
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(entryBlock);

        enter_scope();

        for (size_t i = 0; i < argNames.size(); ++i) {
            auto argVal = entryBlock->getArgument(i);
            declare(argNames[i], argVal, false, argTypes[i], func_def->parameters[i].type->value);
        }

        gen_statement(func_def->body.get());

        // Check if the current block needs a terminator.
        mlir::Block *currentBlock = builder.getBlock();
        if (currentBlock) {
            bool isEmpty = currentBlock->empty();
            bool hasNoPreds = currentBlock->hasNoPredecessors();

            // If the block is empty and unreachable (no predecessors), remove it.
            // Exception: The entry block might have no predecessors but shouldn't be removed if
            // empty? Actually, entry block is created by addEntryBlock() and is the start.
            if (isEmpty && hasNoPreds && currentBlock != entryBlock) {
                currentBlock->erase();
            } else if (!isEmpty && !currentBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                // Reachable (or entry) but unterminated.
                if (funcOp.getFunctionType().getNumResults() == 0) {
                    builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
                } else {
                    builder.create<mlir::LLVM::UnreachableOp>(builder.getUnknownLoc());
                }
            } else if (isEmpty) {
                // Empty but reachable (e.g. fallthrough from previous block or entry block of empty
                // function)
                if (funcOp.getFunctionType().getNumResults() == 0) {
                    builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
                } else {
                    builder.create<mlir::LLVM::UnreachableOp>(builder.getUnknownLoc());
                }
            }
        }

        leave_scope();

    } else if (auto *struct_def = dynamic_cast<const StructDefinition *>(stmt)) {
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
                    fieldType = builder.getI32Type();
                }
            }
            elementTypes.push_back(fieldType);

            struct_field_indices[name][fieldName] = index++;
            struct_field_types[name][fieldName] = fieldType;
        }

        if (mlir::failed(structType.setBody(elementTypes, struct_def->is_packed))) {
            std::cerr << "Failed to set body for struct " << name << std::endl;
        }

        // Method Generation
        for (const auto &method : struct_def->methods) {
            std::string methodName = method->name->value;
            std::string mangledName = name + "_" + methodName;

            std::vector<mlir::Type> methodArgTypes;
            // Implicit 'self' parameter (pointer to struct instance)
            methodArgTypes.push_back(mlir::LLVM::LLVMPointerType::get(&context));

            std::vector<std::string> argNames;
            argNames.push_back("self");

            for (const auto &param : method->parameters) {
                methodArgTypes.push_back(resolve_type(param.type->value));
                argNames.push_back(param.name->value);
            }

            mlir::Type retType = builder.getNoneType();
            if (method->return_type) {
                retType = resolve_type(method->return_type->value);
            }

            std::vector<mlir::Type> resultTypes;
            if (!llvm::isa<mlir::NoneType>(retType)) {
                resultTypes.push_back(retType);
            }

            auto funcType = builder.getFunctionType(methodArgTypes, resultTypes);
            auto funcOp =
                builder.create<mlir::func::FuncOp>(builder.getUnknownLoc(), mangledName, funcType);

            // Register method in table (key: StructName_MethodName)
            // Note: AST nodes are unique_ptr, so we store the raw pointer.
            // Be careful about lifetime if AST is destroyed before CodeGen finishes (usually not
            // the case).
            function_table[mangledName] = {funcOp, method.get()};

            if (!method->body)
                continue;

            auto *entryBlock = funcOp.addEntryBlock();
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(entryBlock);

            enter_scope();
            current_function_defers.clear(); // Clear defers for new function scope

            // Declare arguments
            // self is at index 0
            auto selfVal = entryBlock->getArgument(0);
            // declare 'self' as a pointer to the struct
            // It is an address (pointer), pointing to 'structType'.
            declare("self", selfVal, true, structType, name + "*");

            for (size_t i = 1; i < argNames.size(); ++i) {
                auto argVal = entryBlock->getArgument(i);
                declare(argNames[i], argVal, false, methodArgTypes[i],
                        method->parameters[i - 1].type->value);
            }

            gen_statement(method->body.get());

            if (entryBlock->empty() ||
                !entryBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
            }

            leave_scope();
        }

    } else if (auto *var_decl = dynamic_cast<const VariableDeclaration *>(stmt)) {
        std::string name = var_decl->name->value;
        mlir::Type type = builder.getI32Type();
        if (var_decl->type) {
            type = resolve_type(var_decl->type->value);
        }

        mlir::Value initVal;
        if (var_decl->initializer) {
            initVal = gen_expression(var_decl->initializer.get());
        }

        if (var_decl->is_mutable) {
            auto one = builder.create<mlir::LLVM::ConstantOp>(
                builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(1));
            auto alloca = builder.create<mlir::LLVM::AllocaOp>(
                builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context), type, one, 0);
            if (initVal) {
                builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), initVal, alloca);
            }
            declare(name, alloca, true, type, var_decl->type ? var_decl->type->value : "");
        } else {
            if (initVal) {
                declare(name, initVal, false, type, var_decl->type ? var_decl->type->value : "");
            } else {
                // Immutable var without initializer? Error or undefined?
                // For now, ignore or create undefined?
            }
        }

    } else if (auto *return_stmt = dynamic_cast<const ReturnStatement *>(stmt)) {
        if (return_stmt->return_value) {
            auto val = gen_expression(return_stmt->return_value.get());
            emit_deferred();
            builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc(), val);
        } else {
            emit_deferred();
            builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
        }

    } else if (auto *block = dynamic_cast<const BlockStatement *>(stmt)) {
        enter_scope();
        for (const auto &s : block->statements) {
            gen_statement(s.get());
        }
        leave_scope();

    } else if (auto *if_stmt = dynamic_cast<const IfStatement *>(stmt)) {
        // Implement IfStatement using unstructured control flow (cf.cond_br)
        // to support early returns inside branches.

        auto *funcOp = builder.getBlock()->getParent()->getParentOp();
        auto *region = builder.getBlock()->getParent();

        auto *thenBlock = new mlir::Block();
        auto *elseBlock = new mlir::Block(); // Use even if empty for simplicity
        auto *mergeBlock = new mlir::Block();

        region->getBlocks().insertAfter(builder.getBlock()->getIterator(), thenBlock);
        region->getBlocks().insertAfter(thenBlock->getIterator(), elseBlock);
        region->getBlocks().insertAfter(elseBlock->getIterator(), mergeBlock);

        auto cond = gen_expression(if_stmt->condition.get());
        if (!cond)
            return; // Error

        builder.create<mlir::cf::CondBranchOp>(builder.getUnknownLoc(), cond, thenBlock,
                                               mlir::ValueRange{}, elseBlock, mlir::ValueRange{});

        // Then Block
        builder.setInsertionPointToStart(thenBlock);
        gen_statement(if_stmt->consequence.get());
        // If not terminated (e.g. by return), branch to merge
        if (thenBlock->empty() || !thenBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), mergeBlock);
        }

        // Else Block
        builder.setInsertionPointToStart(elseBlock);
        if (if_stmt->alternative) {
            gen_statement(if_stmt->alternative.get());
        }
        if (elseBlock->empty() || !elseBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), mergeBlock);
        }

        // Continue after merge
        builder.setInsertionPointToStart(mergeBlock);

    } else if (auto *while_stmt = dynamic_cast<const WhileStatement *>(stmt)) {
        // Implement WhileStatement using unstructured control flow (cf.cond_br)

        auto *region = builder.getBlock()->getParent();

        auto *condBlock = new mlir::Block();
        auto *bodyBlock = new mlir::Block();
        auto *endBlock = new mlir::Block();

        region->getBlocks().insertAfter(builder.getBlock()->getIterator(), condBlock);
        region->getBlocks().insertAfter(condBlock->getIterator(), bodyBlock);
        region->getBlocks().insertAfter(bodyBlock->getIterator(), endBlock);

        builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);

        // Condition Block
        builder.setInsertionPointToStart(condBlock);
        auto cond = gen_expression(while_stmt->condition.get());
        builder.create<mlir::cf::CondBranchOp>(builder.getUnknownLoc(), cond, bodyBlock,
                                               mlir::ValueRange{}, endBlock, mlir::ValueRange{});

        // Body Block
        builder.setInsertionPointToStart(bodyBlock);
        gen_statement(while_stmt->body.get());
        // If not terminated, branch back to condition
        if (bodyBlock->empty() || !bodyBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);
        }

        // End Block
        builder.setInsertionPointToStart(endBlock);

    } else if (auto *expr_stmt = dynamic_cast<const ExpressionStatement *>(stmt)) {
        gen_expression(expr_stmt->expression.get());

    } else if (auto *import_stmt = dynamic_cast<const ImportStatement *>(stmt)) {
        handle_import(import_stmt->path);
    } else if (auto *defer_stmt = dynamic_cast<const DeferStatement *>(stmt)) {
        current_function_defers.push_back(defer_stmt);
    }
}

mlir::Value CodeGen::gen_expression(const Expression *expr) {
    if (auto *int_lit = dynamic_cast<const IntegerLiteral *>(expr)) {
        return builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), int_lit->value,
                                                          32);
    } else if (auto *float_lit = dynamic_cast<const FloatLiteral *>(expr)) {
        auto floatType = builder.getF32Type();
        auto floatAttr = builder.getFloatAttr(floatType, float_lit->value);
        return builder.create<mlir::arith::ConstantOp>(builder.getUnknownLoc(), floatType,
                                                       floatAttr);
    } else if (auto *bool_lit = dynamic_cast<const BooleanLiteral *>(expr)) {
        return builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(),
                                                          bool_lit->value ? 1 : 0, 1);
    } else if (auto *str_lit = dynamic_cast<const StringLiteral *>(expr)) {
        std::string strVal = str_lit->value;
        auto strType = mlir::LLVM::LLVMArrayType::get(builder.getI8Type(), strVal.size() + 1);

        std::string globalName = "str_" + std::to_string(std::hash<std::string>{}(strVal));

        mlir::LLVM::GlobalOp globalStr;
        {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(theModule.getBody());
            globalStr = builder.create<mlir::LLVM::GlobalOp>(
                builder.getUnknownLoc(), strType,
                /*isConstant=*/true, mlir::LLVM::Linkage::Internal, globalName,
                builder.getStringAttr(strVal + "\0"));
        }

        auto globalPtr =
            builder.create<mlir::LLVM::AddressOfOp>(builder.getUnknownLoc(), globalStr);

        auto stringStructType = type_table["String"];
        auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), stringStructType);

        auto zero = builder.create<mlir::LLVM::ConstantOp>(
            builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(0));
        auto gep = builder.create<mlir::LLVM::GEPOp>(
            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context), strType, globalPtr,
            mlir::ValueRange{zero, zero});

        auto tmp1 = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), undef, gep,
                                                              llvm::ArrayRef<int64_t>{0});

        auto lenVal =
            builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(),
                                                   builder.getI64IntegerAttr(strVal.size()));
        auto res = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), tmp1, lenVal,
                                                             llvm::ArrayRef<int64_t>{1});

        return res;
    } else if (auto *array_lit = dynamic_cast<const ArrayLiteral *>(expr)) {
        if (array_lit->elements.empty()) {
            // Handle empty array? Maybe array<0 x i8>?
            // For now assume non-empty or handle via type inference if possible.
            return nullptr;
        }

        auto firstElem = gen_expression(array_lit->elements[0].get());
        if (!firstElem)
            return nullptr;

        mlir::Type elemType = firstElem.getType();
        auto arrayType = mlir::LLVM::LLVMArrayType::get(elemType, array_lit->elements.size());

        mlir::Value currentArray =
            builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), arrayType);

        // Insert first element
        currentArray = builder.create<mlir::LLVM::InsertValueOp>(
            builder.getUnknownLoc(), currentArray, firstElem, llvm::ArrayRef<int64_t>{0});

        for (size_t i = 1; i < array_lit->elements.size(); ++i) {
            auto elem = gen_expression(array_lit->elements[i].get());
            if (!elem)
                return nullptr;

            // Simple type check/cast
            if (elem.getType() != elemType) {
                // Cast if compatible?
            }

            currentArray = builder.create<mlir::LLVM::InsertValueOp>(
                builder.getUnknownLoc(), currentArray, elem, llvm::ArrayRef<int64_t>{(int64_t)i});
        }

        return currentArray;
    } else if (auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
        if (prefix->op == "*") {
            auto ptr = gen_expression(prefix->right.get());
            if (!ptr)
                return nullptr;
            return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), builder.getI32Type(),
                                                      ptr);
        } else if (prefix->op == "&") {
            return gen_address(prefix->right.get());
        }
    } else if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        auto sym = lookup(ident->value);
        if (sym.value) {
            if (sym.is_address) {
                if (llvm::isa<mlir::LLVM::LLVMPointerType>(sym.value.getType())) {
                    return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), sym.type,
                                                              sym.value);
                }
                return builder.create<mlir::memref::LoadOp>(builder.getUnknownLoc(), sym.value);
            } else {
                return sym.value;
            }
        }
        return nullptr;
    } else if (auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
        // Try to get address of the member if possible (e.g. if base is addressable)
        auto addr = gen_address(expr);
        if (addr) {
            auto type = get_expression_type(expr);
            return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), type, addr);
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
                        builder.getUnknownLoc(), baseVal, llvm::ArrayRef<int64_t>{index});
                }
            }
        }

        return nullptr;
    } else if (auto *bin = dynamic_cast<const InfixExpression *>(expr)) {
        auto left = gen_expression(bin->left.get());
        auto right = gen_expression(bin->right.get());

        if (!left || !right)
            return nullptr;

        // Simple type checking/promotion for binary ops
        if (left.getType() != right.getType()) {
            if (left.getType().isInteger(32) && right.getType().isInteger(64)) {
                left = builder.create<mlir::arith::ExtSIOp>(builder.getUnknownLoc(),
                                                            builder.getI64Type(), left);
            } else if (left.getType().isInteger(64) && right.getType().isInteger(32)) {
                right = builder.create<mlir::arith::ExtSIOp>(builder.getUnknownLoc(),
                                                             builder.getI64Type(), right);
            }
            // Add more cases as needed (float, etc)
        }

        if (bin->op == "+")
            return builder.create<mlir::arith::AddIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "-")
            return builder.create<mlir::arith::SubIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "*")
            return builder.create<mlir::arith::MulIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "/")
            return builder.create<mlir::arith::DivSIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "==")
            return builder.create<mlir::arith::CmpIOp>(builder.getUnknownLoc(),
                                                       mlir::arith::CmpIPredicate::eq, left, right);
        if (bin->op == "<")
            return builder.create<mlir::arith::CmpIOp>(
                builder.getUnknownLoc(), mlir::arith::CmpIPredicate::slt, left, right);
        if (bin->op == ">")
            return builder.create<mlir::arith::CmpIOp>(
                builder.getUnknownLoc(), mlir::arith::CmpIPredicate::sgt, left, right);
    } else if (auto *assign = dynamic_cast<const AssignmentExpression *>(expr)) {
        auto right = gen_expression(assign->right.get());
        auto lhsAddr = gen_address(assign->left.get());
        if (lhsAddr) {
            if (llvm::isa<mlir::LLVM::LLVMPointerType>(lhsAddr.getType())) {
                builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), right, lhsAddr);
            } else {
                builder.create<mlir::memref::StoreOp>(builder.getUnknownLoc(), right, lhsAddr);
            }
            return right;
        }
    } else if (auto *struct_lit = dynamic_cast<const StructLiteral *>(expr)) {
        std::string structName = struct_lit->name->value;
        if (!type_table.count(structName))
            return nullptr;

        mlir::Type type = type_table[structName];
        auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), type);
        mlir::Value current = undef;

        for (const auto &field : struct_lit->fields) {
            std::string fieldName = field.first;
            auto val = gen_expression(field.second.get());
            if (!val)
                return nullptr;

            int index = struct_field_indices[structName][fieldName];
            current = builder.create<mlir::LLVM::InsertValueOp>(
                builder.getUnknownLoc(), current, val, llvm::ArrayRef<int64_t>{index});
        }
        return current;
    } else if (auto *call = dynamic_cast<const CallExpression *>(expr)) {
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
                            builder.getUnknownLoc(), builder.getI64Type(),
                            builder.getI64IntegerAttr(1));
                        auto alloca = builder.create<mlir::LLVM::AllocaOp>(
                            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                            val.getType(), one, 0);
                        builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), val, alloca);
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
                std::cerr << "Indirect calls not supported yet\n";
                return nullptr;
            }
        }

        std::vector<mlir::Value> args;
        if (selfArg) {
            args.push_back(selfArg);
        }

        for (const auto &arg : call->arguments) {
            auto val = gen_expression(arg.get());
            if (!val)
                return nullptr;
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
                    builder.getUnknownLoc(), deferredType,
                    mlir::FlatSymbolRefAttr::get(&context, funcName), args);
                return callOp.getHandle();
            } else {
                auto callOp =
                    builder.create<mlir::func::CallOp>(builder.getUnknownLoc(), funcOp, args);
                if (callOp.getNumResults() > 0)
                    return callOp.getResult(0);
                return nullptr;
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
                                args[i] = builder.create<mlir::LLVM::SExtOp>(
                                    builder.getUnknownLoc(), expected, args[i]);
                            }
                        }
                    }
                }

                auto callOp =
                    builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), funcOp, args);
                if (callOp.getNumResults() > 0)
                    return callOp.getResult();
                return nullptr;
            }

            auto callOp = builder.create<mlir::func::CallOp>(
                builder.getUnknownLoc(), mlir::FlatSymbolRefAttr::get(&context, funcName),
                mlir::TypeRange{}, args);
            if (callOp.getNumResults() > 0)
                return callOp.getResult(0);
            return nullptr;
        }
    } else if (auto *spawn = dynamic_cast<const SpawnExpression *>(expr)) {
        if (auto *call = dynamic_cast<const CallExpression *>(spawn->call.get())) {
            std::string funcName;
            if (auto *id = dynamic_cast<const Identifier *>(call->function.get())) {
                funcName = id->value;
            } else {
                return nullptr;
            }

            // Simplification: Assume single pointer argument for now
            // or cast the single argument to void*
            // This is a temporary limitation to get the runtime working.

            mlir::Value argVal;
            if (call->arguments.empty()) {
                // Pass null
                argVal = builder.create<mlir::LLVM::ZeroOp>(
                    builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context));
            } else if (call->arguments.size() == 1) {
                argVal = gen_expression(call->arguments[0].get());
                if (!argVal)
                    return nullptr;

                // Cast to void* if needed
                if (!llvm::isa<mlir::LLVM::LLVMPointerType>(argVal.getType())) {
                    // Try to inttoptr if integer
                    if (argVal.getType().isInteger(64)) {
                        argVal = builder.create<mlir::LLVM::IntToPtrOp>(
                            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                            argVal);
                    } else if (argVal.getType().isInteger(32)) {
                        auto ext = builder.create<mlir::LLVM::ZExtOp>(builder.getUnknownLoc(),
                                                                      builder.getI64Type(), argVal);
                        argVal = builder.create<mlir::LLVM::IntToPtrOp>(
                            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                            mlir::ValueRange{ext.getResult()});
                    } else {
                        // Fallback: bitcast or error
                        // For now, assume pointer compatible
                        argVal = builder.create<mlir::LLVM::BitcastOp>(
                            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                            argVal);
                    }
                } else {
                    argVal = builder.create<mlir::LLVM::BitcastOp>(
                        builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                        argVal);
                }
            } else {
                std::cerr << "Spawn only supports 0 or 1 argument currently\n";
                return nullptr;
            }

            // Get function pointer
            auto funcOp = theModule.lookupSymbol<mlir::func::FuncOp>(funcName);
            if (!funcOp) {
                // Try LLVM func
                auto llvmFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(funcName);
                if (!llvmFunc) {
                    std::cerr << "Function " << funcName << " not found for spawn\n";
                    return nullptr;
                }
                // We need a pointer to this function.
                // AddressOfOp works on GlobalOp or FuncOp if we use LLVM dialect.
                // But mlir::func::FuncOp is not directly addressable as an SSA value in standard
                // MLIR unless we convert to LLVM dialect first or use ConstantOp with function
                // type?

                // Actually, we need to look up the LLVM function declaration.
                // If it's a standard FuncOp, we might need a wrapper or ensure it's converted.
                // For now, let's assume we can get the address.

                auto funcPtr =
                    builder.create<mlir::LLVM::AddressOfOp>(builder.getUnknownLoc(), llvmFunc);

                auto voidFuncPtr = builder.create<mlir::LLVM::BitcastOp>(
                    builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context), funcPtr);

                auto spawnFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_spawn_task");
                auto callOp = builder.create<mlir::LLVM::CallOp>(
                    builder.getUnknownLoc(), spawnFunc, mlir::ValueRange{voidFuncPtr, argVal});

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
                builder.getUnknownLoc(), funcOp.getFunctionType(),
                mlir::FlatSymbolRefAttr::get(&context, funcName));

            // This returns a value of function type. We need to cast this to void*.
            // We use UnrealizedConversionCastOp to bridge the gap until lowering.
            auto cast = builder.create<mlir::UnrealizedConversionCastOp>(
                builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                funcConst.getResult());

            auto spawnFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_spawn_task");

            // gloin_spawn_task expects (void* func, void* arg)
            auto callOp = builder.create<mlir::LLVM::CallOp>(
                builder.getUnknownLoc(), spawnFunc, mlir::ValueRange{cast.getResult(0), argVal});

            return callOp.getResult();
        }
    } else if (auto *awaitExpr = dynamic_cast<const AwaitExpression *>(expr)) {
        auto handle = gen_expression(awaitExpr->expr.get());
        if (!handle)
            return nullptr;

        auto awaitFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("gloin_await_task");
        auto callOp = builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), awaitFunc,
                                                         mlir::ValueRange{handle});

        auto resPtr = callOp.getResult();

        // Result is void*. Cast back to expected type.
        // We assume the result is an integer (pointer-sized or smaller) for now.
        // If it's a pointer, we can just bitcast.
        // If it's an integer, we cast ptr -> int -> trunc.

        auto intType = builder.getI64Type(); // void* is 64-bit usually
        auto ptrToInt =
            builder.create<mlir::LLVM::PtrToIntOp>(builder.getUnknownLoc(), intType, resPtr);

        // Truncate to i32 if needed (assuming i32 return for test case)
        auto i32Type = builder.getI32Type();
        auto trunc =
            builder.create<mlir::LLVM::TruncOp>(builder.getUnknownLoc(), i32Type, ptrToInt);

        return trunc;
    }

    return nullptr;
}

mlir::Type CodeGen::get_expression_type(const Expression *expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        return lookup(ident->value).type;
    }
    if (dynamic_cast<const IntegerLiteral *>(expr))
        return builder.getI32Type();
    if (dynamic_cast<const BooleanLiteral *>(expr))
        return builder.getI1Type();
    if (dynamic_cast<const FloatLiteral *>(expr))
        return builder.getF32Type();

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

    // ... more types
    return builder.getI32Type();
}

mlir::Value CodeGen::gen_address(const Expression *expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr)) {
        auto sym = lookup(ident->value);
        if (sym.value && sym.is_address)
            return sym.value;
    } else if (auto *member_access = dynamic_cast<const MemberAccessExpression *>(expr)) {
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
                auto sym = lookup(ident->value);
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
                            builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context),
                            structType, baseAddr, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, index});
                    }
                }
            }
        }

        if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(baseExprType)) {

            auto ident = dynamic_cast<const Identifier *>(member_access->member.get());
            std::string structName = structType.getName().str();
            int index = struct_field_indices[structName][ident->value];

            return builder.create<mlir::LLVM::GEPOp>(
                builder.getUnknownLoc(), mlir::LLVM::LLVMPointerType::get(&context), structType,
                baseAddr, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, index});
        }
    } else if (auto *prefix = dynamic_cast<const PrefixExpression *>(expr)) {
        if (prefix->op == "*") {
            return gen_expression(prefix->right.get());
        }
    }
    return nullptr;
}

void CodeGen::handle_import(const std::string &import_path) {
    if (import_path.find("@std/") == 0) {
        handle_std_import(import_path.substr(5));
    }
}

void CodeGen::handle_std_import(const std::string &module_name) {
    // stub
}

CodeGen::SymbolInfo CodeGen::lookup(const std::string &name) {
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
    if (!type_name.empty() && type_name[0] == '*') {
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
                return nullptr;
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
                std::cerr << "Generic arg count mismatch for " << base_name << "\n";
                return nullptr;
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
                    std::cerr << "Failed to resolve field type " << fieldTypeName
                              << " in generic instantiation\n";
                    return nullptr;
                }

                fieldTypes.push_back(fieldType);
                struct_field_indices[type_name][field.name->value] = fieldIdx++;
                struct_field_types[type_name][field.name->value] = fieldType;
            }

            if (mlir::failed(structType.setBody(fieldTypes, def->is_packed))) {
                std::cerr << "Failed to set body for generic struct " << type_name << "\n";
                return nullptr;
            }

            return structType;
        }
    }

    return nullptr;
}
