#include "codegen.h"
#include "jit_runner.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
class StringLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body, const std::string &extra = "") {
        return source(
            "import \"@strings\"; import \"@status\"; import \"@arena\"; import \"@std\"; " +
            extra +
            " def main() -> i32 { def mut memory: arena.GeneralArena = "
            "arena.GeneralArena.create(); defer memory.free(); " +
            body + " }");
    }
    std::filesystem::path library() {
        const char *override_binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(override_binary ? override_binary : gloin_test::gloinc)
                        .parent_path();
        auto path = root / "stdlib";
        return std::filesystem::exists(path) ? path : root / "../share/gloinc/stdlib";
    }
    void copy_modules() {
        for (const auto *file : {"std.gloin", "strings.gloin", "arena.gloin", "status.gloin"})
            source(read((library() / file).string()), file);
    }
};
std::string literal(const std::string &bytes) {
    std::string result = "\"";
    for (char byte : bytes) {
        switch (byte) {
        case '\0':
            result += "\\0";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        default:
            result += byte;
        }
    }
    return result + "\"";
}
} // namespace

TEST_F(StringLibraryTest, SharedStatusesRetainCompatibilityAndDistinctBoundsFailure) {
    expect_run(invoke({program(R"(
        if std.OK != status.OK || std.END != status.END || std.INVALID != status.INVALID
            || std.OVERFLOW != status.OVERFLOW || std.IO_ERROR != status.IO_ERROR
            || std.TOO_LONG != status.TOO_LONG || std.NO_MEMORY != status.NO_MEMORY { return 1; }
        if status.OK != 0 || status.END != 1 || status.INVALID != 2 || status.OVERFLOW != 3
            || status.IO_ERROR != 4 || status.TOO_LONG != 5 || status.NO_MEMORY != 6
            || status.OUT_OF_RANGE != 7 { return 2; } return 0;)")}),
               0);
}

TEST_F(StringLibraryTest, LengthEqualityAndOrderingUseCountedUnsignedBytes) {
    auto file = program(R"(
        def line_result: result<std.InputLine> = std.input(&memory, 8);
        if line_result.erroneous { return 97; }
        def line: std.InputLine = line_result.value;
        if line.end || strings.byte_length(line.value) != 4 { return 1; }
        if strings.byte_length("café") != 5 || !strings.is_empty("") || strings.is_empty("\0") { return 2; }
        if !strings.equal("a\0b", "a\0b") || strings.equal("a\0b", "a\0c") { return 3; }
        if strings.compare("", "") != 0 || strings.compare("ab", "abc") != -1
            || strings.compare("abc", "ab") != 1 || strings.compare("b", "a") != 1 { return 4; }
        def high: result<string> = strings.slice_bytes(line.value, 1, 1);
        if high.erroneous { return 97; }
        def byte: result<u8> = strings.byte_at(line.value, 1);
        if byte.erroneous { return 97; }
        if high.erroneous || byte.erroneous || byte.value != 255 { return 5; }
        if strings.compare(high.value, "z") != 1 { return 6; }
        std.print(line.value); return 0;)");
    const std::string bytes = std::string("a") + char(255) + std::string("\0b", 2);
    expect_run(invoke({file}, source(bytes + "\n", "stdin")), 0, bytes);
}

TEST_F(StringLibraryTest, ByteAndSliceBoundariesNeverWrapAndFailuresPreserveCodes) {
    expect_run(invoke({program(R"(
        def first: result<u8> = strings.byte_at("abc", 0);
        if first.erroneous { return 97; }
        def last: result<u8> = strings.byte_at("abc", 2);
        if last.erroneous { return 97; }
        def empty: result<u8> = strings.byte_at("", 0);
        if !empty.erroneous { return 97; }
        def end: result<u8> = strings.byte_at("abc", 3);
        if !end.erroneous { return 97; }
        def huge: result<u8> = strings.byte_at("abc", 18446744073709551615);
        if !huge.erroneous { return 97; }
        if first.erroneous || first.value != 97 || last.value != 99 { return 1; }
        if empty.error.code != status.OUT_OF_RANGE || end.error.code != status.OUT_OF_RANGE
            || huge.error.code != status.OUT_OF_RANGE || empty.error.os_error != 0 || end.error.os_error != 0 || huge.error.os_error != 0 { return 2; }
        def all: result<string> = strings.slice_bytes("abc", 0, 3);
        if all.erroneous { return 97; }
        def tail: result<string> = strings.slice_bytes("abc", 3, 0);
        if tail.erroneous { return 97; }
        def none: result<string> = strings.slice_bytes("", 0, 0);
        if none.erroneous { return 97; }
        if all.erroneous || !strings.equal(all.value, "abc")
            || tail.erroneous || !strings.is_empty(tail.value)
            || none.erroneous { return 3; }
        def a: result<string> = strings.slice_bytes("abc", 4, 0);
        if !a.erroneous { return 97; }
        def b: result<string> = strings.slice_bytes("abc", 2, 2);
        if !b.erroneous { return 97; }
        def c: result<string> = strings.slice_bytes("abc", 18446744073709551615, 2);
        if !c.erroneous { return 97; }
        def d: result<string> = strings.slice_bytes("abc", 1, 18446744073709551615);
        if !d.erroneous { return 97; }
        if a.error.code != status.OUT_OF_RANGE || b.error.code != status.OUT_OF_RANGE
            || c.error.code != status.OUT_OF_RANGE || d.error.code != status.OUT_OF_RANGE { return 4; }
        if a.error.os_error != 0 || b.error.os_error != 0
            || c.error.os_error != 0 || d.error.os_error != 0 { return 5; }
        def utf8: result<string> = strings.slice_bytes("é", 1, 1);
        if utf8.erroneous { return 97; }
        def continuation: result<u8> = strings.byte_at(utf8.value, 0);
        if continuation.erroneous { return 97; }
        if continuation.value != 169 { return 6; } return 0;)")}),
               0);
}

TEST_F(StringLibraryTest, SearchDistinguishesZeroAbsentEmptyAndLastMatch) {
    expect_run(invoke({program(R"(
        def first: strings.FindResult = strings.find("ababa", "aba");
        def last: strings.FindResult = strings.find("aaab", "ab");
        def absent: strings.FindResult = strings.find("aaab", "ac");
        def empty: strings.FindResult = strings.find("", "");
        def longer: strings.FindResult = strings.find("a", "ab");
        def nul: strings.FindResult = strings.find("a\0b", "\0b");
        if !first.found || first.offset != 0 || !last.found || last.offset != 2 { return 1; }
        if absent.found || absent.offset != 0 || !empty.found || empty.offset != 0
            || longer.found || !nul.found || nul.offset != 1 { return 2; }
        if !strings.starts_with("", "") || !strings.ends_with("", "")
            || !strings.contains("abc", "") || strings.contains("", "x") { return 3; }
        if strings.starts_with("a", "ab") || strings.ends_with("a", "ab")
            || !strings.starts_with("abc", "ab") || !strings.ends_with("abc", "bc") { return 4; }
        return 0;)")}),
               0);
}

TEST_F(StringLibraryTest, TrimmingUsesExactlyAsciiWhitespaceAndPreservesInterior) {
    auto file = program(R"(
        def input_result: result<std.InputLine> = std.input(&memory, 32);
        if input_result.erroneous { return 97; }
        def input: std.InputLine = input_result.value;
        if input.end || !strings.equal(strings.trim_ascii(input.value), "a b") { return 1; }
        if !strings.equal(strings.trim_start_ascii(" \tx \r"), "x \r")
            || !strings.equal(strings.trim_end_ascii(" \tx \r"), " \tx") { return 2; }
        if !strings.is_empty(strings.trim_ascii(" \t\r\n")) || !strings.is_empty(strings.trim_ascii("")) { return 3; }
        if !strings.equal(strings.trim_ascii(" x "), " x ")
            || !strings.equal(strings.trim_ascii("\0x\0"), "\0x\0") { return 4; }
        return 0;)");
    expect_run(invoke({file}, source("\t\v\f\r a b \r\f\v\t\n", "stdin")), 0);
}

TEST_F(StringLibraryTest, DeterministicSearchAndOrderingAgreeWithIndependentOracle) {
    std::string body;
    uint32_t seed = 421;
    const std::string alphabet("ab \t\0", 5);
    auto word = [&](unsigned limit) {
        seed = seed * 1664525u + 1013904223u;
        const auto length = seed % limit;
        std::string result;
        for (unsigned i = 0; i < length; ++i) {
            seed = seed * 1664525u + 1013904223u;
            result += alphabet[seed % alphabet.size()];
        }
        return result;
    };
    for (unsigned i = 0; i < 64; ++i) {
        auto a = word(16), b = word(5);
        const auto at = a.find(b);
        const auto n = std::to_string(i);
        const auto compare = a.compare(b);
        body += "def r" + n + ": strings.FindResult = strings.find(" + literal(a) + ", " +
                literal(b) + ");";
        body += "if r" + n + ".found != " + (at != std::string::npos ? "true" : "false") + " || r" +
                n + ".offset != " + std::to_string(at == std::string::npos ? 0 : at) +
                " { return 1; }";
        body += "if strings.compare(" + literal(a) + ", " + literal(b) + ") != " +
                std::to_string(compare < 0   ? -1
                               : compare > 0 ? 1
                                             : 0) +
                " { return 2; }";
        body += "if strings.starts_with(" + literal(a) + ", " + literal(b) +
                ") != " + (a.starts_with(b) ? "true" : "false") + " { return 3; }";
        body += "if strings.ends_with(" + literal(a) + ", " + literal(b) +
                ") != " + (a.ends_with(b) ? "true" : "false") + " { return 4; }";
    }
    expect_run(invoke({program(body + "return 0;")}), 0);
}

TEST_F(StringLibraryTest, CopiedBytesOutliveSourceResetAndFreeIncludingNul) {
    expect_run(invoke({program(R"(
        def mut source: arena.GeneralArena = arena.GeneralArena.create();
        def original: result<string> = strings.copy(&source, " a\0b ");
        if original.erroneous { return 97; }
        if original.erroneous { return 1; }
        def kept: result<string> = strings.copy(&memory, strings.trim_ascii(original.value));
        if kept.erroneous { return 97; }
        if kept.erroneous { return 2; }
        source.reset();
        def overwrite: result<string> = strings.copy(&source, "xxxxx");
        if overwrite.erroneous { return 97; }
        source.free();
        if !strings.equal(kept.value, "a\0b") { return 3; }
        def same: result<string> = strings.copy(&memory, kept.value);
        if same.erroneous { return 97; }
        if same.erroneous || !strings.equal(same.value, kept.value) { return 4; }
        std.print(same.value); return 0;)")}),
               0, std::string("a\0b", 3));
}

TEST_F(StringLibraryTest, BorrowedViewsAndSearchRequireNoArenaAndCopiesFailExplicitly) {
    copy_modules();
    source(R"(def pub struct GeneralArena {
        def pub static create() -> GeneralArena { return GeneralArena {}; }
        def pub free(self: &GeneralArena) -> void {}
        def pub alloc_bytes(self: &GeneralArena, size: u64) -> *u8 { return null; }
        def pub try_alloc_bytes(self: &GeneralArena, size: u64) -> *u8 { return null; }
        def pub try_alloc(self: &GeneralArena, size: u64, alignment: u64) -> *u8 { return null; }
    })",
           "arena.gloin");
    auto file = program(R"(
        def text: string = strings.trim_ascii(" a=b ");
        def position: strings.FindResult = strings.find(text, "=");
        def key: result<string> = strings.slice_bytes(text, 0, position.offset);
        if key.erroneous { return 97; }
        def byte: result<u8> = strings.byte_at(text, 0);
        if byte.erroneous { return 97; }
        if !position.found || key.erroneous || !strings.equal(key.value, "a")
            || byte.value != 97 || strings.compare(key.value, "a") != 0 { return 1; }
        if !strings.starts_with(text, "a") || !strings.ends_with(text, "b")
            || !strings.contains(text, "=") || strings.byte_length(text) != 3 { return 2; }
        def copied: result<string> = strings.copy(&memory, text);
        if !copied.erroneous { return 97; }
        def empty: result<string> = strings.copy(&memory, "");
        if empty.erroneous { return 97; }
        if copied.error.code != status.NO_MEMORY || copied.error.os_error != 0
            || empty.erroneous || !strings.is_empty(empty.value) { return 3; }
        return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}

TEST_F(StringLibraryTest, EmptyCopyDoesNotInspectArenaButNonemptyCopyRequiresLiveHandle) {
    expect_run(invoke({program(
                   R"(memory.free(); def outcome: result<string> = strings.copy(&memory, "");
        if outcome.erroneous { return 97; }
        if outcome.erroneous || !strings.is_empty(outcome.value) { return 1; } return 0;)")}),
               0);
    EXPECT_EQ(invoke({program("memory.free(); def copied: result<string> = strings.copy(&memory, "
                              "\"x\"); if copied.erroneous { return 1; } return 0;")})
                  .status,
              -2);
}

TEST_F(StringLibraryTest, PublicArgumentsAndResultTypesAreCheckedInEveryMode) {
    for (const std::string body :
         {"strings.byte_at(\"abc\", -1);", "strings.slice_bytes(\"abc\", 0);",
          "strings.equal(1, 2);", "strings.copy(memory, \"a\");", "strings.find(\"a\", 97);",
          "def n: u8 = strings.byte_at(\"a\", 0);", "strings.matches_at(\"a\", \"a\", 0);"}) {
        const auto file = program(body + "return 0;");
        for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(StringLibraryTest, PrimitivesArePrivateToCanonicalStringsModule) {
    const auto body = "def pub probe() -> u64 { return __strings_length(\"x\"); }";
    source(body, "local.gloin");
    expect_error(invoke({source("import \"./local\"; def main() -> i32 { return 0; }")}), 1,
                 "Undefined variable");
    source(body, "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@std\"; def main() -> i32 { return 0; }")}),
                 1, "Undefined variable");
    source("def __strings_length() -> u64 { return 0; }", "strings.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@strings\"; def main() -> i32 { return 0; }")}),
                 1, "Cannot redeclare");
    source("def struct __strings_length {}", "strings.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@strings\"; def main() -> i32 { return 0; }")}),
                 1, "reserved struct name");
}

TEST_F(StringLibraryTest, PrivatePrimitiveArgumentsAreCheckedAndBoundsTrapBeforeAccess) {
    const auto file =
        source("import \"@strings\"; def main() -> i32 { strings.probe(); return 0; }");
    for (const std::string body :
         {"__strings_length(1);", "__strings_byte(\"x\");", "__strings_slice(\"a\", -1, 1);",
          "__strings_copy(\"x\", null);"}) {
        source("def pub probe() -> void { " + body + " }", "strings.gloin");
        for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({"--stdlib-dir", directory, mode, file}), 1, "error:");
    }
    for (const std::string body : {"__strings_byte(\"\", 0);", "__strings_byte(\"x\", 1);",
                                   "__strings_slice(\"abc\", 18446744073709551615, 2);",
                                   "__strings_slice(\"abc\", 2, 18446744073709551615);"}) {
        source("def pub probe() -> void { " + body + " }", "strings.gloin");
        expect_success(invoke({"--stdlib-dir", directory, "--check", file}), "");
        EXPECT_EQ(invoke({"--stdlib-dir", directory, file}).status, -2) << body;
    }
}

TEST_F(StringLibraryTest, PublicLibraryRemainsReplaceableAndNativeSymbolsCannotCollide) {
    source("def pub byte_length(text: string) -> i32 { return 42; }", "strings.gloin");
    expect_run(
        invoke(
            {"--stdlib-dir", directory,
             source(
                 "import \"@strings\"; def main() -> i32 { return strings.byte_length(\"\"); }")}),
        42);
    expect_run(invoke({program(R"(def text: result<string> = strings.copy(&memory, "x");
        if text.erroneous { return 97; }
        if text.erroneous { return 1; } return gloin_strings_copy();)",
                               "def gloin_strings_copy() -> i32 { return 42; }")}),
               42);
}

