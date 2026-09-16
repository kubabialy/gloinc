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
    auto result = compile_source(code, "e2e.gloin", context, CompilationMode::Executable);
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

TEST(E2ETest, NestedReturnPathsExecuteEveryArm) {
    expect_result(run_code(R"(
        def choose(a: bool, b: bool) -> i32 {
            if a {
                if b { return 6; } else { return 10; }
            } else {
                if b { { return 12; } } else { return 14; }
            }
        }
        def main() -> i32 {
            return choose(true, true) + choose(true, false) +
                   choose(false, true) + choose(false, false);
        }
    )"),
                  42);
}

TEST(E2ETest, ImplicitAndEarlyVoidReturnsExecute) {
    expect_result(run_code(R"(
        def main() -> i32 {
            empty(); work(true); work(false); all(true); all(false);
            value();
            return value();
        }
        def empty() -> void {}
        def work(b: bool) -> void { if b { return; } empty(); }
        def all(b: bool) -> void { if b { return; } else { return; } }
        def value() -> i32 { return 42; }
    )"),
                  42);
}

TEST(E2ETest, LoopReturnsAndFallbackBothExecute) {
    expect_result(run_code(R"(
        def pick(enabled: bool, arm: bool) -> i32 {
            while enabled { if arm { return 10; } else { return 12; } }
            return 20;
        }
        def main() -> i32 {
            return pick(true, true) + pick(true, false) + pick(false, true);
        }
    )"),
                  42);
}

TEST(E2ETest, NestedCallsPreserveValueParametersAndReturnTypes) {
    expect_result(run_code(R"(
        def main() -> int {
            def mut x: i32 = 40;
            def answer: i32 = add(copy(x), narrow(2), true);
            x = 0;
            return answer;
        }
        def copy(x: i32) -> i32 { def mut local: i32 = x; local = local + 1; return local; }
        def narrow(x: u8) -> u8 { return x; }
        def add(left: i32, right: u8, enabled: bool) -> i32 {
            if enabled { if right == 2 { return left + 1; } return 0; }
            return -1;
        }
    )"),
                  42);
}

TEST(E2ETest, EntryAliasPreservesNegativeProgramResult) {
    expect_result(run_code("def priv main() -> int { return -1; }"), -1);
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

TEST(E2ETest, MultilineIdentifierConditionAndCalls) {
    std::string code = R"(
        def value(
        ) -> i32 { return 42; }
        def main() -> i32 {
            def enabled: bool = true;
            if enabled
            {
                return value
                (
                );
            } else {
                return 0;
            }
        }
    )";
    expect_result(run_code(code), 42);
}

TEST(E2ETest, CheckedAliasesAndShadowedBindings) {
    expect_result(run_code(R"(
        def keep(x: int) -> i32 {
            { def mut x: i32 = 7; x = 9; }
            return x;
        }
        def main() -> int { return keep(42); }
    )"),
                  42);
}

TEST(E2ETest, ForwardCallChain) {
    expect_result(run_code(R"(
        def main() -> i32 { ready(); return first(40); }
        def first(x: int) -> i32 { return second(x) + 1; }
        def ready() -> void {}
        def second(x: i32) -> int { return x + 1; }
    )"),
                  42);
}

TEST(E2ETest, DirectRecursion) {
    expect_result(run_code(R"(
        def main() -> i32 { return sum(6); }
        def sum(n: i32) -> i32 {
            if n == 0 { return 0; } else { return n + sum(n - 1); }
        }
    )"),
                  21);
}

TEST(E2ETest, MutualRecursion) {
    expect_result(run_code(R"(
        def main() -> i32 { return score(10) + score(9); }
        def score(n: i32) -> i32 {
            if even(n) {
                if odd(n - 1) { return 21; } else { return 1; }
            } else {
                if odd(n) { return 21; } else { return 2; }
            }
        }
        def even(n: i32) -> bool {
            if n == 0 { return true; } else { return odd(n - 1); }
        }
        def odd(n: i32) -> bool {
            if n == 0 { return false; } else { return even(n - 1); }
        }
    )"),
                  42);
}

TEST(E2ETest, ShadowInitializerUsesOuterBindingAndRestoresFunction) {
    expect_result(run_code(R"(
        def main() -> i32 {
            def mut result: i32 = 0;
            {
                def answer: i32 = answer();
                { def answer: i32 = answer + 1; result = answer; }
                result = result + answer;
            }
            return result - answer() - 1;
        }
        def answer() -> i32 { return 42; }
    )"),
                  42);
}

TEST(E2ETest, SiblingAndLoopScopesKeepIndependentBindings) {
    expect_result(run_code(R"(
        def main() -> i32 {
            def mut result: i32 = 0;
            def x: i32 = 30;
            if true { def x: i32 = 5; result = x; }
            else { def x: i32 = 99; result = x; }
            { def x: i32 = 7; result = result + x; }
            def mut count: i32 = 0;
            while count < 2 {
                def x: i32 = count;
                result = result + x;
                count = count + 1;
            }
            return result + x - 1;
        }
    )"),
                  42);
}

