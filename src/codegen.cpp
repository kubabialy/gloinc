#include "codegen.h"
#include <iostream>

// using namespace mlir; // Removed to avoid conflict with gloin::Type
// using namespace gloin;

CodeGen::CodeGen(mlir::MLIRContext& ctx) : context(ctx), builder(&ctx) {

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
    if (type.isInteger(1) || type.isInteger(8)) return 1;
    if (type.isInteger(16)) return 2;
    if (type.isInteger(32)) return 4;
    if (type.isInteger(64)) return 8;
    if (llvm::isa<mlir::LLVM::LLVMPointerType>(type)) return 8;
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


    
    // __gloin_arena_alloc(Arena*, i64) -> i8*
#if 0
    // if (!theModule.lookupSymbol("__gloin_arena_alloc")) {
        auto arenaPtrType = ptrType; // Arena* passed as ptr
        auto funcType = mlir::LLVM::LLVMFunctionType::get(ptrType, {arenaPtrType, i64Type}, false);
        auto func = builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "__gloin_arena_alloc", funcType);
        
        auto* entry = func.addEntryBlock(builder);
        mlir::OpBuilder::InsertionGuard funcGuard(builder);
        builder.setInsertionPointToStart(entry);
        
        auto arena = entry->getArgument(0);
        auto size = entry->getArgument(1);
        
        // Load current, end
        auto zero = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(0));
        auto one = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(1));
        auto two = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(2));

        auto structType = type_table["Arena"];
        
        auto ptrAddr = builder.create<mlir::LLVM::GEPOp>(builder.getUnknownLoc(), ptrType, structType, arena, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, 0});
        auto endAddr = builder.create<mlir::LLVM::GEPOp>(builder.getUnknownLoc(), ptrType, structType, arena, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, 1});
        
        auto currentPtr = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), ptrType, ptrAddr);
        auto endPtr = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), ptrType, endAddr);
        
        auto currentInt = builder.create<mlir::LLVM::PtrToIntOp>(builder.getUnknownLoc(), i64Type, currentPtr);
        auto endInt = builder.create<mlir::LLVM::PtrToIntOp>(builder.getUnknownLoc(), i64Type, endPtr);
        
        auto nextInt = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), currentInt, size);
        
        auto cond = builder.create<mlir::LLVM::ICmpOp>(builder.getUnknownLoc(), mlir::LLVM::ICmpPredicate::ugt, nextInt.getResult(), endInt);
        
        auto* growBlock = func.addBlock();
        auto* fastBlock = func.addBlock();
        
        builder.create<mlir::LLVM::CondBrOp>(builder.getUnknownLoc(), cond, growBlock, fastBlock);
        
        // Grow Block
        builder.setInsertionPointToStart(growBlock);
        {
            auto blockSize = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(4096));
            // Ensure size fits
            auto overhead = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(8)); // Header size
            auto reqSize = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), size, overhead);
            
            auto useSize = builder.create<mlir::LLVM::UMaxOp>(builder.getUnknownLoc(), blockSize, reqSize.getResult());
            
            // auto mallocFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
            auto call = builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), mlir::TypeRange{ptrType}, mlir::SymbolRefAttr::get(&context, "malloc"), mlir::ValueRange{useSize.getResult()});
            auto newBlock = call.getResult();
            
            // Link old block
            auto blocksAddr = builder.create<mlir::LLVM::GEPOp>(builder.getUnknownLoc(), ptrType, structType, arena, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, 2});
            auto oldBlocks = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), ptrType, blocksAddr);
            
            // newBlock points to oldBlocks
            // Assuming newBlock is at least 8 bytes, store pointer at start
            auto newBlockPtr = builder.create<mlir::LLVM::BitcastOp>(builder.getUnknownLoc(), ptrType, newBlock); // Cast to ptr* if needed, but ptr is opaque
            builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), oldBlocks, newBlockPtr);
            
            // Update arena.blocks
            builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), newBlock, blocksAddr);
            
            // Update arena.ptr = newBlock + 8
            auto newPtrInt = builder.create<mlir::LLVM::PtrToIntOp>(builder.getUnknownLoc(), i64Type, newBlock);
            auto startOffset = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), newPtrInt, overhead);
            auto newCurrentPtr = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, startOffset.getResult());
            
            // Update arena.end = newBlock + useSize
            auto newEndInt = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), newPtrInt, useSize.getResult());
            auto newEndPtr = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, newEndInt.getResult());
            
            builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), newEndPtr, endAddr);
            
            // Return result from new block
            // next ptr = start + size
            auto nextOffset = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), startOffset.getResult(), size);
            auto nextPtr = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, nextOffset.getResult());
            builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), nextPtr, ptrAddr);
            
            builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange{newCurrentPtr});
        }
        
        // Fast Block
        builder.setInsertionPointToStart(fastBlock);
        {
            auto nextPtr = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, nextInt.getResult());
            builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), nextPtr, ptrAddr);
            builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange{currentPtr});
        }
    // }
