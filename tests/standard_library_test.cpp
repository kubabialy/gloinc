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
def line: result<std.InputLine> = std.input(&memory,32);
if line.erroneous { return 1; } if line.value.end { return 1; }
def parsed: result<i32> = std.to_int(line.value.value);
if parsed.erroneous { return 2; }
def text: result<string> = std.to_string(&memory,parsed.value+1);
if text.erroneous { return 3; }
std.println(text.value); return 42;
)");
    expect_run(invoke({file}, source("41\r\n", "stdin")), 42, "number: 42\n");
    expect_success(invoke({"--check", file}, source("ignored", "stdin")), "");
}
TEST_F(StandardLibraryTest, IntegerParsingHandlesSignsBoundsAndLeadingZeroes) {
    auto file =
        program(R"(def min: result<i32> = std.to_int("-2147483648"); if min.erroneous { return 1; }
def max: result<i32> = std.to_int("+2147483647"); if max.erroneous { return 1; }
def zero: result<i32> = std.to_int("-000"); if zero.erroneous { return 1; }
def decimal: result<i32> = std.to_int("00042"); if decimal.erroneous { return 1; }
if min.value != -2147483648 || max.value != 2147483647 || zero.value != 0 || decimal.value != 42 { return 2; }
return 0;
)");
    expect_run(invoke({file}), 0);
}
TEST_F(StandardLibraryTest, InvalidAndOverflowResultsPreserveCodesWithoutReadablePayloads) {
    auto file = program(
        R"(def texts: [string;7] = {"", " 12", "12\0", "１２", "2147483648", "-2147483649", "999999999999999x"};
def codes: [i32;7] = {std.INVALID,std.INVALID,std.INVALID,std.INVALID,std.OVERFLOW,std.OVERFLOW,std.INVALID};
for def mut i: u64 = 0; i < 7; i = i + 1 {
    def r: result<i32> = std.to_int(texts[i]);
    if !r.erroneous { return 1; }
    if r.error.code != codes[i] || r.error.os_error != 0 { return 2; }
}
return 0;
)");
    expect_run(invoke({file}), 0);
}
TEST_F(StandardLibraryTest, FormattingIsExactAndDoesNotPrintImplicitly) {
    auto file = program(
        R"(def ignored: result<string> = std.to_string(&memory,99); if ignored.erroneous { return 1; }
def values: [i32;4] = {-2147483648,2147483647,0,-42};
for def mut i: u64 = 0; i < 4; i = i + 1 {
    def text: result<string> = std.to_string(&memory,values[i]);
    if text.erroneous { return 2; } std.println(text.value);
}
return 17;
)");
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
    auto file = program(R"(for def mut i: i32 = 0; i < 3; i = i + 1 {
    def line: result<std.InputLine> = std.input(&memory,8);
    if line.erroneous { return 1; } if line.value.end { return 1; }
    std.print("["); std.print(line.value.value); std.println("]");
}
for def mut i: i32 = 0; i < 2; i = i + 1 {
    def line: result<std.InputLine> = std.input(&memory,8);
    if line.erroneous { return 2; } if !line.value.end { return 2; }
    std.print(line.value.value);
}
return 0;
)");
    expect_run(invoke({file}, source("a\r\n\nlast", "stdin")), 0, "[a]\n[]\n[last]\n");
}
TEST_F(StandardLibraryTest, InputPreservesCountedBytesIncludingNulAndNonUtf8) {
    auto file = program(R"(def line: result<std.InputLine> = std.input(&memory,100);
if line.erroneous { return 1; } if line.value.end { return 1; }
std.print(line.value.value); return 0;
)");
    const std::string bytes = std::string("a\0b", 3) + " café λ " + char(0xff);
    expect_run(invoke({file}, source(bytes + "\n", "stdin")), 0, bytes);
}
TEST_F(StandardLibraryTest, ZeroLimitAcceptsEmptyLinesAndDrainsNonemptyLines) {
    auto file =
        program(R"(def a: result<std.InputLine> = std.input(&memory,0); if a.erroneous { return 1; }
if a.value.end { return 1; } std.print(a.value.value);
def b: result<std.InputLine> = std.input(&memory,0); if b.erroneous { return 2; }
if b.value.end { return 2; } std.print(b.value.value);
def c: result<std.InputLine> = std.input(&memory,0); if !c.erroneous { return 3; }
if c.error.code != std.TOO_LONG || c.error.os_error != 0 { return 3; }
def d: result<std.InputLine> = std.input(&memory,0); if d.erroneous { return 4; }
if !d.value.end { return 4; } std.print(d.value.value);
return 0;
)");
    expect_run(invoke({file}, source("\n\r\nabc", "stdin")), 0);
}
TEST_F(StandardLibraryTest, OversizedLineDoesNotPoisonTheNextRead) {
    auto file = program(R"(def first: result<std.InputLine> = std.input(&memory,3);
if !first.erroneous { return 1; }
if first.error.code != std.TOO_LONG { return 1; }
def next: result<std.InputLine> = std.input(&memory,3);
if next.erroneous { return 2; } if next.value.end { return 2; }
std.println(next.value.value); return 0;
)");
    expect_run(invoke({file}, source(std::string(100000, 'x') + "\r\nabc\r\n", "stdin")), 0,
               "abc\n");
}
TEST_F(StandardLibraryTest, ImpossibleAllocationReturnsErrorWithoutConsumingInput) {
    auto file = program(R"(def bad: result<std.InputLine> = std.input(&memory,18446744073709551615);
if !bad.erroneous { return 1; }
if bad.error.code != std.NO_MEMORY { return 1; }
def good: result<std.InputLine> = std.input(&memory,3);
if good.erroneous { return 2; } if good.value.end { return 2; }
std.println(good.value.value); return 0;
)");
    expect_run(invoke({file}, source("yes\n", "stdin")), 0, "yes\n");
}
TEST_F(StandardLibraryTest, InputIoErrorIsRecoverableWithoutClaimingNativeErrno) {
    const auto folder = directory + "/unreadable-stream";
    std::filesystem::create_directory(folder);
    auto file = program(R"(def line: result<std.InputLine> = std.input(&memory,16);
if !line.erroneous { return 1; }
if line.error.code != std.IO_ERROR || line.error.os_error != 0 { return 1; }
return 0;
)");
    expect_run(invoke({file}, folder), 0);
}
TEST_F(StandardLibraryTest, StringsSurviveGrowthAndIndependentArenaReset) {
    auto file = program(R"(def first_result: result<string> = std.to_string(&memory,-2147483648);
if first_result.erroneous { return 1; }
def first: string = first_result.value;
def mut other: arena.GeneralArena = arena.GeneralArena.create(); defer other.free();
for def mut i: i32 = 0; i < 10000; i = i + 1 {
    def text: result<string> = std.to_string(&memory,i); if text.erroneous { return 2; }
    def parsed: result<i32> = std.to_int(text.value); if parsed.erroneous { return 3; }
    if parsed.value != i { return 3; }
    def transient: result<string> = std.to_string(&other,i); if transient.erroneous { return 4; }
    other.reset();
}
std.println(first); memory.reset();
def fresh: result<string> = std.to_string(&memory,42); if fresh.erroneous { return 5; }
std.println(fresh.value); return 0;
)");
    expect_run(invoke({file}), 0, "-2147483648\n42\n");
}
TEST_F(StandardLibraryTest, DeferCapturesAllocatedStringsBeforeCleanupAndFree) {
    auto file = program(R"(for def mut i: i32 = 1; i < 4; i = i + 1 {
    def text: result<string> = std.to_string(&memory,i); if text.erroneous { return 1; }
    defer std.println(text.value);
}
return 42;
)");
    expect_run(invoke({file}), 42, "3\n2\n1\n");
}
TEST_F(StandardLibraryTest, ClearedArenaTrapsOnFormattingInputAndByteAllocation) {
    for (const std::string call :
         {"def r: result<string> = std.to_string(&memory, 42); if r.erroneous { return 1; }",
          "def r: result<std.InputLine> = std.input(&memory, 16); if r.erroneous { return 1; }",
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
    auto ir = invoke(
        {"--emit-llvm",
         program(
             R"(def text: result<string> = std.to_string(&memory,42); if text.erroneous { return 1; }
def value: result<i32> = std.to_int(text.value); if value.erroneous { return 1; }
return value.value;
)")});
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
               prompt + "conversion error: signed decimal i32 is out of range\n");
    expect_run(invoke({example}, source("oops\n", "stdin")), 3,
               prompt + "conversion error: invalid signed decimal i32\n");
    expect_run(invoke({example}, source(std::string(129, '1') + "\n", "stdin")), 2,
               prompt + "input error: input line exceeds byte limit\n");
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
    auto file = program(
        R"(def outcome: result<std.InputLine> = std.input(&memory, 10); if !outcome.erroneous { return 1; } if outcome.error.code != std.NO_MEMORY { return 1; } return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
    expect_run(invoke({"--stdlib-dir", directory, gloin_test::standard_example}), 2,
               "Enter decimal i32 values, one per line; EOF finishes:\ninput error: cannot "
               "allocate input buffer\n");
    file = program("def r: result<string> = std.to_string(&memory, 42); if !r.erroneous { return "
                   "1; } if r.error.code != std.NO_MEMORY { return 2; } return 0;");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
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

TEST_F(StandardLibraryTest, ResultsRequireHandlingAndInputPayloadNeedsSuccess) {
    for (const std::string body :
         {"std.input(&memory,8);", "std.to_int(\"42\");", "std.to_string(&memory,42);",
          "def r: result<std.InputLine> = std.input(&memory,8); def line: std.InputLine = r.value;",
          "def r: result<std.InputLine> = std.input(&memory,8); if r.erroneous { "
          "std.print(r.value.value); }",
          "def r: result<string> = std.to_string(&memory,42); if r.erroneous { return 1; } "
          "std.print(r.error.message);",
          "def r: std.InputResult = std.input(&memory,8);",
          "def r: std.IntResult = std.to_int(\"42\");"})
        rejects(body + " return 0;");
}

TEST_F(StandardLibraryTest, InputEndAndAliasesUseGloinIRInJitAndNativeModes) {
    const auto file = program(R"(
        def impossible: result<std.InputLine> = std.input(&memory,18446744073709551615);
        if !impossible.erroneous { return 1; }
        if impossible.error.code != std.NO_MEMORY || impossible.error.os_error != 0
            || !strings.equal(impossible.error.message,"input buffer size is not representable") { return 2; }
        def first: result<std.InputLine> = std.input(&memory,8);
        if first.erroneous { return 3; }
        if first.value.end || !strings.is_empty(first.value.value) { return 4; }
        def second: result<std.InputLine> = std.input(&memory,8);
        if second.erroneous { return 5; }
        if second.value.end || !strings.equal(second.value.value,"42\0") { return 6; }
        for def mut i: i32 = 0; i < 2; i = i + 1 {
            def ended: result<std.InputLine> = std.input(&memory,8);
            if ended.erroneous { return 7; }
            if !ended.value.end || !strings.is_empty(ended.value.value) { return 8; }
        }
        def parsed: result<i32> = std.parse_i32_checked("2147483648");
        if !parsed.erroneous { return 9; }
        if parsed.error.code != std.OVERFLOW || parsed.error.os_error != 0
            || !strings.equal(parsed.error.message,"signed decimal i32 is out of range") { return 10; }
        def text: result<string> = std.to_string(&memory,42);
        if text.erroneous { return 11; }
        if !strings.equal(text.value,"42") { return 12; }
        return 42;
    )",
                              "import \"@strings\";");
    const auto input = source(std::string("\n42\0", 4), "stdin");
    const auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation :
         {"gloin.result_success", "gloin.result_failure", "gloin.struct_literal"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({file}, input), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/std-input-results";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        const std::optional<llvm::StringRef> redirects[] = {input, std::nullopt, std::nullopt};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10),
                  42);
    }
}