TEST_F(StringLibraryTest, JitRejectsIncorrectCopyRuntimeAbi) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const std::string declaration :
         {"llvm.func @gloin_strings_copy(!llvm.ptr, i32, !llvm.ptr)",
          "llvm.func @gloin_strings_copy(!llvm.ptr, i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_strings_copy(!llvm.ptr, i64, !llvm.ptr, ...)",
          "llvm.func @gloin_strings_copy(%a: !llvm.ptr, %b: i64, %c: !llvm.ptr) { llvm.return }"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + declaration +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}

TEST_F(StringLibraryTest, ExternalLlvmExecutionUsesNativeCopyAndByteOperations) {
    auto ir = invoke({"--emit-llvm", program(R"(
        def text: result<string> = strings.copy(&memory, " a\0b ");
        if text.erroneous { return 97; }
        if text.erroneous { return 1; }
        if !strings.equal(strings.trim_ascii(text.value), "a\0b") { return 2; }
        def byte: result<u8> = strings.byte_at(text.value, 2);
        if byte.erroneous { return 97; }
        if byte.erroneous || byte.value != 0 { return 3; } return 42;)")});
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

TEST_F(StringLibraryTest, DocumentedConfigurationExampleRunsAndKeepsScratchStorageBounded) {
    auto example = std::string(gloin_test::strings_example);
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example = (std::filesystem::path(binary).parent_path() /
                   "../share/gloinc/examples/strings_lab.gloin")
                      .string();
    expect_success(invoke({"--check", example}), "");
    expect_run(invoke({example}), 0, "count = 42\nstrings lab: ok\n");
}

TEST_F(StringLibraryTest, GuideCodeBlocksCompileAndRunAsWritten) {
    const auto guide = read(gloin_test::strings_guide);
    const std::string marker = "```gloin\n";
    size_t cursor = 0;
    unsigned blocks = 0;
    while ((cursor = guide.find(marker, cursor)) != std::string::npos) {
        cursor += marker.size();
        const auto end = guide.find("```", cursor);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(guide.substr(cursor, end - cursor))}), 0,
                   blocks == 0 ? "found name\n" : "");
        cursor = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}

TEST_F(StringLibraryTest, NullEmptyDescriptorCanBeSlicedTrimmedSearchedAndCopied) {
    copy_modules();
    source(read((library() / "std.gloin").string()) +
               "\ndef pub empty() -> string { return __std_string_view(null, 0); }",
           "std.gloin");
    const auto file = program(R"(
        def empty: string = std.empty();
        if !strings.is_empty(empty) || !strings.equal(strings.trim_ascii(empty), "") { return 1; }
        def slice: result<string> = strings.slice_bytes(empty, 0, 0);
        if slice.erroneous { return 97; }
        def byte: result<u8> = strings.byte_at(empty, 0);
        if !byte.erroneous { return 97; }
        def found: strings.FindResult = strings.find(empty, "");
        def copy: result<string> = strings.copy(&memory, empty);
        if copy.erroneous { return 97; }
        if slice.erroneous || !strings.is_empty(slice.value)
            || byte.error.code != status.OUT_OF_RANGE || !found.found || found.offset != 0
            || copy.erroneous || !strings.is_empty(copy.value) { return 2; }
        return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}

TEST_F(StringLibraryTest, ResultsRequireHandlingAndRejectInactiveOrLegacyPayloadAccess) {
    for (const std::string body :
         {"strings.byte_at(\"x\", 0);", "def r: result<string> = strings.copy(&memory, \"x\");",
          "def n: u8 = strings.byte_at(\"x\", 0).value;",
          "def r: result<u8> = strings.byte_at(\"x\", 0); def n: u8 = r.value;",
          "def r: result<string> = strings.slice_bytes(\"x\", 2, 1); "
          "if r.erroneous { std.println(r.value); }",
          "def r: result<string> = strings.copy(&memory, \"x\"); "
          "if r.erroneous { return 1; } std.println(r.error.message);",
          "def r: strings.StringResult = strings.copy(&memory, \"x\");",
          "def r: strings.ByteResult = strings.byte_at(\"x\", 0);"}) {
        const auto file = program(body + " return 0;");
        for (const auto *mode : {"--check", "--emit-ir", "--emit-llvm", "--jit"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(StringLibraryTest, CodesAliasesAndForwardingUseGloinIrInJitAndNativeModes) {
    const auto file = program(R"(
        def byte: result<u8> = strings.byte_at("a\0b", 1);
        if byte.erroneous { return 1; }
        if byte.value != 0 { return 2; }
        def view: result<string> = strings.slice_bytes("a\0b", 1, 2);
        if view.erroneous { return 3; }
        if !strings.equal(view.value, "\0b") { return 4; }
        def ordinary: result<u8> = strings.byte_at("", 0);
        if !ordinary.erroneous { return 5; }
        def alias: result<u8> = strings.byte_at_checked("", 0);
        if !alias.erroneous { return 6; }
        if ordinary.error.code != status.OUT_OF_RANGE || ordinary.error.os_error != 0
            || !strings.equal(ordinary.error.message, "string byte index is out of range")
            || alias.error.code != ordinary.error.code
            || !strings.equal(alias.error.message, ordinary.error.message) { return 7; }
        def code: i32 = match forwarded() {
            case value(text) => 0;
            case error(problem) => problem.code;
        };
        if code != status.OUT_OF_RANGE { return 8; }
        memory.free();
        def empty: result<string> = strings.copy(&memory, "");
        if empty.erroneous { return 9; }
        if !strings.is_empty(empty.value) { return 10; }
        return 42;
    )",
                              R"(
        def forwarded() -> result<string> {
            def part: result<string> = strings.slice_bytes_checked("abc", 1, 18446744073709551615);
            if part.erroneous { return part.error; }
            return part.value;
        }
    )");
    const auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation : {"!gloin.result<", "!gloin.error", "gloin.result_success",
                                  "gloin.result_failure", "gloin.error_code", "gloin.match"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({file}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/string-results";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        std::string message;
        bool failed = false;
        const auto code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10,
                                                    0, &message, &failed);
        EXPECT_FALSE(failed) << message;
        EXPECT_EQ(code, 42) << message;
    }
}