#endif


#if 0
    // __gloin_arena_free(Arena*) -> void
    // if (!theModule.lookupSymbol("__gloin_arena_free")) {
        auto funcType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType}, false);
        auto func = builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "__gloin_arena_free", funcType);
        
        auto* entry = func.addEntryBlock(builder);
        mlir::OpBuilder::InsertionGuard funcGuard(builder);
        builder.setInsertionPointToStart(entry);
        
        auto arena = entry->getArgument(0);
        auto structType = type_table["Arena"];
        
        // Load blocks
        auto blocksAddr = builder.create<mlir::LLVM::GEPOp>(builder.getUnknownLoc(), ptrType, structType, arena, llvm::ArrayRef<mlir::LLVM::GEPArg>{0, 2});
        auto currentBlock = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), ptrType, blocksAddr);
        
        // Loop
        auto* condBlock = func.addBlock();
        auto* bodyBlock = func.addBlock();
        auto* endBlock = func.addBlock();
        
        builder.create<mlir::LLVM::BrOp>(builder.getUnknownLoc(), mlir::ValueRange{currentBlock}, condBlock);
        
        builder.setInsertionPointToStart(condBlock);
        auto blockPtr = condBlock->addArgument(ptrType, builder.getUnknownLoc());
        auto nullPtr = builder.create<mlir::LLVM::ZeroOp>(builder.getUnknownLoc(), ptrType);
        auto isNotNull = builder.create<mlir::LLVM::ICmpOp>(builder.getUnknownLoc(), mlir::LLVM::ICmpPredicate::ne, blockPtr, nullPtr);
        builder.create<mlir::LLVM::CondBrOp>(builder.getUnknownLoc(), isNotNull, bodyBlock, endBlock);
        
        builder.setInsertionPointToStart(bodyBlock);
        // Load next
        auto nextPtrLoad = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), ptrType, blockPtr);
        
        // free(blockPtr)
        // auto freeFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
        builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), mlir::TypeRange{}, mlir::SymbolRefAttr::get(&context, "free"), mlir::ValueRange{blockPtr});
        
        builder.create<mlir::LLVM::BrOp>(builder.getUnknownLoc(), mlir::ValueRange{nextPtrLoad}, condBlock);
        
        builder.setInsertionPointToStart(endBlock);
        builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange{});
    // }
#endif
}


void CodeGen::dump() {
    theModule.dump();
}

void CodeGen::enter_scope() {
    current_scope = std::make_shared<GenScope>(GenScope{ {}, {}, current_scope });
}

void CodeGen::leave_scope() {
    if (current_scope->parent)
        current_scope = current_scope->parent;
}

void CodeGen::declare(const std::string& name, mlir::Value value, bool is_address, mlir::Type type) {
    current_scope->values[name] = {value, is_address, type};
}

CodeGen::SymbolInfo CodeGen::lookup(const std::string& name) {
    if (current_scope->values.count(name))
        return current_scope->values[name];
    if (current_scope->parent) {
        auto ptr = current_scope->parent;
        while (ptr) {
            if (ptr->values.count(name)) return ptr->values[name];
            ptr = ptr->parent;
        }
    }
    return {nullptr, false, nullptr};
}

mlir::Type CodeGen::resolve_type(const std::string& type_name) {
    if (type_table.count(type_name)) {
        return type_table[type_name];
    }
    
    // Check for Array [T; N]
    if (type_name.front() == '[' && type_name.back() == ']') {
         size_t semicolonPos = type_name.find(';');
         if (semicolonPos != std::string::npos) {
             std::string subTypeStr = type_name.substr(1, semicolonPos - 1);
             std::string sizeStr = type_name.substr(semicolonPos + 1, type_name.size() - semicolonPos - 2);
             
             // Trim? Assuming clean output from parser
             mlir::Type elemType = resolve_type(subTypeStr);
             int64_t size = std::stoll(sizeStr);
             
             return mlir::LLVM::LLVMArrayType::get(elemType, size);
         }
    }
    
    // Fallback or error?
    return builder.getI32Type();
}

mlir::ModuleOp CodeGen::generate(const std::vector<std::unique_ptr<Statement>>& program) {
    for (const auto& stmt : program) {
        gen_statement(stmt.get());
    }
    return theModule;
}

