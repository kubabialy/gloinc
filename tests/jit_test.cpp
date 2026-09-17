#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <csignal>
#include <cstdlib>
#include <future>
#include <sys/wait.h>

namespace {
std::string render(const Diagnostics &diagnostics) {
    std::ostringstream out;
    diagnostics.render(out);
    return out.str();
}
std::string print(mlir::ModuleOp module) {
    std::string text;
    llvm::raw_string_ostream out(text);
    module.print(out);
    return text;
}
void expect_value(const ExecutionResult &result, int32_t value) {
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    ASSERT_TRUE(result.value);
    EXPECT_EQ(*result.value, value);
    EXPECT_FALSE(result.failed_stage);
    EXPECT_FALSE(result.diagnostics->has_errors());
}
void execute(const std::string &source, int32_t expected = 42) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto compiled = compile_source(source, "jit.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    expect_value(JitRunner::run(*compiled.module), expected);
}
mlir::OwningOpRef<mlir::ModuleOp> parse(mlir::MLIRContext &context, llvm::StringRef source) {
    CodeGen load_dialects(context);
    return mlir::parseSourceString<mlir::ModuleOp>(source, &context);
}
void expect_failure(const ExecutionResult &result, DiagnosticStage stage,
                    const std::string &message) {
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.value);
    EXPECT_EQ(result.failed_stage, stage);
    ASSERT_TRUE(result.diagnostics->has_errors());
    EXPECT_NE(render(*result.diagnostics).find(message), std::string::npos);
}
void run_trapping_source(const std::string &source) {
    mlir::MLIRContext context;
    auto compiled = compile_source(source, "trap.gloin", context, CompilationMode::Executable);
    if (!compiled.success())
        std::_Exit(80);
    auto result = JitRunner::run(*compiled.module);
    std::_Exit(result.success() ? 81 : 82);
}
bool is_arithmetic_trap(int status) {
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGTRAP || WTERMSIG(status) == SIGILL);
}
} // namespace

TEST(JitRunnerTest, SmokeTest) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();

    const char *moduleStr = R"MLIR(
        func.func @main() -> i32 {
            %c42 = arith.constant 42 : i32
            return %c42 : i32
        }
    )MLIR";

    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(moduleStr, &context);
    ASSERT_TRUE(module);

    auto result = JitRunner::run(module.get());
    ASSERT_TRUE(result.success());
    EXPECT_EQ(*result.value, 42);
}

TEST(JitRunnerTest, ExecutesCallsBranchesAndLoops) {
    for (const std::string source :
         {"def main() -> i32 { return add(40, 2); } def add(a: i32, b: i32) -> i32 { return a + b; "
          "}",
          "def choose(b: bool) -> i32 { if b { return 20; } else { return 22; } } def main() -> "
          "i32 { return choose(true) + choose(false); }",
          "def main() -> i32 { def mut i: i32 = 0; while i < 42 { i = i + 1; } return i; }",
          "def main() -> i32 { def mut sum: i32 = 0; for def mut i: i32 = 0; i < 3; i = i + 1 { "
          "unless i < 0 { sum = sum + 14; } } return sum; }",
          "def sum(n: i32) -> i32 { if n == 0 { return 0; } return n + sum(n - 1); } def main() -> "
          "i32 { return sum(6) * 2; }",
          "def main() -> i32 { def x: i32; if false && (1 / 0 == 0) { return 0; } else { x = 42; } "
          "for ;; { return x; } return 0; }"})
        execute(source);
}

TEST(JitRunnerTest, EveryCoreScalarCallAndCheckedOperatorExecutes) {
    std::string source = "def noop() -> void {} def invert(x: bool) -> bool { return !x; }";
    std::string main = "def main() -> i32 { noop(); if invert(true) { return 0; }";
    for (const std::string type :
         {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"}) {
        const bool fp = type.starts_with("f");
        source += "def calculate_" + type + "(x: " + type + ") -> " + type +
                  " { def mut y: " + type + " = x; y = y * " + (fp ? "2.0" : "2") + "; return y; }";
        main += "if calculate_" + type + "(" + (fp ? "21.0" : "21") +
                ") != " + (fp ? "42.0" : "42") + " { return 0; }";
    }
    execute(source + main + "return 42; }");
}

TEST(JitRunnerTest, EveryI32ResultIsSeparateFromFailure) {
    for (int32_t value : {int32_t{0}, int32_t{-1}, INT32_MIN, INT32_MAX, int32_t{42}})
        execute("def main() -> i32 { return " + std::to_string(value) + "; }", value);
    expect_failure(JitRunner::run({}), DiagnosticStage::Verification, "null module");
}

TEST(JitRunnerTest, RepeatedRunsPreserveBorrowedModule) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def main() -> i32 { def mut i: i32 = 0; for ; i < 42; i = i + 1 {} return i; }",
        "repeat.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    const auto before = print(*compiled.module);
    for (unsigned i = 0; i < 3; ++i) {
        expect_value(JitRunner::run(*compiled.module), 42);
        EXPECT_EQ(print(*compiled.module), before);
    }
}