TEST(E2ETest, DelayedMutableAndImmutableInitialization) {
    expect_result(run_code(R"(
        def main() -> i32 {
            def x: i32;
            def mut y: i32;
            x = 40;
            y = x;
            y = y + 2;
            return y;
        }
    )"),
                  42);
}

TEST(E2ETest, BranchesInitializeImmutableLocalsOncePerPath) {
    expect_result(run_code(R"(
        def choose(flag: bool) -> i32 {
            def x: i32;
            if flag { x = 20; } else { x = 22; }
            return x;
        }
        def main() -> i32 { return choose(true) + choose(false); }
    )"),
                  42);
}

TEST(E2ETest, ReturningBranchesDoNotRequireInitialization) {
    expect_result(run_code(R"(
        def choose(flag: bool) -> i32 {
            def x: i32;
            if flag { return 20; } else { x = 22; }
            return x;
        }
        def main() -> i32 { return choose(true) + choose(false); }
    )"),
                  42);
}

TEST(E2ETest, LoopLocalsInitializeFreshEachIteration) {
    expect_result(run_code(R"(
        def main() -> i32 {
            def mut total: i32 = 0;
            def mut i: i32 = 0;
            while i < 3 {
                def x: i32;
                if i == 0 { x = 10; } else { x = 16; }
                total = total + x;
                i = i + 1;
            }
            return total;
        }
    )"),
                  42);
}

TEST(E2ETest, GlobalAndLocalConstantsFoldWithLexicalShadowing) {
    expect_result(run_code(R"(
        def main() -> i32 {
            def mut total: i32 = BASE;
            {
                def const BASE: i32 = BASE * 2;
                total = total + BASE;
            }
            return total + BASE;
        }
        def const START: int = 10;
        def const BASE: i32 = START + 1;
    )"),
                  44);
}

TEST(E2ETest, ConstantBooleanShortCircuitSkipsInvalidArithmetic) {
    expect_result(run_code(R"(
        def const SKIP: bool = false && (1 / 0 == 0);
        def const TAKE: bool = true || (1 / 0 == 0);
        def main() -> i32 {
            if SKIP { return 1; }
            if TAKE { return 42; } else { return 2; }
        }
    )"),
                  42);
}

TEST(E2ETest, ConstantNumericAndBooleanOperationsExecuteFoldedResults) {
    expect_result(run_code(R"(
        def const FRACTION: f32 = (1.5 + 2.5) * 3.0 / 2.0;
        def const VALID: bool = FRACTION == 6.0 && !(3 > 4) && (-7 % 3 == -1);
        def const ANSWER: i32 = (100 / 5) + 22;
        def main() -> i32 {
            if VALID { return ANSWER; } else { return 0; }
        }
    )"),
                  42);
}

TEST(E2ETest, IntegerLiteralBasesExecuteTheirValues) {
    for (const std::string spelling : {"42", "042", "0x2A", "0X2a", "0b101010", "0B101010"})
        expect_result(run_code("def main() -> i32 { return " + spelling + "; }"), 42);
}

TEST(E2ETest, ContextualIntegerWidthsAndSignedMinimumExecute) {
    expect_result(run_code(R"(
        def accept(x: i8) -> i8 { def mut copy: i8; copy = x; return copy; }
        def wide(x: i64) -> i64 { return (1 + 1) + x; }
        def main() -> i32 {
            def value: i8 = -128;
            if accept(-128) == value {
                if wide(40) == 42 { return 42; } else { return 1; }
            } else { return 2; }
        }
    )"),
                  42);
}

TEST(E2ETest, FullU64AndI64LiteralRangesExecuteWithoutTruncation) {
    expect_result(run_code(R"(
        def full(x: usize) -> u64 { return x; }
        def minimum() -> i64 { return -9223372036854775808; }
        def main() -> i32 {
            def mut maximum: u64 = 18446744073709551615;
            maximum = 0xffffffffffffffff;
            if full(18446744073709551615) == maximum {
                if minimum() == -0x8000000000000000 { return 42; } else { return 1; }
            } else { return 2; }
        }
    )"),
                  42);
}

TEST(E2ETest, ConstantArithmeticUsesResolvedIntegerWidths) {
    expect_result(run_code(R"(
        def const BIG: u64 = 18446744073709551615;
        def const HALF: u64 = BIG / 2;
        def const MIN: i64 = -9223372036854775808;
        def const NEXT: i64 = MIN + 1;
        def const CHECK: bool = BIG > HALF && NEXT > MIN;
        def const ANSWER: u8 = 6 * 7;
        def main() -> i32 {
            if CHECK { if ANSWER == 42 { return 42; } else { return 1; } }
            else { return 2; }
        }
    )"),
                  42);
}

TEST(E2ETest, F64ConstantRangeAndPrecisionAreRetained) {
    expect_result(run_code(R"(
        def const BIG: f64 = 1e300;
        def const HALF: f64 = BIG / 2.0;
        def const PRECISE: f64 = 16777216.0 + 1.0;
        def const CHECK: bool = HALF > 1e299 && PRECISE == 16777217.0;
        def main() -> i32 { if CHECK { return 42; } else { return 0; } }
    )"),
                  42);
}