void CodeGen::gen_statement(const Statement* stmt) {
    if (auto* decl = dynamic_cast<const VariableDeclaration*>(stmt)) {
        mlir::Type type = builder.getI32Type();
        if (decl->type) {
            type = resolve_type(decl->type->value);
        }
        
        mlir::Value initVal = nullptr;
        if (decl->initializer) {
            initVal = gen_expression(decl->initializer.get());
        } else {
            // Default init?
            if (type.isInteger(32)) 
                initVal = builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), 0, 32);
            // TODO: Struct default init?
        }

        if (decl->is_mutable) {
            // Allocate stack memory
            mlir::Value alloca;
            if (llvm::isa<mlir::LLVM::LLVMStructType>(type)) {
                auto ptrType = mlir::LLVM::LLVMPointerType::get(&context);
                auto one = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(1));
                alloca = builder.create<mlir::LLVM::AllocaOp>(builder.getUnknownLoc(), ptrType, type, one, 0);
            } else {
                alloca = builder.create<mlir::memref::AllocaOp>(
                    builder.getUnknownLoc(), 
                    mlir::MemRefType::get({}, type) // Scalar memref
                );
            }

            if (initVal) {
                if (llvm::isa<mlir::LLVM::LLVMPointerType>(alloca.getType())) {
                    builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), initVal, alloca);
                } else {
                    builder.create<mlir::memref::StoreOp>(builder.getUnknownLoc(), initVal, alloca);
                }
            }
            declare(decl->name->value, alloca, true, type);
        } else {
            // Immutable, just SSA value
            if (initVal) {
                declare(decl->name->value, initVal, false, type);
            }
        }
    } 
    else if (const auto* struct_def = dynamic_cast<const StructDefinition*>(stmt)) {
        std::vector<mlir::Type> elementTypes;
        int idx = 0;
        int current_bit_offset = 0; // Or track explicitly provided offsets
        
        // Check backing type for endianness
        if (struct_def->backing_type) {
            std::string bt = struct_def->backing_type->value;
            if (bt.find("be_") == 0) {
                struct_is_big_endian[struct_def->name->value] = true;
            } else {
                struct_is_big_endian[struct_def->name->value] = false;
            }
        }
        
        for (const auto& field : struct_def->fields) {
            mlir::Type t = resolve_type(field.type->value);
            elementTypes.push_back(t);
            struct_field_indices[struct_def->name->value][field.name->value] = idx++;
            struct_field_types[struct_def->name->value][field.name->value] = t;
            
            if (struct_def->is_packed && field.offset != -1) {
                struct_field_offsets[struct_def->name->value][field.name->value] = field.offset;
                // TODO: infer width from type 'u4' etc.
                // Assuming simple mapping for now.
            }
        }
        
        auto structType = mlir::LLVM::LLVMStructType::getIdentified(&context, struct_def->name->value);
        if (mlir::failed(structType.setBody(elementTypes, struct_def->is_packed))) {
             // Handle error? For now just log or ignore
             // But verified check usually prevents this.
        }
        type_table[struct_def->name->value] = structType;
    }
    else if (auto* func = dynamic_cast<const FunctionDefinition*>(stmt)) {
        std::vector<std::string> arg_names;
        std::vector<std::string> arg_type_names;
        
        for (const auto& param : func->parameters) {
             arg_names.push_back(param.name->value);
             arg_type_names.push_back(param.type->value);
        }
        
        // Resolve types
        std::vector<mlir::Type> mlir_arg_types;
        for(const auto& t : arg_type_names) mlir_arg_types.push_back(resolve_type(t));
        
        auto retType = resolve_type(func->return_type->value);
        
        auto funcType = builder.getFunctionType(mlir_arg_types, {retType});
        auto funcOp = mlir::func::FuncOp::create(builder.getUnknownLoc(), func->name->value, funcType);
        if (func->is_deferred) {
            funcOp->setAttr("is_deferred", builder.getUnitAttr());
        }
        theModule.push_back(funcOp);
        
        auto* entryBlock = funcOp.addEntryBlock();
        builder.setInsertionPointToStart(entryBlock);
        
        enter_scope();
        for (size_t i = 0; i < arg_names.size(); ++i) {
            // Args are immutable by default in Gloin? 
            // If they can be reassigned they need alloca.
            // Let's assume immutable args for now or copy to alloca if mutable required.
            declare(arg_names[i], entryBlock->getArgument(i), false, mlir_arg_types[i]);
        }
        
        if (const auto* block = dynamic_cast<const BlockStatement*>(func->body.get())) {
            for (const auto& s : block->statements) {
                gen_statement(s.get());
            }
        }
        
        // Ensure terminator
        auto* currentBlock = builder.getBlock();
        if (currentBlock->empty() || !currentBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
             emit_deferred(); // Emit deferred statements before implicit return

             // For void, return void? For i32 return 0?
             // Should verify return type.
             if (retType.isInteger(32)) {
                 auto zero = builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), 0, 32);
                 builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange({zero}));
             } else {
                 builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
             }
        }
        
        leave_scope();
    }
    else if (auto* ret = dynamic_cast<const ReturnStatement*>(stmt)) {

        emit_deferred(); // Emit deferred statements before explicit return

        if (ret->return_value) {
            auto val = gen_expression(ret->return_value.get());

            builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange({val}));
        } else {
            builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
        }
    }
    else if (auto* if_stmt = dynamic_cast<const IfStatement*>(stmt)) {

        auto cond = gen_expression(if_stmt->condition.get());
        
        auto* currentBlock = builder.getBlock();
        auto* region = currentBlock->getParent();
        
        auto* thenBlock = builder.createBlock(region);
        auto* elseBlock = builder.createBlock(region);
        auto* mergeBlock = builder.createBlock(region);
        
        // Jump from current to then/else
        builder.setInsertionPointToEnd(currentBlock);
        builder.create<mlir::cf::CondBranchOp>(builder.getUnknownLoc(), cond, thenBlock, mlir::ValueRange(), elseBlock, mlir::ValueRange());
        
        // Then Block
        builder.setInsertionPointToStart(thenBlock);
        if (const auto* block = dynamic_cast<const BlockStatement*>(if_stmt->consequence.get())) {
             for(const auto& s : block->statements) gen_statement(s.get());
        }
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
        
        // Continue in merge block
        builder.setInsertionPointToStart(mergeBlock);
        if (mergeBlock->hasNoPredecessors()) {
             builder.create<mlir::LLVM::UnreachableOp>(builder.getUnknownLoc());
        }
    }
    else if (auto* while_stmt = dynamic_cast<const WhileStatement*>(stmt)) {
        auto* currentBlock = builder.getBlock();
        auto* region = currentBlock->getParent();
        
        auto* condBlock = builder.createBlock(region);
        auto* bodyBlock = builder.createBlock(region);
        auto* mergeBlock = builder.createBlock(region);
        
        // Jump to condition
        builder.setInsertionPointToEnd(currentBlock);
        builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);
        
        // Condition Block
        builder.setInsertionPointToStart(condBlock);
        auto cond = gen_expression(while_stmt->condition.get());
        builder.create<mlir::cf::CondBranchOp>(builder.getUnknownLoc(), cond, bodyBlock, mlir::ValueRange(), mergeBlock, mlir::ValueRange());
        
        // Body Block
        builder.setInsertionPointToStart(bodyBlock);
        if (const auto* block = dynamic_cast<const BlockStatement*>(while_stmt->body.get())) {
             for(const auto& s : block->statements) gen_statement(s.get());
        }
        if (bodyBlock->empty() || !bodyBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);
        }
        
        // Continue in merge block
        builder.setInsertionPointToStart(mergeBlock);
    }
    else if (auto* block = dynamic_cast<const BlockStatement*>(stmt)) {
        // Blocks inside functions usually just flatten unless we want scopes?
        // Gloin blocks have scopes.
        enter_scope();
        for(const auto& s : block->statements) gen_statement(s.get());
        leave_scope();
    }
    else if (auto* expr_stmt = dynamic_cast<const ExpressionStatement*>(stmt)) {
        gen_expression(expr_stmt->expression.get());
    }
    else if (auto* defer_stmt = dynamic_cast<const DeferStatement*>(stmt)) {
        // Defer statement: Just add to the current scope's deferred list
        // It will be generated when scope exits or return happens.
        current_scope->deferred.push_back(defer_stmt);
    }
}

