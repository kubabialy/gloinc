#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Parser/Parser.h"
#include "standard_runtime.h"
#include "support/cli_fixture.h"
#include <fcntl.h>
#include <unistd.h>

namespace {
class StandardModuleTest : public gloin_test::CliFixture {};
} // namespace

TEST_F(StandardModuleTest, WritesExactBytesIncludingNulUtf8AndPercentSigns) {
    auto file = source(R"(
        import "@std";
        def main() -> i32 {
            std.print("");
            std.print("A\0é%\t\\\"\'");
            std.println("");
            std.println("line\r\n");
            return -1;
        }
    )");
    std::string expected = "A";
    expected.push_back('\0');
    expected += "é%\t\\\"'\nline\r\n\n";
    expect_run(invoke({file}), -1, expected);
}

TEST_F(StandardModuleTest, CallsEvaluateOnceInOrderAndRespectControlFlow) {
    auto file = source(R"(
        import "@std";
        def message() -> string { std.print("first:"); return "value"; }
        def echo(s: string) -> string { return s; }
        def main() -> i32 {
            def const TEXT: string = "constant";
            std.println(message());
            def mut s: string = echo(TEXT);
            for def mut i: i32 = 0; i < 2; i = i + 1 {
                std.print(s);
                s = "second";
            }
            if false { std.println("wrong"); }
            unless false { std.println("!"); }
            return 42;
        }
    )");
    expect_run(invoke({file}), 42, "first:value\nconstantsecond!\n");
}

TEST_F(StandardModuleTest, ImportIsFileScopedAndDoesNotAffectReturnValue) {
    auto file = source(R"(
        def helper() -> void { std.println("ok"); }
        def main() -> i32 { helper(); return 17; }
        import "@std";
    )");
    expect_run(invoke({file}), 17, "ok\n");
    auto silent = source("import \"@std\"; def main() -> i32 { return 42; }");
    expect_run(invoke({silent}), 42);
}

