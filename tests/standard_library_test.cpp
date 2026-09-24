#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
class StandardLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body, const std::string &extra = "") {
        return source("import \"@std\"; import \"@arena\"; " + extra +
                      " def main() -> i32 { def mut memory: arena.GeneralArena = "
                      "arena.GeneralArena.create(); defer memory.free(); " +
                      body + " }");
    }
    void rejects(const std::string &body) {
        auto file = program(body);
        for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
};
} // namespace

TEST_F(StandardLibraryTest, DocumentedPromptInputParseAndFormat) {
    auto file = program(R"(std.print("number: ");
        def line: std.InputResult = std.input(&memory, 32);
        if line.status != std.OK { return 1; }
        def parsed: std.IntResult = std.to_int(line.value);
        if parsed.status != std.OK { return 2; }
        std.println(std.to_string(&memory, parsed.value + 1)); return 42;)");
    expect_run(invoke({file}, source("41\r\n", "stdin")), 42, "number: 42\n");
    expect_success(invoke({"--check", file}, source("ignored", "stdin")), "");
}
TEST_F(StandardLibraryTest, IntegerParsingHandlesSignsBoundsAndLeadingZeroes) {
    auto file = program(R"(
        def min: std.IntResult = std.to_int("-2147483648");
        def max: std.IntResult = std.to_int("+2147483647");
        def zero: std.IntResult = std.to_int("-000");
        def decimal: std.IntResult = std.to_int("00042");
        if min.status != std.OK || min.value != -2147483648 || max.status != std.OK
            || max.value != 2147483647 || zero.status != std.OK || zero.value != 0
            || decimal.status != std.OK || decimal.value != 42 { return 1; }
        return 0;)");
    expect_run(invoke({file}), 0);
}
TEST_F(StandardLibraryTest, InvalidAndOverflowResultsAreExplicitAndZeroValued) {
    auto file = program(R"(
        def empty: std.IntResult = std.to_int("");
        def whitespace: std.IntResult = std.to_int(" 12");
        def nul: std.IntResult = std.to_int("12\0");
        def unicode: std.IntResult = std.to_int("１２");
        def high: std.IntResult = std.to_int("2147483648");
        def low: std.IntResult = std.to_int("-2147483649");
        def junk: std.IntResult = std.to_int("999999999999999x");
        if empty.status != std.INVALID || whitespace.status != std.INVALID
            || nul.status != std.INVALID || unicode.status != std.INVALID
            || high.status != std.OVERFLOW || low.status != std.OVERFLOW
            || junk.status != std.INVALID { return 1; }
        if empty.value != 0 || whitespace.value != 0 || nul.value != 0
            || unicode.value != 0 || high.value != 0 || low.value != 0 || junk.value != 0 { return 2; }
        return 0;)");
    expect_run(invoke({file}), 0);
}
TEST_F(StandardLibraryTest, FormattingIsExactAndDoesNotPrintImplicitly) {
    auto file = program(R"(def ignored: string = std.to_string(&memory, 99);
        std.println(std.to_string(&memory, -2147483648));
        std.println(std.to_string(&memory, 2147483647));
        std.println(std.to_string(&memory, 0));
        std.println(std.to_string(&memory, -42)); return 17;)");
    expect_run(invoke({file}), 17, "-2147483648\n2147483647\n0\n-42\n");
}
TEST_F(StandardLibraryTest, NoImplicitNumericFormattingOrWrongArgumentTypes) {
    for (const std::string body :
         {"std.println(42); return 0;", "std.to_string(42); return 0;",
          "std.to_string(&memory, 1.0); return 0;", "std.to_string(&memory, true); return 0;",
          "def n: i64 = 42; std.to_string(&memory, n); return 0;", "std.to_int(42); return 0;",
          "def n: i32 = std.to_int(\"42\"); return n;", "std.input(); return 0;",
          "std.input(&memory); return 0;", "std.input(&memory, -1); return 0;",
          "def n: i32 = 12; std.input(&memory, n); return 0;",
          "std.to_string(memory, 42); return 0;"})
        rejects(body);
}
TEST_F(StandardLibraryTest, InputHandlesCrLfEmptyLinesFinalLineAndRepeatedEof) {
    auto file = program(R"(
        for def mut i: i32 = 0; i < 3; i = i + 1 {
            def line: std.InputResult = std.input(&memory, 8);
            if line.status != std.OK { return 1; }
            std.print("["); std.print(line.value); std.println("]");
        }
        for def mut i: i32 = 0; i < 2; i = i + 1 {
            def line: std.InputResult = std.input(&memory, 8);
            if line.status != std.END { return 2; } std.print(line.value);
        } return 0;)");
    expect_run(invoke({file}, source("a\r\n\nlast", "stdin")), 0, "[a]\n[]\n[last]\n");
}
TEST_F(StandardLibraryTest, InputPreservesCountedBytesIncludingNulAndNonUtf8) {
    auto file = program(R"(def line: std.InputResult = std.input(&memory, 100);
        if line.status != std.OK { return 1; } std.print(line.value); return 0;)");
    const std::string bytes = std::string("a\0b", 3) + " café λ " + char(0xff);
    expect_run(invoke({file}, source(bytes + "\n", "stdin")), 0, bytes);
}
TEST_F(StandardLibraryTest, ZeroLimitAcceptsEmptyLinesAndDrainsNonemptyLines) {
    auto file = program(R"(
        def a: std.InputResult = std.input(&memory, 0);
        def b: std.InputResult = std.input(&memory, 0);
        def c: std.InputResult = std.input(&memory, 0);
        def d: std.InputResult = std.input(&memory, 0);
        if a.status != std.OK || b.status != std.OK || c.status != std.TOO_LONG
            || d.status != std.END { return 1; }
        std.print(a.value); std.print(b.value); std.print(c.value); std.print(d.value);
        return 0;)");
    expect_run(invoke({file}, source("\n\r\nabc", "stdin")), 0);
}
TEST_F(StandardLibraryTest, OversizedLineDoesNotPoisonTheNextRead) {
    auto file = program(R"(def first: std.InputResult = std.input(&memory, 3);
        if first.status != std.TOO_LONG { return 1; }
        std.print(first.value);
        def next: std.InputResult = std.input(&memory, 3);
        if next.status != std.OK { return 2; } std.println(next.value); return 0;)");
    expect_run(invoke({file}, source(std::string(100000, 'x') + "\r\nabc\r\n", "stdin")), 0,
               "abc\n");
}
TEST_F(StandardLibraryTest, ImpossibleAllocationReturnsStatusWithoutConsumingInput) {
    auto file = program(R"(def bad: std.InputResult = std.input(&memory, 18446744073709551615);
        if bad.status != std.NO_MEMORY { return 1; } std.print(bad.value);
        def good: std.InputResult = std.input(&memory, 3);
        if good.status != std.OK { return 2; } std.println(good.value); return 0;)");
    expect_run(invoke({file}, source("yes\n", "stdin")), 0, "yes\n");
}
TEST_F(StandardLibraryTest, InputIoErrorIsRecoverableStatus) {
    const auto folder = directory + "/unreadable-stream";
    std::filesystem::create_directory(folder);
    auto file = program(R"(def line: std.InputResult = std.input(&memory, 16);
        if line.status != std.IO_ERROR { return 1; } std.print(line.value); return 0;)");
    expect_run(invoke({file}, folder), 0);
}
TEST_F(StandardLibraryTest, StringsSurviveGrowthAndIndependentArenaReset) {
    auto file = program(R"(def first: string = std.to_string(&memory, -2147483648);
        def mut other: arena.GeneralArena = arena.GeneralArena.create(); defer other.free();
        for def mut i: i32 = 0; i < 10000; i = i + 1 {
            def text: string = std.to_string(&memory, i);
            def parsed: std.IntResult = std.to_int(text);
            if parsed.status != std.OK || parsed.value != i { return 1; }
            def transient: string = std.to_string(&other, i); other.reset();
        }
        std.println(first);
        memory.reset();
        def fresh: string = std.to_string(&memory, 42); std.println(fresh); return 0;)");
    expect_run(invoke({file}), 0, "-2147483648\n42\n");
}
TEST_F(StandardLibraryTest, DeferCapturesAllocatedStringsBeforeCleanupAndFree) {
    auto file = program(R"(for def mut i: i32 = 1; i < 4; i = i + 1 {
        defer std.println(std.to_string(&memory, i));
    } return 42;)");
    expect_run(invoke({file}), 42, "3\n2\n1\n");
}
TEST_F(StandardLibraryTest, ClearedArenaTrapsOnFormattingInputAndByteAllocation) {
    for (const std::string call : {"std.to_string(&memory, 42);", "std.input(&memory, 16);",
                                   "memory.alloc_bytes(1);", "memory.try_alloc_bytes(1);"}) {
        auto result = invoke({program("memory.free(); " + call + " return 0;")});
        EXPECT_EQ(result.status, -2) << result.err;
        EXPECT_TRUE(result.out.empty());
    }
}
TEST_F(StandardLibraryTest, ByteAllocationInitializesReusedMemoryAndRejectsImpossibleSize) {
    auto file = program(R"(def a: *u8 = memory.alloc_bytes(1);
        if *a != 0 { return 1; } *a = 255;
        memory.reset(); def b: *u8 = memory.alloc_bytes(1);
        if *b != 0 { return 2; }
        def empty: *u8 = memory.alloc_bytes(0);
        if empty == null { return 3; }
        if memory.try_alloc_bytes(18446744073709551615) != null { return 4; }
        return 0;)");
    expect_run(invoke({file}), 0);
}
TEST_F(StandardLibraryTest, NativePrimitivesArePrivateAndCannotBeRedeclared) {
    rejects("def mut n: i32 = 0; __std_parse_i32(\"42\", &n); return 0;");
    source("def pub bad() -> string { return __std_string_view(null, 0); }", "local.gloin");
    expect_error(invoke({source("import \"./local\"; def main() -> i32 { return 0; }")}), 1,
                 "Undefined variable");
    source("def __std_parse_i32() -> i32 { return 42; }", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@std\"; def main() -> i32 { return 0; }")}),
                 1, "Cannot redeclare");
}
TEST_F(StandardLibraryTest, NativeSymbolNamesDoNotCollideWithApplicationFunctions) {
    auto file =
        program("return gloin_std_parse_i32() + gloin_std_input() + gloin_std_format_i32();",
                "def gloin_std_parse_i32() -> i32 { return 10; } "
                "def gloin_std_input() -> i32 { return 12; } "
                "def gloin_std_format_i32() -> i32 { return 20; }");
    expect_run(invoke({file}), 42);
}
TEST_F(StandardLibraryTest, JitRejectsMalformedStandardRuntimeAbis) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const std::string declaration :
         {"llvm.func @gloin_std_parse_i32(!llvm.ptr, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_parse_i32(!llvm.ptr, i64, !llvm.ptr, ...) -> i32",
          "llvm.func @gloin_std_format_i32(i32, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_input(!llvm.ptr, i64) -> i32",
          "llvm.func @gloin_arena_zero_bytes(!llvm.ptr, i32)",
          "llvm.func @gloin_std_parse_i32(%a: !llvm.ptr, %b: i64, %c: !llvm.ptr) -> i32 { %v = "
          "llvm.mlir.constant(0 : i32) : i32 llvm.return %v : i32 }"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + declaration +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        auto executed = JitRunner::run(*module);
        EXPECT_FALSE(executed.success());
    }
}
TEST_F(StandardLibraryTest, LibraryDefinitionsRemainOrdinaryReplaceableSource) {
    source("def pub to_int(value: string) -> i32 { return 42; }", "std.gloin");
    expect_run(
        invoke({"--stdlib-dir", directory,
                source("import \"@std\"; def main() -> i32 { return std.to_int(\"anything\"); }")}),
        42);
}
TEST_F(StandardLibraryTest, ExternalLlvmExecutionLoadsActualStandardRuntime) {
    auto ir = invoke({"--emit-llvm", program(R"(def text: string = std.to_string(&memory, 42);
        def value: std.IntResult = std.to_int(text);
        if value.status != std.OK { return 1; } return value.value;)")});
    ASSERT_EQ(ir.status, 0) << ir.err;
    const char *runtime = std::getenv("GLOIN_TEST_ARENA_RUNTIME");
    gloin_test::ToolCommand runner{
        gloin_test::mlir_runner,
        {std::string("--shared-libs=") + (runtime ? runtime : gloin_test::arena_runtime)}};
#ifdef __APPLE__
    if (auto *asan = dlsym(RTLD_DEFAULT, "__asan_init")) {
        Dl_info info{};
        ASSERT_NE(dladdr(asan, &info), 0);
        std::string preload = info.dli_fname;
        if (const auto *existing = std::getenv("DYLD_INSERT_LIBRARIES"); existing && *existing)
            preload += std::string(":") + existing;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

TEST_F(StandardLibraryTest, PackagedExampleChecksOutputAndConversionErrors) {
    auto example = std::string(gloin_test::standard_example);
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example = (std::filesystem::path(binary).parent_path() /
                   "../share/gloinc/examples/standard_library.gloin")
                      .string();
    const std::string prompt = "Enter decimal i32 values, one per line; EOF finishes:\n";
    expect_run(invoke({example}, source("10\r\n-3\n+35", "stdin")), 0,
               prompt + "accepted: 10\naccepted: -3\naccepted: 35\ncount: 3\nsum: 42\n");
    expect_run(invoke({example}, source("", "stdin")), 0, prompt + "count: 0\nsum: 0\n");
    expect_run(invoke({example}, source("2147483648\n", "stdin")), 3,
               prompt + "conversion error: 3\n");
    expect_run(invoke({example}, source("oops\n", "stdin")), 3, prompt + "conversion error: 2\n");
    expect_run(invoke({example}, source(std::string(129, '1') + "\n", "stdin")), 2,
               prompt + "input error: 5\n");
}
TEST_F(StandardLibraryTest, WrongNativePrimitiveArgumentsFailBeforeCodegen) {
    for (const std::string body : {"__std_parse_i32(42);", "__std_format_i32(42, null, null);",
                                   "__std_input(null, 10, null);", "__std_string_view(42, 1);"}) {
        source("def pub bad() -> void { " + body + " }", "std.gloin");
        auto file = source("import \"@std\"; def main() -> i32 { return 0; }");
        for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({"--stdlib-dir", directory, mode, file}), 1, "error:");
    }
}

TEST_F(StandardLibraryTest, AllocationFailureHasDefinedInputAndFormattingPolicies) {
    const char *override_binary = std::getenv("GLOIN_TEST_CLI");
    auto binary = std::filesystem::path(override_binary ? override_binary : gloin_test::gloinc);
    auto std_file = binary.parent_path() / "stdlib/std.gloin";
    if (!std::filesystem::exists(std_file))
        std_file = binary.parent_path() / "../share/gloinc/stdlib/std.gloin";
    source(read(std_file.string()), "std.gloin");
    source(read((std_file.parent_path() / "status.gloin").string()), "status.gloin");
    source(R"(def pub struct GeneralArena {
        def pub static create() -> GeneralArena { return GeneralArena {}; }
        def pub free(self: &GeneralArena) -> void {}
        def pub reset(self: &GeneralArena) -> void {}
        def pub alloc_bytes(self: &GeneralArena, size: u64) -> *u8 { return null; }
        def pub try_alloc_bytes(self: &GeneralArena, size: u64) -> *u8 { return null; }
    })",
           "arena.gloin");
    auto file = program(R"(def result: std.InputResult = std.input(&memory, 10);
        if result.status != std.NO_MEMORY { return 1; } std.print(result.value); return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
    expect_run(invoke({"--stdlib-dir", directory, gloin_test::standard_example}), 2,
               "Enter decimal i32 values, one per line; EOF finishes:\ninput error: 6\n");
    file = program("std.to_string(&memory, 42); return 0;");
    EXPECT_EQ(invoke({"--stdlib-dir", directory, file}).status, -2);
}

TEST_F(StandardLibraryTest, NativeStringViewAllowsNullOnlyWhenEmpty) {
    auto file = source("import \"@std\"; def main() -> i32 { std.probe(); return 0; }");
    source("def pub probe() -> void { __write_stdout(__std_string_view(null, 0)); }", "std.gloin");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
    source("def pub probe() -> void { __write_stdout(__std_string_view(null, 1)); }", "std.gloin");
    auto result = invoke({"--stdlib-dir", directory, file});
    EXPECT_EQ(result.status, -2);
    EXPECT_TRUE(result.out.empty());
}