TEST(JitRunnerTest, FreshEnginesDoNotShareGlobalState) {
    mlir::MLIRContext context;
    // Test-only IR global observes engine isolation; source globals remain deferred.
    auto module = parse(context, R"(
        module {
            llvm.mlir.global internal @counter(0) : i32
            llvm.func @main() -> i32 {
                %p = llvm.mlir.addressof @counter : !llvm.ptr
                %old = llvm.load %p : !llvm.ptr -> i32
                %one = llvm.mlir.constant(1 : i32) : i32
                %next = llvm.add %old, %one : i32
                llvm.store %next, %p : i32, !llvm.ptr
                llvm.return %next : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    for (unsigned i = 0; i < 3; ++i)
        expect_value(JitRunner::run(*module), 1);
}

TEST(JitRunnerTest, AcceptsAlreadyLoweredPrivateEntry) {
    mlir::MLIRContext context;
    auto compiled = compile_source("def priv main() -> int { return 42; }", "private.gloin",
                                   context, CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    expect_value(JitRunner::run(*compiled.module), 42);
}

TEST(JitRunnerTest, PackedWrapperNamesCannotHijackEntry) {
    execute("def _mlir_main(x: i32) -> i32 { return x + 1; } def main() -> i32 { return "
            "_mlir_main(41); }");
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            llvm.func @"gloin.jit.entry"() -> i32 {
                %c = llvm.mlir.constant(7 : i32) : i32
                llvm.return %c : i32
            }
            llvm.func @"_mlir_gloin.jit.entry.1"(%x: i32) -> i32 { llvm.return %x : i32 }
            llvm.func @main() -> i32 {
                %c = llvm.mlir.constant(42 : i32) : i32
                llvm.return %c : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    expect_value(JitRunner::run(*module), 42);
}

TEST(JitRunnerTest, MissingAndNonFunctionEntriesFailCleanly) {
    mlir::MLIRContext context;
    for (const std::string source :
         {"module {}", "module { llvm.mlir.global internal @main(0) : i32 }"}) {
        auto module = parse(context, source);
        ASSERT_TRUE(module);
        expect_failure(JitRunner::run(*module), DiagnosticStage::Execution,
                       "defined main() -> i32");
    }
}

TEST(JitRunnerTest, InvalidEntrySignaturesFailBeforeInvocation) {
    mlir::MLIRContext context;
    for (const std::string source :
         {"module { func.func @main(%x: i32) -> i32 { return %x : i32 } }",
          "module { func.func @main() { return } }",
          "module { func.func @main() -> i64 { %c = arith.constant 42 : i64 return %c : i64 } }",
          "module { func.func @main() -> f32 { %c = arith.constant 42.0 : f32 return %c : f32 } }",
          "module { func.func private @main() -> i32 }",
          "module { llvm.func @main(...) -> i32 { %c = llvm.mlir.constant(42 : i32) : i32 "
          "llvm.return %c : i32 } }"}) {
        SCOPED_TRACE(source);
        auto module = parse(context, source);
        ASSERT_TRUE(module);
        expect_failure(JitRunner::run(*module), DiagnosticStage::Execution, "entry must define");
    }
    auto compiled = compile_source("def main() -> i32 { return 42; }", "abi.gloin", context,
                                   CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    auto main = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main");
    main.setCConv(mlir::LLVM::CConv::Fast);
    expect_failure(JitRunner::run(*compiled.module), DiagnosticStage::Execution,
                   "C calling convention");
    main.setCConv(mlir::LLVM::CConv::C);
    main.setLinkage(mlir::LLVM::Linkage::AvailableExternally);
    expect_failure(JitRunner::run(*compiled.module), DiagnosticStage::Execution, "emitted linkage");
}

TEST(JitRunnerTest, ExternalDependenciesFailBeforeInvocation) {
    mlir::MLIRContext context;
    for (const std::string source : {
             R"(module {
                 func.func private @missing() -> i32 loc("missing.gloin":3:4)
                 func.func @main() -> i32 { %x = call @missing() : () -> i32 return %x : i32 }
             })",
             R"(module {
                 llvm.mlir.global external @missing() : i32
                 llvm.func @main() -> i32 {
                     %p = llvm.mlir.addressof @missing : !llvm.ptr
                     %x = llvm.load %p : !llvm.ptr -> i32
                     llvm.return %x : i32
                 }
             })"}) {
        auto module = parse(context, source);
        ASSERT_TRUE(module);
        expect_failure(JitRunner::run(*module), DiagnosticStage::Execution,
                       "not supported by the core JIT: missing");
    }
    auto compiled = compile_source(
        "def helper() -> i32 { return 42; } def main() -> i32 { return helper(); }", "helper.gloin",
        context, CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    auto helper = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("helper");
    helper.setLinkage(mlir::LLVM::Linkage::AvailableExternally);
    expect_failure(JitRunner::run(*compiled.module), DiagnosticStage::Execution,
                   "emitted linkage: helper");
    helper.setLinkage(mlir::LLVM::Linkage::External);
    helper.setCConv(mlir::LLVM::CConv::Fast);
    expect_failure(JitRunner::run(*compiled.module), DiagnosticStage::Execution,
                   "C calling convention");
}

TEST(JitRunnerTest, VerificationAndLoweringFailuresKeepTheirStage) {
    mlir::MLIRContext context;
    auto compiled = compile_source("def main() -> i32 { return 42; }", "bad.gloin", context);
    ASSERT_TRUE(compiled.success());
    auto main = compiled.module->lookupSymbol<mlir::func::FuncOp>("main");
    main.front().getTerminator()->setOperands(mlir::ValueRange{});
    expect_failure(JitRunner::run(*compiled.module), DiagnosticStage::Verification, "return");
    auto module = parse(context, R"(module { func.func @main() -> i32 {
        %c = "gloin.constant"() {value = 42 : i32} : () -> i32
        return %c : i32
    } })");
    ASSERT_TRUE(module);
    expect_failure(JitRunner::run(*module), DiagnosticStage::Lowering, "gloin.constant");
}

TEST(JitRunnerTest, LocatedDiagnosticsAndSuccessDoNotPrintImplicitly) {
    mlir::MLIRContext context;
    auto invalid =
        parse(context, "module { func.func @main() { return } loc(\"entry.gloin\":5:7) }");
    ASSERT_TRUE(invalid);
    auto compiled = compile_source("def main() -> i32 { return 42; }", "quiet.gloin", context);
    ASSERT_TRUE(compiled.success());
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    auto bad = JitRunner::run(*invalid);
    auto good = JitRunner::run(*compiled.module);
    auto stderr_text = testing::internal::GetCapturedStderr();
    auto stdout_text = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(stderr_text.empty()) << stderr_text;
    EXPECT_TRUE(stdout_text.empty()) << stdout_text;
    expect_failure(bad, DiagnosticStage::Execution, "entry.gloin:5:7: error:");
    expect_value(good, 42);
}

TEST(JitRunnerTest, ConcurrentContextsHaveIndependentResults) {
    auto run = [](int expected) {
        mlir::MLIRContext context;
        auto compiled =
            compile_source("def main() -> i32 { return " + std::to_string(expected) + "; }",
                           "concurrent.gloin", context, CompilationMode::Executable);
        if (!compiled.success())
            return false;
        auto result = JitRunner::run(*compiled.module);
        return result.success() && *result.value == expected;
    };
    auto first = std::async(std::launch::async, run, 17);
    auto second = std::async(std::launch::async, run, 93);
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
}

TEST(JitRunnerTest, IntegerArithmeticFailuresTrapInsteadOfReturningAValue) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    for (const std::string expression : {"1 / 0", "2147483647 + 1", "-2147483648 % -1"}) {
        const auto source = "def main() -> i32 { return " + expression + "; }";
        EXPECT_EXIT(run_trapping_source(source), is_arithmetic_trap, "");
    }
}

TEST(JitRunnerTest, FloatingArithmeticFailuresTrapInsteadOfReturningAValue) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    for (const std::string body : {"def x: f64 = 1.0 / -0.0;", "def x: f32 = 3.4e38 * 2.0;"}) {
        const auto source = "def main() -> i32 { " + body + " return 42; }";
        EXPECT_EXIT(run_trapping_source(source), is_arithmetic_trap, "");
    }
}