TEST_F(StandardModuleTest, MissingModulesMembersAndInvalidCallsFailBeforeExecution) {
    for (const std::string text :
         {"import \"missing\"; def main() -> i32 { return 0; }",
          "import \"@std/io\"; def main() -> i32 { return 0; }",
          "import \"#package\"; def main() -> i32 { return 0; }",
          "import \"\"; def main() -> i32 { return 0; }",
          "def main() -> i32 { std.println(\"bad\"); return 0; }",
          "import \"@std\"; def main() -> i32 { std.missing(\"bad\"); return 0; }",
          "import \"@std\"; def main() -> i32 { std.println(); return 0; }",
          "import \"@std\"; def main() -> i32 { std.print(\"a\", \"b\"); return 0; }",
          "import \"@std\"; def main() -> i32 { std.println(42); return 0; }",
          "import \"@std\"; def main() -> i32 { return std.println(\"bad\"); }",
          "import \"@std\"; def main() -> i32 { def x: string = std.print(\"bad\"); return 0; }",
          "import \"@std\"; def main() -> i32 { std.print; return 0; }",
          "import \"@std\"; def main() -> i32 { def x: string; std.print(x); return 0; }"}) {
        SCOPED_TRACE(text);
        auto file = source(text);
        for (const std::string mode : {"--run", "--check", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(StandardModuleTest, DuplicateImportsAndNamespaceConflictsAreRejected) {
    for (const std::string text :
         {"import \"@std\"; import \"@std\"; def main() -> i32 { return 0; }",
          "import \"@std\"; def std() -> void {} def main() -> i32 { return 0; }",
          "def const std: i32 = 0; import \"@std\"; def main() -> i32 { return 0; }",
          "import \"@std\"; def main() -> i32 { def std: i32 = 1; std.print(\"bad\"); return 0; }",
          "def main() -> i32 { import \"@std\"; return 0; }"}) {
        SCOPED_TRACE(text);
        expect_error(invoke({source(text)}), 1, "error:");
    }
    expect_success(invoke({source("import \"@std\"; def main() -> i32 { "
                                  "{ def std: i32 = 1; } std.print(\"ok\"); return 0; }")}),
                   "ok");
}

TEST_F(StandardModuleTest, CheckAndIrEmissionNeverExecuteOutput) {
    auto file = source("import \"@std\"; def main() -> i32 { "
                       "std.println(\"never printed\"); return 1 / 0; }");
    expect_success(invoke({"--check", file}), "");
    for (const std::string mode : {"--emit-ir", "--emit-llvm"}) {
        auto result = invoke({mode, file});
        ASSERT_EQ(result.status, 0) << result.err;
        EXPECT_TRUE(result.err.empty());
        mlir::MLIRContext context;
        CodeGen dialects(context);
        auto module = mlir::parseSourceString<mlir::ModuleOp>(result.out, &context);
        ASSERT_TRUE(module);
        auto runtime = module->lookupSymbol<mlir::LLVM::LLVMFuncOp>(standard_output_symbol);
        ASSERT_TRUE(runtime);
        EXPECT_EQ(runtime.getFunctionType().getNumParams(), 2u);
    }
}

TEST_F(StandardModuleTest, SemanticResolutionAndRuntimeDoNotLeakAcrossCompilations) {
    mlir::MLIRContext context;
    for (unsigned i = 0; i < 2; ++i) {
        auto result = compile_source("import \"@std\"; def main() -> i32 { "
                                     "std.print(\"\"); return -1; }",
                                     "std.gloin", context);
        ASSERT_TRUE(result.success());
        auto executed = JitRunner::run(*result.module);
        ASSERT_TRUE(executed.success());
        EXPECT_EQ(*executed.value, -1);
        auto missing = compile_source("def main() -> i32 { std.print(\"\"); return 0; }",
                                      "missing.gloin", context);
        EXPECT_FALSE(missing.success());
        EXPECT_EQ(missing.failed_stage, DiagnosticStage::Semantic);
    }
}

TEST_F(StandardModuleTest, JitRejectsWrongStandardRuntimeAbi) {
    mlir::MLIRContext context;
    CodeGen dialects(context);
    for (const std::string declaration :
         {"llvm.func @gloin.runtime.output(i32)",
          "llvm.func @gloin.runtime.output(%p: !llvm.ptr, %n: i64) { llvm.return }",
          "llvm.func @gloin.runtime.output(!llvm.ptr, i64, ...)"}) {
        SCOPED_TRACE(declaration);
        auto module = mlir::parseSourceString<mlir::ModuleOp>("module { " + declaration + R"(
            llvm.func @main() -> i32 {
                %zero = llvm.mlir.constant(0 : i32) : i32
                llvm.return %zero : i32
            }
        }
    )",
                                                              &context);
        ASSERT_TRUE(module);
        auto result = JitRunner::run(*module);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Execution);
    }
}

TEST_F(StandardModuleTest, OutputWriteFailureIsAnExecutionError) {
    ASSERT_EXIT(
        {
            // A read-only descriptor makes stdout writes fail without raising SIGPIPE.
            int descriptor = open("/dev/null", O_RDONLY);
            if (descriptor < 0 || dup2(descriptor, STDOUT_FILENO) < 0)
                _exit(10);
            close(descriptor);
            mlir::MLIRContext context;
            auto compiled = compile_source("import \"@std\"; def main() -> i32 { "
                                           "std.print(\"write fails\"); return 0; }",
                                           "io.gloin", context);
            if (!compiled.success())
                _exit(11);
            auto result = JitRunner::run(*compiled.module);
            _exit(!result.success() && result.failed_stage == DiagnosticStage::Execution ? 0 : 12);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(StandardModuleTest, EscapedImportPathAndDelayedStringsAreChecked) {
    auto file = source(R"(
        import "@st\d";
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({file}), 1, "escape");
    auto valid = source(R"(
        import "@std";
        def main() -> i32 {
            def text: string;
            if true { text = "initialized"; } else { text = "wrong"; }
            std.println(text);
            return 0;
        }
    )");
    expect_success(invoke({valid}), "initialized\n");
}

TEST_F(StandardModuleTest, ModuleFileControlsSignaturesAndBehavior) {
    source(R"(
        def priv prefix() -> string { return "from file:"; }
        def pub println(value: string) -> void {
            __write_stdout(prefix());
            __write_stdout(value);
        }
    )",
           "std.gloin");
    auto file = source("import \"@std\"; def main() -> i32 { std.println(\"yes\"); return 7; }");
    expect_run(invoke({"--stdlib-dir", directory, file}), 7, "from file:yes");
    // Replacing the file changes both its signature and implementation without rebuilding C++.
    source("def pub println(value: i32) -> i32 { return value + 1; }", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "type mismatch");
    file = source("import \"@std\"; def main() -> i32 { return std.println(41); }");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
}

TEST_F(StandardModuleTest, NewLowercaseModuleFilesNeedNoCompilerSpecialCases) {
    source(R"(
        def const OFFSET: i32 = 2;
        def priv twice(value: i32) -> i32 { return value * 2; }
        def pub answer(value: i32) -> i32 { return twice(value) + OFFSET; }
    )",
           "math.gloin");
    source("def pub answer() -> i32 { return 10; }", "std.gloin");
    auto file = source(R"(
        import "@math";
        import "@std";
        def answer() -> i32 { return 1; }
        def main() -> i32 { return math.answer(15) + std.answer() + answer(); }
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 43);
    auto ir = invoke({"--stdlib-dir", directory, "--emit-llvm", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    EXPECT_NE(ir.out.find("gloin.module.math.answer"), std::string::npos);
    EXPECT_NE(ir.out.find("gloin.module.std.answer"), std::string::npos);
}

TEST_F(StandardModuleTest, MissingEmptyAndUnreadableModuleFilesNeverBecomeStubs) {
    auto file = source("import \"@std\"; def main() -> i32 { std.println(\"bad\"); return 0; }");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Cannot load module");
    auto library = source("", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Unknown or private member");
    source("def pub println(s: string) -> void {}", "std.gloin");
    ASSERT_FALSE(llvm::sys::fs::setPermissions(library, llvm::sys::fs::perms::no_perms));
    auto result = invoke({"--stdlib-dir", directory, file});
    EXPECT_FALSE(llvm::sys::fs::setPermissions(library, llvm::sys::fs::perms::owner_all));
    expect_error(result, 1, "Cannot read module");
}

TEST_F(StandardModuleTest, ModuleErrorsKeepTheirOwnSourceLocations) {
    auto file = source("import \"@std\"; def main() -> i32 { return 0; }");
    for (const std::string module :
         {"def pub broken() -> i32 { return 1 }", "def pub broken() -> i32 { return true; }",
          "def pub broken() -> void { __write_stdout(42); }",
          "def pub broken() -> void { __write_stdout(); }",
          "def pub __write_stdout(s: string) -> void {}", "import \"@math\";"}) {
        SCOPED_TRACE(module);
        auto library = source(module, "std.gloin");
        for (const std::string mode : {"--run", "--check", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({"--stdlib-dir", directory, mode, file}), 1, library + ":1:");
    }
}

TEST_F(StandardModuleTest, ModuleScopesAndPrivateFunctionsCannotLeak) {
    auto collision = source("import \"@std\"; def str_0() -> i32 { return 42; } "
                            "def main() -> i32 { std.println(\"ok\"); return str_0(); }");
    expect_run(invoke({collision}), 42, "ok\n");
    source("def priv hidden() -> i32 { return 9; } "
           "def pub answer() -> i32 { return hidden(); }",
           "std.gloin");
    for (const std::string expression : {"std.hidden()", "hidden()", "answer()"}) {
        auto file = source("import \"@std\"; def main() -> i32 { return " + expression + "; }");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "error:");
    }
    auto file = source("import \"@std\"; def caller() -> i32 { return 42; } "
                       "def main() -> i32 { return std.answer(); }");
    auto library = source("def pub answer() -> i32 { return caller(); }", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, library + ":1:");
    source("def pub main() -> i32 { return 42; }", "std.gloin");
    file = source("import \"@std\";");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Executable requires");
}

TEST_F(StandardModuleTest, NativeByteOutputIsOnlyAvailableInsideLibraryModules) {
    for (const std::string prefix : {"", "import \"@std\"; "}) {
        auto file = source(prefix + "def main() -> i32 { __write_stdout(\"bad\"); return 0; }");
        expect_error(invoke({file}), 1, "Undefined variable");
    }
    auto file = source("def __write_stdout() -> i32 { return 17; } "
                       "def main() -> i32 { return __write_stdout(); }");
    expect_run(invoke({file}), 17);
}

TEST_F(StandardModuleTest, LibraryDirectoryOptionIsExplicitAndDoesNotAffectScalarCode) {
    expect_error(invoke({"--stdlib-dir"}), 2, "requires one directory");
    expect_error(invoke({"--stdlib-dir", directory, "--stdlib-dir", directory}), 2, "only once");
    auto file = source("def main() -> i32 { return 42; }");
    expect_run(invoke({"--stdlib-dir", directory + "/missing", file}), 42);
    file = source("import \"@../std\"; def main() -> i32 { return 0; }");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Unsupported module path");
}
