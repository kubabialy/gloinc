#include "gtest/gtest.h"
#include "jit_runner.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"

TEST(JitRunnerTest, SmokeTest) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();

    const char* moduleStr = R"MLIR(
        func.func @main() -> i32 {
            %c42 = arith.constant 42 : i32
            return %c42 : i32
        }
    )MLIR";

    mlir::OwningOpRef<mlir::ModuleOp> module = mlir::parseSourceString<mlir::ModuleOp>(moduleStr, &context);
    ASSERT_TRUE(*module);

    int result = JitRunner::run(module.get());
    EXPECT_EQ(result, 42);
}

