#include "../src/compiler.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/sema.h"
#include "mlir/IR/Verifier.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include <gtest/gtest.h>

namespace {
llvm::Expected<int> run_code(const std::string &code) {
    mlir::MLIRContext context;
    auto result = compile_source(code, "e2e.gloin", context);
    if (!result.success()) {
        std::ostringstream errors;
        result.diagnostics->render(errors);
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", errors.str().c_str());
    }
    auto &module = result.module;
    if (!module || mlir::failed(mlir::verify(*module)))
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "Invalid generated module");
    std::string source;
    llvm::raw_string_ostream stream(source);
    module->print(stream);
    return gloin_test::run_external_mlir(source, {gloin_test::mlir_opt, {}},
                                         {gloin_test::mlir_runner, {}});
}

void expect_result(llvm::Expected<int> result, int expected) {
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, expected);
}
} // namespace

TEST(E2ETest, ReturnInteger) {
    std::string code = R"(
        def main() -> i32 {
            return 42;
        }
    )";
    expect_result(run_code(code), 42);
}

TEST(E2ETest, SimpleArithmetic) {
    std::string code = R"(
        def main() -> i32 {
            return 10 + 32;
        }
    )";
    expect_result(run_code(code), 42);
}

TEST(E2ETest, VariableUsage) {
    std::string code = R"(
        def main() -> i32 {
            def x: i32 = 10;
            def y: i32 = 32;
            return x + y;
        }
    )";
    expect_result(run_code(code), 42);
}

TEST(E2ETest, ControlFlowIf) {
    std::string code = R"(
        def main() -> i32 {
            if true {
                return 42;
            } else {
                return 0;
            }
        }
    )";
    expect_result(run_code(code), 42);
}

TEST(E2ETest, ControlFlowLoop) {
    std::string code = R"(
        def main() -> i32 {
            def mut i: i32 = 0;
            def mut sum: i32 = 0;
            while i < 10 {
                sum = sum + 1;
                i = i + 1;
            }
            return sum;
        }
    )";
    expect_result(run_code(code), 10);
}
