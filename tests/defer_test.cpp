#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <csignal>
#include <cstdlib>
#include <sys/wait.h>

namespace {
class DeferTest : public gloin_test::CliFixture {};

// Instrument native allocation calls after production lowering, leaving the
// generated registration/cleanup control flow untouched.
void redirect_allocator(mlir::ModuleOp module, mlir::OpBuilder &builder, bool release) {
    module.walk([&](mlir::LLVM::CallOp call) {
        auto callee = call->getAttrOfType<mlir::FlatSymbolRefAttr>("callee");
        if (callee && (callee.getValue() == "malloc" || (release && callee.getValue() == "free")))
            call->setAttr("callee", mlir::FlatSymbolRefAttr::get(builder.getContext(),
                                                                 callee.getValue() == "malloc"
                                                                     ? "gloin.test.allocate"
                                                                     : "gloin.test.release"));
    });
}
} // namespace

TEST_F(DeferTest, CapturesAtRegistrationAndRunsLifoAfterReturnValue) {
    auto file = source(R"(
        import "@std";
        def result() -> i32 { std.print("return;"); return 42; }
        def main() -> i32 {
            def mut text: string = "first;";
            defer std.print(text);
            text = "second;";
            defer std.print(text);
            std.print("body;");
            return result();
        }
    )");
    expect_run(invoke({file}), 42, "body;return;second;first;");
    expect_success(invoke({"--check", file}), "");
    for (const std::string mode : {"--emit-ir", "--emit-llvm"}) {
        auto result = invoke({mode, file});
        EXPECT_EQ(result.status, 0) << result.err;
        EXPECT_TRUE(result.err.empty());
        EXPECT_NE(result.out.find("@malloc"), std::string::npos);
        EXPECT_NE(result.out.find("@free"), std::string::npos);
    }
}

TEST_F(DeferTest, UntakenBranchesUnlessAndZeroIterationLoopsRegisterNothing) {
    auto file = source(R"(
        import "@std";
        def bad() -> string { std.print("BAD"); return "BAD"; }
        def main() -> i32 {
            if false { defer std.print(bad()); } else { defer std.print("else;"); }
            unless true { defer std.print(bad()); }
            unless false { defer std.print("unless;"); }
            while false { defer std.print(bad()); }
            for ; false; { defer std.print(bad()); }
            std.print("body;"); return 42;
        }
    )");
    expect_run(invoke({file}), 42, "body;unless;else;");
}

TEST_F(DeferTest, MixedLoopSitesRetainValuesAfterScopesEnd) {
    auto file = source(R"(
        import "@std";
        def put(n: i32) -> void {
            if n == 0 { std.print("0"); }
            if n == 1 { std.print("1"); }
            if n == 2 { std.print("2"); }
        }
        def main() -> i32 {
            def mut i: i32 = 0;
            while i < 3 {
                def captured: i32 = i;
                defer put(captured);
                if i == 1 { defer std.print("X"); }
                i = i + 1;
            }
            for def mut j: i32 = 0; j < 2; j = j + 1 {
                defer put(j);
            }
            std.print("body;"); return 42;
        }
    )");
    expect_run(invoke({file}), 42, "body;102X10");
}

TEST_F(DeferTest, EarlyAndImplicitVoidReturnsDrainOnlyReachedRegistrations) {
    auto file = source(R"(
        import "@std";
        def work(early: bool) -> void {
            defer std.print("A");
            if early { defer std.print("B"); return; }
            defer std.print("C");
        }
        def main() -> i32 { work(true); std.print(";"); work(false); return 42; }
    )");
    expect_run(invoke({file}), 42, "BA;CA");
}

TEST_F(DeferTest, ReturnsFromNestedLoopsAndBothBranchArmsCleanUpOnce) {
    auto file = source(R"(
        import "@std";
        def work(left: bool) -> i32 {
            defer std.print("outer;");
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                defer std.print("loop;");
                while true {
                    if left { defer std.print("left;"); return 20; }
                    else { defer std.print("right;"); return 22; }
                }
            }
            return 0;
        }
        def main() -> i32 { return work(true) + work(false); }
    )");
    expect_run(invoke({file}), 42, "left;loop;outer;right;loop;outer;");
}