// Helper to emit deferred statements
// Walks up scopes until it hits a function boundary (or root)
// and emits statements in LIFO order (reverse of how they were added, 
// and from inner scope to outer scope? No, deferred is LIFO.
// If I defer A, then enter block, defer B. Return.
// Execution: B then A.
void CodeGen::emit_deferred() {
    auto ptr = current_scope;
    while (ptr) {
        // Reverse order for LIFO within scope
        for (auto it = ptr->deferred.rbegin(); it != ptr->deferred.rend(); ++it) {
            gen_expression((*it)->call.get());
        }
        ptr = ptr->parent;
        // Stop at function boundary (implied by implementation structure, 
        // global scope usually has different parent or we handle it otherwise)
        if (!ptr) break; 
    }
}

mlir::Value CodeGen::gen_expression(const Expression* expr) {
    if (auto* int_lit = dynamic_cast<const IntegerLiteral*>(expr)) {
        return builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), int_lit->value, 32);
    }
    else if (auto* float_lit = dynamic_cast<const FloatLiteral*>(expr)) {
        // Use arith::ConstantOp which is more generic and often easier to construct
        // It takes an attribute.
        auto floatType = builder.getF32Type();
        auto floatAttr = builder.getFloatAttr(floatType, float_lit->value);
        return builder.create<mlir::arith::ConstantOp>(builder.getUnknownLoc(), floatType, floatAttr);
    }
    else if (auto* bool_lit = dynamic_cast<const BooleanLiteral*>(expr)) {
        return builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), bool_lit->value ? 1 : 0, 1);
    }
    else if (auto* str_lit = dynamic_cast<const StringLiteral*>(expr)) {
        std::string strVal = str_lit->value;
        auto strType = mlir::LLVM::LLVMArrayType::get(builder.getI8Type(), strVal.size() + 1);
        
        std::string globalName = "str_" + std::to_string(std::hash<std::string>{}(strVal));
        
        mlir::LLVM::GlobalOp globalStr;
        {
             mlir::OpBuilder::InsertionGuard guard(builder);
             builder.setInsertionPointToStart(theModule.getBody());
             // Check if exists or create unique? For now replace/create
             globalStr = builder.create<mlir::LLVM::GlobalOp>(
                 builder.getUnknownLoc(),
                 strType,
                 /*isConstant=*/true,
                 mlir::LLVM::Linkage::Internal,
                 globalName,
                 builder.getStringAttr(strVal + "\0")
             );
        }
        
        auto globalPtr = builder.create<mlir::LLVM::AddressOfOp>(builder.getUnknownLoc(), globalStr);
        
        auto stringStructType = type_table["String"];
        auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), stringStructType);
        
        auto zero = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(0));
        auto gep = builder.create<mlir::LLVM::GEPOp>(
             builder.getUnknownLoc(),
             mlir::LLVM::LLVMPointerType::get(&context),
             strType,
             globalPtr,
             mlir::ValueRange{zero, zero}
        );
        
        auto tmp1 = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), undef, gep, llvm::ArrayRef<int64_t>{0});
        
        auto lenVal = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(strVal.size()));
        auto res = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), tmp1, lenVal, llvm::ArrayRef<int64_t>{1});
        
        return res;
    }
    else if (auto* array_lit = dynamic_cast<const ArrayLiteral*>(expr)) {
        if (array_lit->elements.empty()) return nullptr; // Or empty array?
        
        std::vector<mlir::Value> values;
        mlir::Type elemType;
        for (const auto& el : array_lit->elements) {
            auto val = gen_expression(el.get());
            if (!val) return nullptr;
            values.push_back(val);
            if (!elemType) elemType = val.getType();
        }
        
        auto arrayType = mlir::LLVM::LLVMArrayType::get(elemType, values.size());
        auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), arrayType);
        mlir::Value current = undef;
        
        for (size_t i = 0; i < values.size(); ++i) {
             current = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), current, values[i], llvm::ArrayRef<int64_t>{(int64_t)i});
        }
        
        return current;
    }
    else if (auto* prefix = dynamic_cast<const PrefixExpression*>(expr)) {
        if (prefix->op == "*") {
             auto ptr = gen_expression(prefix->right.get());
             if (!ptr) return nullptr;
             // Dereference: Load
             // We need type. Assuming i32 for now if unknown.
             return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), builder.getI32Type(), ptr);
        } else if (prefix->op == "&") {
             // AddressOf
             return gen_address(prefix->right.get());
        }
    }
    else if (auto* ident = dynamic_cast<const Identifier*>(expr)) {
        auto sym = lookup(ident->value);
        if (sym.value) {
            if (sym.is_address) {
                if (llvm::isa<mlir::LLVM::LLVMPointerType>(sym.value.getType())) {
                     return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), sym.type, sym.value);
                }
                return builder.create<mlir::memref::LoadOp>(builder.getUnknownLoc(), sym.value);
            } else {
                return sym.value;
            }
        }
        return nullptr;
    }
    else if (auto* member_access = dynamic_cast<const MemberAccessExpression*>(expr)) {
        // Check for packed struct field first
        auto baseType = get_expression_type(member_access->left.get());
        if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(baseType)) {
             auto ident = dynamic_cast<const Identifier*>(member_access->member.get());
             std::string structName = structType.getName().str();
             
             if (struct_field_offsets.count(structName) && struct_field_offsets[structName].count(ident->value)) {
                 // It is a packed field!
                 int offset = struct_field_offsets[structName][ident->value];
                 // Assume width 1 or need to lookup width. Default 1?
                 int width = 1; // Simplify
                 
                 // Load the backing storage.
                 // Need address of the struct.
                 auto baseAddr = gen_address(member_access->left.get());
                 if (!baseAddr) return nullptr;
                 
                 // For packed(u32), we assume the struct IS a u32?
                 // Or we load the whole struct as integer?
                 // If it's a struct type in LLVM, we can't just load as int.
                 // We need to cast pointer to int pointer?
                 auto intPtrType = mlir::LLVM::LLVMPointerType::get(&context); 
                 // Cast baseAddr to i32*?
                 auto i32Type = builder.getI32Type();
                 auto bitcast = builder.create<mlir::LLVM::BitcastOp>(builder.getUnknownLoc(), intPtrType, baseAddr);
                 
                 auto val = builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), i32Type, bitcast);
                 
                 // Handle Endianness
                 bool isBigEndian = struct_is_big_endian[structName];
                 
                 mlir::Value shifted;
                 if (isBigEndian) {
                     int actual_shift = 32 - offset - width;
                     auto shiftAmt = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i32Type, builder.getI32IntegerAttr(actual_shift));
                     shifted = builder.create<mlir::LLVM::LShrOp>(builder.getUnknownLoc(), val, shiftAmt);
                 } else {
                     // Little Endian: Offset is from LSB
                     auto shiftAmt = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i32Type, builder.getI32IntegerAttr(offset));
                     shifted = builder.create<mlir::LLVM::LShrOp>(builder.getUnknownLoc(), val, shiftAmt);
                 }
                 
                 // Mask & ((1 << width) - 1)
                 int maskVal = (1 << width) - 1;
                 auto mask = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i32Type, builder.getI32IntegerAttr(maskVal));
                 auto masked = builder.create<mlir::LLVM::AndOp>(builder.getUnknownLoc(), shifted, mask);
                 
                 return masked;
             }
        }

        // Normal Read access
        auto addr = gen_address(expr);
        if (addr) {
            auto type = get_expression_type(expr);
            return builder.create<mlir::LLVM::LoadOp>(builder.getUnknownLoc(), type, addr);
        }
        return nullptr;
    }
    else if (auto* bin = dynamic_cast<const InfixExpression*>(expr)) {
        auto left = gen_expression(bin->left.get());
        auto right = gen_expression(bin->right.get());
        
        if (!left || !right) return nullptr;
        
        if (bin->op == "+") return builder.create<mlir::arith::AddIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "-") return builder.create<mlir::arith::SubIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "*") return builder.create<mlir::arith::MulIOp>(builder.getUnknownLoc(), left, right);
        if (bin->op == "/") return builder.create<mlir::arith::DivSIOp>(builder.getUnknownLoc(), left, right);
        
        if (bin->op == "==") {
            return builder.create<mlir::arith::CmpIOp>(builder.getUnknownLoc(), mlir::arith::CmpIPredicate::eq, left, right);
        }
        if (bin->op == "<") {
            return builder.create<mlir::arith::CmpIOp>(builder.getUnknownLoc(), mlir::arith::CmpIPredicate::slt, left, right);
        }
         if (bin->op == ">") {
            return builder.create<mlir::arith::CmpIOp>(builder.getUnknownLoc(), mlir::arith::CmpIPredicate::sgt, left, right);
        }
        // ... more ops
    }
    else if (auto* assign = dynamic_cast<const AssignmentExpression*>(expr)) {
        auto right = gen_expression(assign->right.get());
        
        // Use gen_address for LHS
        auto lhsAddr = gen_address(assign->left.get());
        if (lhsAddr) {
             if (llvm::isa<mlir::LLVM::LLVMPointerType>(lhsAddr.getType())) {
                 builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), right, lhsAddr);
             } else {
                 builder.create<mlir::memref::StoreOp>(builder.getUnknownLoc(), right, lhsAddr);
             }
             return right;
        }
    }
    else if (auto* spawn = dynamic_cast<const SpawnExpression*>(expr)) {
        if (auto* call = dynamic_cast<const CallExpression*>(spawn->call.get())) {
            std::string funcName;
            if (auto* ident = dynamic_cast<const Identifier*>(call->function.get())) {
                funcName = ident->value;
            } else {
                return nullptr;
            }

            std::vector<mlir::Value> operands;
            for (const auto& arg : call->arguments) {
                auto val = gen_expression(arg.get());
                if (!val) return nullptr;
                operands.push_back(val);
            }

            mlir::Type retType = builder.getI32Type();
            if (auto funcOp = theModule.lookupSymbol<mlir::func::FuncOp>(funcName)) {
                 if (funcOp.getFunctionType().getNumResults() > 0)
                    retType = funcOp.getFunctionType().getResult(0);
                 else
                    retType = builder.getNoneType();
            }

            auto spawnType = gloin::GloinSpawnType::get(&context, retType);
            
            auto spawnOp = builder.create<gloin::SpawnOp>(
                builder.getUnknownLoc(), 
                spawnType, 
                mlir::SymbolRefAttr::get(&context, funcName), 
                operands
            );
            return spawnOp.getResult();
        }
        return nullptr;
    }
    else if (auto* call = dynamic_cast<const CallExpression*>(expr)) {
         std::string funcName;
         bool is_deferred = false;
         mlir::Type retType = builder.getNoneType();

         if (auto* ident = dynamic_cast<const Identifier*>(call->function.get())) {
            funcName = ident->value;
            
            if (auto funcOp = theModule.lookupSymbol<mlir::func::FuncOp>(funcName)) {
                if (funcOp->hasAttr("is_deferred")) {
                    is_deferred = true;
                }
                if (funcOp.getFunctionType().getNumResults() > 0) {
                    retType = funcOp.getFunctionType().getResult(0);
                }
            }
         }

         if (is_deferred) {
             std::vector<mlir::Value> operands;
             for (const auto& arg : call->arguments) {
                 auto val = gen_expression(arg.get());
                 if (!val) return nullptr;
                 operands.push_back(val);
             }
             
             auto deferredType = gloin::GloinDeferredType::get(&context, retType);
             
             auto asyncCall = builder.create<gloin::AsyncCallOp>(
                 builder.getUnknownLoc(),
                 deferredType,
                 mlir::SymbolRefAttr::get(&context, funcName),
                 operands
             );
             return asyncCall.getResult();
         }

         // Check for MemberCall (Arena.alloc, Arena.free) or Static Call (Arena::new)
         // The Parser produces:
         // - Identifier(func) if global call
         // - MemberAccess(obj.method) if method call
         // - ScopeResolution? (Arena::new) -> Parser might handle this as Identifier("Arena::new") or ScopeAccess
         
         // Assuming Parser handles "Arena::new" as Identifier for now or MemberAccess
         
         // If it's a MemberAccessExpression inside the CallExpression
         if (auto* member = dynamic_cast<const MemberAccessExpression*>(call->function.get())) {
             // Method call: obj.method(args)
             // Lower to: Struct_method(&obj, args)
             
             auto* method_ident = dynamic_cast<const Identifier*>(member->member.get());
             std::string method_name = method_ident->value;
             
             // Check for Arena Intrinsics
             if (method_name == "alloc") {
                 mlir::Type allocType = builder.getI8Type();
                 int64_t size = 1;
                 if (!call->arguments.empty()) {
                     if (auto* ident = dynamic_cast<const Identifier*>(call->arguments[0].get())) {
                         allocType = resolve_type(ident->value);
                         size = get_type_size(allocType);
                     }
                 }
                 auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(size));
                 
                 auto arenaPtr = gen_address(member->left.get());
                 if (!arenaPtr) return nullptr;
                 
                 auto allocFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__gloin_arena_alloc");
                 return builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), allocFunc, mlir::ValueRange{arenaPtr, sizeVal}).getResult();
             } else if (method_name == "free") {
                 auto arenaPtr = gen_address(member->left.get());
                 if (!arenaPtr) return nullptr;
                 
                 auto freeFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__gloin_arena_free");
                 builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), freeFunc, mlir::ValueRange{arenaPtr});
                 return nullptr;
             }
             
             // Normal Method Call
             // 1. Generate address of object (self)
             auto objAddr = gen_address(member->left.get());
             if (!objAddr) return nullptr;
             
             // 2. Resolve method name (Struct_Method)
             auto objType = get_expression_type(member->left.get());
             std::string structName;
             if (auto st = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(objType)) {
                 structName = st.getName().str();
             }
             std::string mangledName = structName + "_" + method_name;
             
             std::vector<mlir::Value> args;
             args.push_back(objAddr); // self pointer
             for(const auto& arg : call->arguments) {
                 args.push_back(gen_expression(arg.get()));
             }
             
             // TODO: Lookup function signature for return type
             auto callOp = builder.create<mlir::func::CallOp>(builder.getUnknownLoc(), mangledName, mlir::TypeRange{builder.getI32Type()}, args); 
             return callOp.getResult(0);
         }
         else if (auto* ident = dynamic_cast<const Identifier*>(call->function.get())) {
             std::string funcName = ident->value;
             
             // Check for Static Intrinsics
             if (funcName == "Arena::new") {
                 auto arenaType = type_table["Arena"];
                 auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), arenaType);
                 
                 // malloc(4096)
                 auto size = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), builder.getI64Type(), builder.getI64IntegerAttr(4096));
                 auto mallocFunc = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
                 auto block = builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), mallocFunc, mlir::ValueRange{size}).getResult();
                 
                 // Init block header (next = null)
                 auto ptrType = mlir::LLVM::LLVMPointerType::get(&context);
                 auto nullPtr = builder.create<mlir::LLVM::ZeroOp>(builder.getUnknownLoc(), ptrType);
                 builder.create<mlir::LLVM::StoreOp>(builder.getUnknownLoc(), nullPtr, block);
                 
                 // ptr = block + 8
                 auto i64Type = builder.getI64Type();
                 auto eight = builder.create<mlir::LLVM::ConstantOp>(builder.getUnknownLoc(), i64Type, builder.getI64IntegerAttr(8));
                 auto blockInt = builder.create<mlir::LLVM::PtrToIntOp>(builder.getUnknownLoc(), i64Type, block);
                 auto ptrInt = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), blockInt, eight);
                 auto ptr = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, ptrInt.getResult());
                 
                 // end = block + 4096
                 auto endInt = builder.create<mlir::LLVM::AddOp>(builder.getUnknownLoc(), blockInt, size);
                 auto end = builder.create<mlir::LLVM::IntToPtrOp>(builder.getUnknownLoc(), ptrType, endInt.getResult());
                 
                 auto tmp1 = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), undef, ptr, llvm::ArrayRef<int64_t>{0});
                 auto tmp2 = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), tmp1, end, llvm::ArrayRef<int64_t>{1});
                 auto res = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), tmp2, block, llvm::ArrayRef<int64_t>{2});
                 
                 return res;
             }

             std::vector<mlir::Value> args;
             for(const auto& arg : call->arguments) {
                 args.push_back(gen_expression(arg.get()));
             }
             auto callOp = builder.create<mlir::func::CallOp>(builder.getUnknownLoc(), funcName, mlir::TypeRange{builder.getI32Type()}, args); 
             return callOp.getResult(0);
         }
    }
    else if (auto* await_expr = dynamic_cast<const AwaitExpression*>(expr)) {
        auto operand = gen_expression(await_expr->expr.get());
        if (!operand) return nullptr;
        
        mlir::Type resType = builder.getI32Type();
        if (auto deferred = llvm::dyn_cast<gloin::GloinDeferredType>(operand.getType())) {
             resType = deferred.getValueType();
        }
        
        return builder.create<gloin::AwaitOp>(builder.getUnknownLoc(), resType, operand).getResult();
    }
    else if (auto* struct_lit = dynamic_cast<const StructLiteral*>(expr)) {
        std::string structName = struct_lit->name->value;
        if (!type_table.count(structName)) return nullptr;
        
        mlir::Type type = type_table[structName];
        auto undef = builder.create<mlir::LLVM::UndefOp>(builder.getUnknownLoc(), type);
        mlir::Value current = undef;
        
        for (const auto& field : struct_lit->fields) {
            std::string fieldName = field.first;
            auto val = gen_expression(field.second.get());
            if (!val) return nullptr;
            
            int index = struct_field_indices[structName][fieldName];
            current = builder.create<mlir::LLVM::InsertValueOp>(builder.getUnknownLoc(), current, val, llvm::ArrayRef<int64_t>{index});
        }
        
        return current;
    }
    
    return nullptr;
}

