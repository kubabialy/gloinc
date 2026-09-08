#include <gtest/gtest.h>
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "../src/dialect/GloinDialect.h"
#include "../src/codegen.h"

TEST(MLIRSetup, ContextCreation) {
    mlir::MLIRContext context;
    EXPECT_TRUE(context.isMultithreadingEnabled());
}

TEST(MLIRSetup, AllCompilerDialects) {
    mlir::MLIRContext context;
    CodeGen codegen(context);
    mlir::OwningOpRef<mlir::ModuleOp> module(codegen.generate({}));

    // Exercise the compiler's actual registration path. Mixing MLIR archives with
    // libMLIR previously crashed when these dialects loaded in the same context.
    EXPECT_NE(context.getLoadedDialect<gloin::GloinDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::func::FuncDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::arith::ArithDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::cf::ControlFlowDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::memref::MemRefDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::scf::SCFDialect>(), nullptr);
    EXPECT_NE(context.getLoadedDialect<mlir::LLVM::LLVMDialect>(), nullptr);
}

TEST(MLIRSetup, DialectRegistration) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<gloin::GloinDialect>();
    
    // Test creating a Gloin type
    // !gloin.int<32, Big>
    mlir::OpBuilder builder(&context);
    auto type = gloin::GloinIntegerType::get(&context, 32, gloin::Endianness::Big);
    EXPECT_TRUE(type != nullptr);
    
    // Verify parameters
    EXPECT_EQ(type.getWidth(), 32);
    EXPECT_EQ(type.getEndianness(), gloin::Endianness::Big);
}

TEST(MLIRSetup, SpawnOpCreation) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<gloin::GloinDialect>();
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    
    mlir::OpBuilder builder(&context);
    
    // Create a dummy function to spawn
    auto location = builder.getUnknownLoc();
    auto i32Type = builder.getI32Type();
    
    // Spawn type: !gloin.spawn<i32>
    auto spawnType = gloin::GloinSpawnType::get(&context, i32Type);
    
    // create spawn op: spawn @func(args...) -> !gloin.spawn<i32>
    // We need a module to hold the function and the spawn call
    auto module = mlir::ModuleOp::create(location);
    builder.setInsertionPointToStart(module.getBody());
    
    auto funcType = builder.getFunctionType({}, {i32Type});
    auto workerFunc = builder.create<mlir::func::FuncOp>(location, "worker_func", funcType);
    
    // Create another function to call spawn in
    auto mainFunc = builder.create<mlir::func::FuncOp>(location, "main", builder.getFunctionType({}, {}));
    auto entryBlock = mainFunc.addEntryBlock();
    builder.setInsertionPointToStart(entryBlock);
    
    auto spawnOp = builder.create<gloin::SpawnOp>(
        location, 
        spawnType, 
        mlir::SymbolRefAttr::get(&context, "worker_func"), 
        mlir::ValueRange{}
    );
    
    ASSERT_TRUE(spawnOp != nullptr);
    ASSERT_EQ(spawnOp.getOperation()->getNumResults(), 1);
    ASSERT_TRUE(spawnOp.getType() == spawnType);
    
    builder.create<mlir::func::ReturnOp>(location);
    
    module.dump();
}
