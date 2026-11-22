#include "../src/codegen.h"
#include "../src/dialect/GloinDialect.h"
#include "compiler.h"
#include "lowering.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include <gtest/gtest.h>

TEST(MLIRSetup, ContextCreation) {
    mlir::MLIRContext context;
    EXPECT_TRUE(context.isMultithreadingEnabled());
}

TEST(MLIRSetup, AllCompilerDialects) {
    mlir::MLIRContext context;
    CodeGen codegen(context);
    mlir::OwningOpRef<mlir::ModuleOp> module(codegen.generate_unchecked_for_testing({}));

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
    auto type = gloin::GloinIntegerType::get(&context, 32, gloin::Endianness::Big,
                                           gloin::IntegerSignedness::Unsigned);
    EXPECT_TRUE(type != nullptr);
    
    // Verify parameters
    EXPECT_EQ(type.getWidth(), 32);
    EXPECT_EQ(type.getEndianness(), gloin::Endianness::Big);
}

TEST(MLIRSetup, ThreadOwnershipRejectsDuplicatedAndForgedHandles) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(import "@threads";
        def spawnable worker() -> i32 { return 7; }
        def main() -> i32 {
            def r: result<threads.Thread<i32>> = spawn worker();
            if r.erroneous { return 1; }
            def t: threads.Thread<i32> = r.value;
            def answer: result<i32> = t.join();
            if answer.erroneous { return 2; }
            return answer.value;
        })",
                                   "owner.gloin", context);
    ASSERT_TRUE(compiled.success());
    gloin::JoinOp join;
    compiled.module->walk([&](gloin::JoinOp found) { join = found; });
    ASSERT_TRUE(join);
    mlir::OpBuilder builder(join);
    auto *duplicate = builder.clone(*join);
    Diagnostics copied;
    EXPECT_FALSE(verify_module(*compiled.module, copied));
    ASSERT_TRUE(copied.has_errors());
    EXPECT_NE(copied.all().front().message.find("already been joined"), std::string::npos);
    duplicate->erase();

    // Even an unused fabricated owner must fail before LLVM lowering.
    auto pointer = builder.create<mlir::LLVM::ZeroOp>(
        join.getLoc(), builder.getType<mlir::LLVM::LLVMPointerType>());
    builder.create<gloin::FromLayoutOp>(join.getLoc(), join.getHandle().getType(), pointer);
    Diagnostics forged;
    EXPECT_FALSE(verify_module(*compiled.module, forged));
    ASSERT_TRUE(forged.has_errors());
    EXPECT_NE(forged.all().front().message.find("originate"), std::string::npos);
}