void CodeGen::gen_function(const std::string& name, const std::vector<std::string>& args, const Statement* body) {
   // Deprecated/Refactored into gen_statement(FunctionDefinition)
   // Keeping method signature in header for now but logic moved.
}

mlir::Type CodeGen::get_expression_type(const Expression* expr) {
    if (auto* ident = dynamic_cast<const Identifier*>(expr)) {
        return lookup(ident->value).type;
    }
    if (auto* member_access = dynamic_cast<const MemberAccessExpression*>(expr)) {
         auto leftType = get_expression_type(member_access->left.get());
         if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(leftType)) {
              auto ident = dynamic_cast<const Identifier*>(member_access->member.get());
              std::string structName = structType.getName().str();
              if (struct_field_types.count(structName) && struct_field_types[structName].count(ident->value)) {
                  return struct_field_types[structName][ident->value];
              }
         }
    }
    if (dynamic_cast<const IntegerLiteral*>(expr)) return builder.getI32Type();
    if (dynamic_cast<const BooleanLiteral*>(expr)) return builder.getI1Type();
    if (dynamic_cast<const StringLiteral*>(expr)) return type_table["String"];
    
    if (auto* array_lit = dynamic_cast<const ArrayLiteral*>(expr)) {
        if (array_lit->elements.empty()) return builder.getNoneType(); // Unknown
        auto elemType = get_expression_type(array_lit->elements[0].get());
        return mlir::LLVM::LLVMArrayType::get(elemType, array_lit->elements.size());
    }
    
    return builder.getI32Type();
}

mlir::Value CodeGen::gen_address(const Expression* expr) {
    if (auto* ident = dynamic_cast<const Identifier*>(expr)) {
        auto sym = lookup(ident->value);
        if (sym.value && sym.is_address) return sym.value;
    } 
    else if (auto* member_access = dynamic_cast<const MemberAccessExpression*>(expr)) {
        auto baseAddr = gen_address(member_access->left.get());
        if (!baseAddr) return nullptr;
        
        auto baseExprType = get_expression_type(member_access->left.get());
        if (auto structType = llvm::dyn_cast<mlir::LLVM::LLVMStructType>(baseExprType)) {
             auto ident = dynamic_cast<const Identifier*>(member_access->member.get());
             std::string structName = structType.getName().str();
             int index = struct_field_indices[structName][ident->value];
             
             return builder.create<mlir::LLVM::GEPOp>(
                 builder.getUnknownLoc(),
                 mlir::LLVM::LLVMPointerType::get(&context),
                 structType,
                 baseAddr,
                 llvm::ArrayRef<mlir::LLVM::GEPArg>{0, index}
             );
        }
    }
    return nullptr;
}