TEST_F(DeferTest, ReceiverAndArgumentsRunOnceInSourceOrderAtRegistration) {
    auto file = source(R"(
        import "@std";
        def struct P {
            def mut x: i32,
            def add(self: &P, a: i32, b: i32) -> void {
                std.print("M"); self.x = self.x + a + b;
            }
        }
        def receiver(p: &P) -> &P { std.print("R"); return p; }
        def arg(label: string, n: i32) -> i32 { std.print(label); return n; }
        def work(p: &P) -> void {
            defer receiver(p).add(arg("A", 20), arg("B", 21));
            std.print("body;");
        }
        def main() -> i32 { def mut p: P = P { x: 1 }; work(&p); return p.x; }
    )");
    expect_run(invoke({file}), 42, "RABbody;M");
}

TEST_F(DeferTest, PointerCapturesPreserveIdentityAndObserveLaterPointeeMutation) {
    auto file = source(R"(
        def store(out: &i32, value: &const i32) -> void { *out = *value; }
        def work(out: &i32) -> void {
            def mut first: i32 = 1;
            def mut other: i32 = 99;
            def mut pointer: &i32 = &first;
            defer store(out, pointer);
            pointer = &other;
            first = 42;
        }
        def main() -> i32 { def mut out: i32 = 0; work(&out); return out; }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(DeferTest, ReturnedScalarAndStructAreCopiedBeforeCleanupMutatesLocals) {
    auto file = source(R"(
        def struct P { def mut x: i32, }
        def change(p: &P) -> void { p.x = 99; }
        def scalar() -> i32 { def mut p: P = P { x: 20 }; defer change(&p); return p.x; }
        def aggregate() -> P { def mut p: P = P { x: 22 }; defer change(&p); return p; }
        def main() -> i32 { return scalar() + aggregate().x; }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(DeferTest, StructCaptureCopiesValuesIncludingStringAndEmptyFields) {
    auto file = source(R"(
        import "@std";
        def struct Empty {}
        def struct Payload { def mut text: string, def mut n: i64, def e: Empty, }
        def consume(p: Payload, out: &i64) -> void { std.print(p.text); *out = *out + p.n; }
        def work(out: &i64) -> void {
            for def mut i: i64 = 0; i < 3; i = i + 1 {
                def mut p: Payload = Payload { text: "saved;", n: i + 13, e: Empty {} };
                defer consume(p, out);
                p.text = "changed;"; p.n = 999;
            }
        }
        def main() -> i32 { def mut out: i64 = 0; work(&out); if out == 42 { return 42; } return 1; }
    )");
    expect_run(invoke({file}), 42, "saved;saved;saved;");
}

TEST_F(DeferTest, CapturesEveryScalarTypeWithNativeAlignment) {
    auto file = source(R"(
        def check(a: i8, b: u8, c: i16, d: u16, e: i32, f: u32, g: i64, h: u64,
                  i: f32, j: f64, k: bool, out: &i32) -> void {
            if a == -128 && b == 255 && c == -32768 && d == 65535 &&
                e == -2147483648 && f == 4294967295 && g == -9223372036854775808 &&
                h == 18446744073709551615 && i == 1.25 && j == -2.5 && k { *out = 42; }
        }
        def work(out: &i32) -> void {
            defer check(-128, 255, -32768, 65535, -2147483648, 4294967295,
                        -9223372036854775808, 18446744073709551615, 1.25, -2.5, true, out);
        }
        def main() -> i32 { def mut out: i32 = 0; work(&out); return out; }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(DeferTest, RecursiveInvocationsAndDeferredCalleesHaveIndependentLogs) {
    auto file = source(R"(
        import "@std";
        def cleanup() -> void { defer std.print("inner;"); std.print("cleanup;"); }
        def recurse(n: i32) -> void {
            defer std.print("frame;");
            if n > 0 { recurse(n - 1); }
            defer cleanup();
        }
        def main() -> i32 { defer std.print("main;"); recurse(2); return 42; }
    )");
    expect_run(invoke({file}), 42,
               "cleanup;inner;frame;cleanup;inner;frame;cleanup;inner;frame;main;");
}

TEST_F(DeferTest, StaticMethodsAndNonVoidResultsCanBeDeferred) {
    auto file = source(R"(
        import "@std";
        def struct P {
            def static make() -> P { std.print("static;"); return P {}; }
            def read(self: &const P) -> i32 { std.print("instance;"); return 99; }
        }
        def ignored() -> i32 { std.print("function;"); return 255; }
        def main() -> i32 {
            def p: P = P {};
            defer ignored(); defer P.make(); defer p.read(); return 42;
        }
    )");
    expect_run(invoke({file}), 42, "instance;static;function;");
}

TEST_F(DeferTest, ModuleMethodsAndNativeOutputUseTheSameCapturePath) {
    source(R"(
        def pub struct P {
            def text: string,
            def pub static make() -> P { return P { text: "module;" }; }
            def pub flush(self: &const P) -> void { defer __write_stdout(self.text); }
        }
        def pub print(text: string) -> void { defer __write_stdout(text); }
    )",
           "log.gloin");
    auto file = source(R"(
        import "@log";
        def main() -> i32 {
            def p: log.P = log.P.make(); defer p.flush(); defer log.print("text;"); return 42;
        }
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42, "text;module;");
}

TEST_F(DeferTest, FatalTrapsDoNotUnwindPendingRegistrations) {
    for (const std::string body :
         {"defer std.print(\"BAD\"); std.print(\"body;\"); def n: i32 = 1 / 0; return n;",
          "defer std.print(\"BAD\"); std.print(\"body;\"); return 1 / 0;",
          "defer std.print(\"BAD\"); std.print(\"body;\"); defer consume(1 / 0); return 42;",
          "defer std.print(\"BAD\"); defer crash(); std.print(\"body;\"); return 42;"}) {
        SCOPED_TRACE(body);
        auto file = source("import \"@std\"; def consume(n: i32) -> void {} "
                           "def crash() -> void { def n: i32 = 1 / 0; } "
                           "def main() -> i32 { " +
                           body + " }");
        expect_success(invoke({"--check", file}), "");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err << result.message;
        EXPECT_EQ(result.out, "body;");
        EXPECT_TRUE(result.err.empty());
    }
}

TEST_F(DeferTest, NullableMethodReceiverIsCapturedWithoutPrematureDereference) {
    auto file = source(R"(
        import "@std";
        def struct P {
            def check(self: *const P) -> void { if self == null { std.print("null;"); } }
        }
        def main() -> i32 { def p: *const P = null; defer p.check(); std.print("body;"); return 42; }
    )");
    expect_run(invoke({file}), 42, "body;null;");
}

TEST_F(DeferTest, InvalidCallsAndCapturesFailBeforeExecutionInEveryMode) {
    for (const std::string body :
         {"defer 42;", "defer x;", "defer missing();", "defer consume();", "defer consume(true);",
          "def x: i32; defer consume(x);", "def p: P = P { x: 1 }; defer p.change();",
          "defer (P { x: 1 }).read();", "defer P.read();", "def p: P = P { x: 1 }; defer p.read;",
          "defer consume(consume(1));"}) {
        SCOPED_TRACE(body);
        auto file = source("def consume(n: i32) -> void {} "
                           "def struct P { def mut x: i32, "
                           "def change(self: &P) -> void { self.x = 2; } "
                           "def read(self: &const P) -> i32 { return self.x; } } "
                           "def main() -> i32 { def trap: i32 = 1 / 0; " +
                           body + " return 0; }");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(DeferTest, DeferredWritesDoNotInitializeVariablesAtRegistration) {
    auto file = source("def set(out: &i32) -> void { *out = 42; } "
                       "def main() -> i32 { def mut x: i32; defer set(&x); return x; }");
    expect_error(invoke({file}), 1, "uninitialized");
}

TEST_F(DeferTest, NativeAllocatorNamesDoNotCollideWithSourceFunctions) {
    auto file = source(R"(
        def malloc() -> i32 { return 20; }
        def free(out: &i32) -> void { *out = *out + 22; }
        def work(out: &i32) -> void { defer free(out); *out = malloc(); }
        def main() -> i32 { def mut out: i32 = 0; work(&out); return out; }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(DeferTest, ManyPendingLoopRegistrationsRetainReverseOrder) {
    auto file = source(R"(
        def consume(value: i64, expected: &i64, total: &i64) -> void {
            if value != *expected { def trap: i32 = 1 / 0; }
            *expected = *expected - 1;
            *total = *total + value;
        }
        def work(total: &i64) -> void {
            def mut expected: i64 = 99999;
            for def mut i: i64 = 0; i < 100000; i = i + 1 { defer consume(i, &expected, total); }
        }
        def main() -> i32 {
            def mut total: i64 = 0; work(&total);
            if total == 4999950000 { return 42; } return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(DeferTest, NativeExecutionAlsoWorksThroughExternalLLVMRunner) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def append(value: i32, out: &i32) -> void { *out = *out * 10 + value; }
        def work(out: &i32) -> i32 { defer append(2, out); defer append(4, out); return 99; }
        def main() -> i32 { def mut out: i32 = 0; work(&out); return out; }
    )",
                                   "external-defer.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    auto result = gloin_test::run_external_module(*compiled.module, {gloin_test::mlir_opt, {}},
                                                  {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

TEST_F(DeferTest, FunctionsWithoutDeferDoNotAllocateRegistrationStorage) {
    auto file = source("def main() -> i32 { return 42; }");
    auto result = invoke({"--emit-llvm", file});
    ASSERT_EQ(result.status, 0) << result.err;
    EXPECT_EQ(result.out.find("@malloc"), std::string::npos);
    EXPECT_EQ(result.out.find("@free"), std::string::npos);
    EXPECT_EQ(result.out.find("llvm.alloca"), std::string::npos);
}

TEST_F(DeferTest, JitRejectsMalformedAllocatorRuntimeDeclarations) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const std::string declaration :
         {"llvm.func @malloc(i32) -> !llvm.ptr", "llvm.func @free(!llvm.ptr) -> i64",
          "llvm.func @malloc(i64, ...) -> !llvm.ptr"}) {
        SCOPED_TRACE(declaration);
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + declaration +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(42 : i32) : i32 llvm.return "
                "%v : i32 } }",
            &context);
        ASSERT_TRUE(module);
        auto result = JitRunner::run(*module);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Execution);
    }
}

TEST_F(DeferTest, EveryRegistrationIsFreedAcrossRepeatedFunctionInvocations) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def consume(n: i64, total: &i64) -> void { *total = *total + n; }
        def work(total: &i64) -> void {
            defer consume(1, total);
            for def mut i: i64 = 0; i < 10; i = i + 1 { defer consume(i, total); }
        }
        def main() -> i32 {
            def mut total: i64 = 0;
            for def mut i: i32 = 0; i < 20; i = i + 1 { work(&total); }
            if total == 920 { return 42; } return 1;
        }
    )",
                                   "allocation-balance.gloin", context, CompilationMode::Executable,
                                   CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    mlir::OpBuilder builder(&context);
    redirect_allocator(*compiled.module, builder, true);
    auto main = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main");
    main->setAttr(mlir::SymbolTable::getSymbolAttrName(),
                  builder.getStringAttr("gloin.test.program"));
    auto helpers = mlir::parseSourceString<mlir::ModuleOp>(R"(
        module {
          llvm.func @malloc(i64) -> !llvm.ptr
          llvm.func @free(!llvm.ptr)
          llvm.func @gloin.test.program() -> i32
          llvm.mlir.global internal @gloin.test.pending(0 : i64) : i64
          llvm.mlir.global internal @gloin.test.count(0 : i64) : i64
          llvm.func @gloin.test.allocate(%size: i64) -> !llvm.ptr {
            %p = llvm.mlir.addressof @gloin.test.pending : !llvm.ptr
            %n = llvm.load %p : !llvm.ptr -> i64
            %one = llvm.mlir.constant(1 : i64) : i64
            %next = llvm.add %n, %one : i64
            llvm.store %next, %p : i64, !llvm.ptr
            %c = llvm.mlir.addressof @gloin.test.count : !llvm.ptr
            %old = llvm.load %c : !llvm.ptr -> i64
            %count = llvm.add %old, %one : i64
            llvm.store %count, %c : i64, !llvm.ptr
            %result = llvm.call @malloc(%size) : (i64) -> !llvm.ptr
            llvm.return %result : !llvm.ptr
          }
          llvm.func @gloin.test.release(%record: !llvm.ptr) {
            %p = llvm.mlir.addressof @gloin.test.pending : !llvm.ptr
            %n = llvm.load %p : !llvm.ptr -> i64
            %one = llvm.mlir.constant(1 : i64) : i64
            %next = llvm.sub %n, %one : i64
            llvm.store %next, %p : i64, !llvm.ptr
            llvm.call @free(%record) : (!llvm.ptr) -> ()
            llvm.return
          }
          llvm.func @main() -> i32 {
            %result = llvm.call @gloin.test.program() : () -> i32
            %p = llvm.mlir.addressof @gloin.test.pending : !llvm.ptr
            %n = llvm.load %p : !llvm.ptr -> i64
            %zero = llvm.mlir.constant(0 : i64) : i64
            %balanced = llvm.icmp "eq" %n, %zero : i64
            %c = llvm.mlir.addressof @gloin.test.count : !llvm.ptr
            %count = llvm.load %c : !llvm.ptr -> i64
            %expected = llvm.mlir.constant(220 : i64) : i64
            %complete = llvm.icmp "eq" %count, %expected : i64
            %ok = llvm.and %balanced, %complete : i1
            %failure = llvm.mlir.constant(2 : i32) : i32
            %value = llvm.select %ok, %result, %failure : i1, i32
            llvm.return %value : i32
          }
        }
    )",
                                                           &context);
    ASSERT_TRUE(helpers);
    for (auto &operation : helpers->getBody()->getOperations()) {
        if (auto function = llvm::dyn_cast<mlir::LLVM::LLVMFuncOp>(operation);
            function && function.isExternal())
            continue;
        compiled.module->getBody()->push_back(operation.clone());
    }
    auto result = JitRunner::run(*compiled.module);
    ASSERT_TRUE(result.success());
    EXPECT_EQ(result.value, 42);
}

TEST_F(DeferTest, AllocationFailureTrapsBeforeLinkingOrInvokingCleanup) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def noop() -> void {} def main() -> i32 { defer noop(); return 42; }",
        "allocation-failure.gloin", context, CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    mlir::OpBuilder builder(&context);
    redirect_allocator(*compiled.module, builder, false);
    auto helper = mlir::parseSourceString<mlir::ModuleOp>(R"(
        module {
          llvm.func @gloin.test.allocate(%size: i64) -> !llvm.ptr {
            %null = llvm.mlir.zero : !llvm.ptr
            llvm.return %null : !llvm.ptr
          }
        }
    )",
                                                          &context);
    ASSERT_TRUE(helper);
    compiled.module->getBody()->push_back(helper->getBody()->front().clone());
    EXPECT_EXIT(
        {
            auto result = JitRunner::run(*compiled.module);
            std::_Exit(result.success() ? 0 : 1);
        },
        [](int status) {
            return WIFSIGNALED(status) &&
                   (WTERMSIG(status) == SIGTRAP || WTERMSIG(status) == SIGILL);
        },
        "");
}
