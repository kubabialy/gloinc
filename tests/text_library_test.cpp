#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#include <tuple>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
class TextLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source(
            "import \"@strings\"; import \"@status\"; import \"@arena\"; import \"@std\"; "
            "def ok(r: strings.StringResult, text: string) -> bool { "
            "return r.status == status.OK && strings.equal(r.value, text); } "
            "def main() -> i32 { def mut memory: arena.GeneralArena = "
            "arena.GeneralArena.create(); defer memory.free(); " +
            body + " }");
    }
    std::filesystem::path library() {
        const char *binary = std::getenv("GLOIN_TEST_CLI");
        const auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
    void copy_modules() {
        for (const auto *name : {"strings.gloin", "std.gloin", "status.gloin", "arena.gloin"})
            source(read((library() / name).string()), name);
    }
    // Per-handle deterministic failure injection, while retaining the actual
    // typed allocation bridge, runtime allocation, and normal cleanup.
    void fail_allocation(unsigned number) {
        copy_modules();
        auto arena = read((library() / "arena.gloin").string());
        auto replace = [&](const std::string &from, const std::string &to) {
            size_t pos = 0;
            while ((pos = arena.find(from, pos)) != std::string::npos) {
                arena.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replace("def mut state: *u8,", "def mut state: *u8, def mut requests: u64,");
        replace("GeneralArena {\n            state: state\n        }",
                "GeneralArena {\n            state: state,\n            requests: 0\n        }");
        replace("__arena_general_alloc(self.state, size, alignment)",
                "self.test_allocate(size, alignment)");
        replace("__arena_general_alloc(self.state, size, 1)", "self.test_allocate(size, 1)");
        const auto insertion = arena.rfind('}');
        ASSERT_NE(insertion, std::string::npos);
        arena.insert(
            insertion,
            "def test_allocate(self: &GeneralArena, size: u64, alignment: u64) -> *u8 { "
            "self.requests = self.requests + 1; if self.requests == " +
                std::to_string(number) +
                " { return null; } return __arena_general_alloc(self.state, size, alignment); }");
        source(arena, "arena.gloin");
    }
};
std::string quoted(const std::string &text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\0')
            out += "\\0";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '"')
            out += "\\\"";
        else
            out += c;
    }
    return out + '"';
}
std::string replacement(const std::string &input, const std::string &needle,
                        const std::string &value) {
    std::string out;
    size_t pos = 0, match;
    while ((match = input.find(needle, pos)) != std::string::npos) {
        out += input.substr(pos, match - pos) + value;
        pos = match + needle.size();
    }
    return out + input.substr(pos);
}
} // namespace

TEST_F(TextLibraryTest, SplitMatchesIndependentOracleIncludingEmptyAndBinaryTokens) {
    std::string body;
    unsigned test = 0;
    for (const auto &[input, delimiter] : std::vector<std::pair<std::string, std::string>>{
             {"", ","},
             {",", ","},
             {",a,,", ","},
             {"abc", "longer"},
             {"aaaaa", "aa"},
             {"éλé", "é"},
             {std::string("a\0b\0", 4), std::string("\0", 1)},
             {"abc", "z"}}) {
        auto name = std::to_string(test++);
        body += "def made" + name + ": strings.SplitCursorResult = strings.SplitCursor.create(" +
                quoted(input) + ", " + quoted(delimiter) + ");";
        body += "if made" + name + ".status != status.OK { return 1; } def mut c" + name +
                ": strings.SplitCursor = made" + name + ".value;";
        size_t position = 0;
        for (;;) {
            auto end = input.find(delimiter, position);
            const auto expected =
                input.substr(position, end == std::string::npos ? end : end - position);
            body += "if !ok(c" + name + ".next(), " + quoted(expected) + ") { return 2; }";
            if (end == std::string::npos)
                break;
            position = end + delimiter.size();
        }
        body += "def end" + name + ": strings.StringResult = c" + name + ".next(); def again" +
                name + ": strings.StringResult = c" + name + ".next();";
        body += "if end" + name + ".status != status.END || again" + name +
                ".status != status.END || !strings.is_empty(end" + name + ".value) { return 3; }";
    }
    expect_run(invoke({program(body + "return 0;")}), 0);
}

TEST_F(TextLibraryTest, LineBoundariesCrLfStandaloneCrAndFinalLine) {
    std::string body;
    unsigned test = 0;
    for (const auto &[input, lines] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"", {}},
             {"\n", {""}},
             {"\n\n", {"", ""}},
             {"a\n", {"a"}},
             {"a\r\nb\r\r\nlast\r", {"a", "b\r", "last\r"}},
             {"a\rb", {"a\rb"}},
             {"\ra\n\r", {"\ra", "\r"}},
             {std::string("a\0b\n", 4), {std::string("a\0b", 3)}}}) {
        const auto name = std::to_string(test++);
        body += "def mut c" + name + ": strings.LineCursor = strings.LineCursor.create(" +
                quoted(input) + ");";
        for (const auto &line : lines)
            body += "if !ok(c" + name + ".next(), " + quoted(line) + ") { return 1; }";
        body += "def end" + name + ": strings.StringResult = c" + name + ".next(); def again" +
                name + ": strings.StringResult = c" + name + ".next();";
        body += "if end" + name + ".status != status.END || again" + name +
                ".status != status.END || !strings.is_empty(end" + name + ".value) { return 2; }";
    }
    expect_run(invoke({program(body + "return 0;")}), 0);
}

TEST_F(TextLibraryTest, CursorCopiesAdvanceIndependentlyAndEmptyDelimiterIsInvalid) {
    expect_run(invoke({program(R"(
        def bad: strings.SplitCursorResult = strings.SplitCursor.create("abc", "");
        if bad.status != status.INVALID { return 1; }
        def mut invalid: strings.SplitCursor = bad.value;
        def error: strings.StringResult = invalid.next();
        if error.status != status.INVALID || !strings.is_empty(error.value) { return 2; }
        def made: strings.SplitCursorResult = strings.SplitCursor.create("a,b,c", ",");
        def mut a: strings.SplitCursor = made.value;
        if !ok(a.next(), "a") { return 3; }
        def mut b: strings.SplitCursor = a;
        if !ok(a.next(), "b") || !ok(a.next(), "c") || !ok(b.next(), "b") { return 4; }
        def mut first: strings.LineCursor = strings.LineCursor.create("x\ny");
        def saved: strings.StringResult = first.next();
        def mut second: strings.LineCursor = first;
        if !ok(first.next(), "y") || !ok(second.next(), "y") || !ok(saved, "x") { return 5; }
        return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, ConcatAndRepeatCheckLimitsAndOverflowBeforeAllocation) {
    expect_run(invoke({program(R"(
        if !ok(strings.concat(&memory, "a\0", "b", 3), "a\0b")
            || !ok(strings.repeat(&memory, "ab", 3, 6), "ababab") { return 1; }
        def huge: strings.StringResult = strings.repeat(&memory, "x", 18446744073709551615, 18446744073709551615);
        if huge.status != status.NO_MEMORY || !strings.is_empty(huge.value) { return 2; }
        memory.free(); // Prove limit/empty paths do not even inspect arena state.
        def a: strings.StringResult = strings.concat(&memory, "a", "bc", 2);
        def b: strings.StringResult = strings.repeat(&memory, "ab", 18446744073709551615, 18446744073709551615);
        if a.status != status.TOO_LONG || b.status != status.TOO_LONG || !strings.is_empty(a.value) || !strings.is_empty(b.value) { return 3; }
        if !ok(strings.concat(&memory, "", "", 0), "")
            || !ok(strings.repeat(&memory, "", 18446744073709551615, 0), "")
            || !ok(strings.repeat(&memory, "abc", 0, 0), "") { return 4; }
        return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, ReplacementMatchesIndependentOracleAndUsesFinalSize) {
    std::string body;
    unsigned test = 0;
    for (const auto &[input, needle, value] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"aaaaa", "aa", "b"},
             {"aaaa", "aa", ""},
             {"abc", "z", "x"},
             {"a", "a", "aaa"},
             {"abcabc", "bc", "X"},
             {"", "x", "long"},
             {"a", "long", ""},
             {"abababa", "aba", "ab"},
             {std::string("a\0a", 3), std::string("\0", 1), "é"},
             {"éé", "é", "λ"}}) {
        const auto expected = replacement(input, needle, value), name = std::to_string(test++);
        const auto args = quoted(input) + ", " + quoted(needle) + ", " + quoted(value) + ", ";
        body += "if !ok(strings.replace_all(&memory, " + args + std::to_string(expected.size()) +
                "), " + quoted(expected) + ") { return 1; }";
        if (!expected.empty()) {
            body += "def bad" + name + ": strings.StringResult = strings.replace_all(&memory, " +
                    args + std::to_string(expected.size() - 1) + ");";
            body += "if bad" + name + ".status != status.TOO_LONG || !strings.is_empty(bad" + name +
                    ".value) { return 2; }";
        }
    }
    body +=
        R"(memory.free(); def bad: strings.StringResult = strings.replace_all(&memory, "x", "", "z", 0);
        if bad.status != status.INVALID { return 3; }
        if !ok(strings.replace_all(&memory, "aaaa", "a", "", 0), "") { return 4; } return 0;)";
    expect_run(invoke({program(body)}), 0);
}

TEST_F(TextLibraryTest, AsciiCaseTransformsAllByteValuesWithoutUnicodeOrNulChanges) {
    std::string input, lower, upper;
    for (unsigned i = 0; i < 256; ++i) {
        input += char(i);
        lower += char(i >= 65 && i <= 90 ? i + 32 : i);
        upper += char(i >= 97 && i <= 122 ? i - 32 : i);
    }
    const auto file = program(R"(
        def a: std.InputResult = std.input(&memory, 256);
        def b: std.InputResult = std.input(&memory, 256);
        if a.status != status.OK || b.status != status.OK { return 1; }
        def prefix: strings.StringResult = strings.concat(&memory, a.value, "\n", 256);
        def all: strings.StringResult = strings.concat(&memory, prefix.value, b.value, 256);
        if all.status != status.OK || strings.byte_length(all.value) != 256 { return 2; }
        def low: strings.StringResult = strings.lower_ascii(&memory, all.value, 256);
        def high: strings.StringResult = strings.upper_ascii(&memory, all.value, 256);
        if low.status != status.OK || high.status != status.OK { return 3; }
        std.print(low.value); std.print(high.value); return 0;)");
    expect_run(invoke(
                   {
                       file,
                   },
                   source(input, "stdin")),
               0, lower + upper);
}

TEST_F(TextLibraryTest, TransformCopiesSurviveSourceResetAndCaseLimitIsPreflighted) {
    expect_run(invoke({program(R"(
        def mut origin: arena.GeneralArena = arena.GeneralArena.create();
        def original: strings.StringResult = strings.copy(&origin, "Abc");
        def a: strings.StringResult = strings.concat(&memory, original.value, "", 3);
        def b: strings.StringResult = strings.repeat(&memory, original.value, 1, 3);
        def c: strings.StringResult = strings.replace_all(&memory, original.value, "z", "x", 3);
        def d: strings.StringResult = strings.lower_ascii(&memory, original.value, 3);
        def e: strings.StringResult = strings.upper_ascii(&memory, original.value, 3);
        origin.reset(); strings.copy(&origin, "xxx"); origin.free();
        if !ok(a,"Abc") || !ok(b,"Abc") || !ok(c,"Abc") || !ok(d,"abc") || !ok(e,"ABC") { return 1; }
        memory.free();
        def low: strings.StringResult = strings.lower_ascii(&memory, "ABC", 2);
        def high: strings.StringResult = strings.upper_ascii(&memory, "abc", 2);
        if low.status != status.TOO_LONG || high.status != status.TOO_LONG { return 2; }
        if !ok(strings.lower_ascii(&memory,"",0),"") || !ok(strings.upper_ascii(&memory,"",0),"") { return 3; }
        return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, BuilderAliasesShareLengthAndFailedAppendIsAtomic) {
    expect_run(invoke({program(R"(
        def made: strings.BuilderResult = strings.StringBuilder.create(&memory, 4);
        if made.status != status.OK { return 1; }
        def mut a: strings.StringBuilder = made.value; def mut b: strings.StringBuilder = a;
        if a.append("ab") != status.OK || b.byte_length() != 2 || b.append_byte(0) != status.OK { return 2; }
        if a.append("XY") != status.TOO_LONG || b.byte_length() != 3 || !ok(a.to_string(&memory),"ab\0") { return 3; }
        if b.append_byte(255) != status.OK || a.byte_length() != 4 || a.capacity() != 4 { return 4; }
        if a.append_byte(1) != status.TOO_LONG || a.append("") != status.OK { return 5; }
        def saved: strings.StringResult = a.to_string(&memory);
        def last: strings.ByteResult = strings.byte_at(saved.value,3);
        if saved.status != status.OK || last.value != 255 { return 6; }
        b.clear();
        if a.byte_length() != 0 || a.capacity() != 4 || a.append("new") != status.OK { return 7; }
        if strings.byte_length(saved.value) != 4 || !ok(b.to_string(&memory),"new") { return 8; }
        def view: &const strings.StringBuilder = &a;
        if view.byte_length() != 3 || view.capacity() != 4 { return 9; } return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, ZeroCapacityBuilderAndSnapshotLifetime) {
    expect_run(invoke({program(R"(
        def zero: strings.BuilderResult = strings.StringBuilder.create(&memory,0);
        if zero.status != status.OK { return 1; }
        def mut empty: strings.StringBuilder = zero.value;
        if empty.append("") != status.OK || empty.append_byte(0) != status.TOO_LONG || empty.append("x") != status.TOO_LONG { return 2; }
        empty.clear(); if !ok(empty.to_string(&memory),"") || empty.capacity() != 0 { return 3; }
        def mut storage: arena.GeneralArena = arena.GeneralArena.create();
        def made: strings.BuilderResult = strings.StringBuilder.create(&storage,8);
        if made.status != status.OK { return 4; }
        def mut b: strings.StringBuilder = made.value;
        if b.append("keep") != status.OK { return 5; }
        def saved: strings.StringResult = b.to_string(&memory);
        storage.reset(); strings.copy(&storage,"overwrite"); storage.free();
        if !ok(saved,"keep") { return 6; } return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, BuilderReportsBothAllocationFailuresAndInvalidHandleTraps) {
    for (unsigned allocation : {1u, 2u}) {
        fail_allocation(allocation);
        auto file =
            program(R"(def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8);
            if made.status != status.NO_MEMORY { return 1; } return 0;)");
        expect_run(invoke({"--stdlib-dir", directory, file}), 0);
        file = program(R"(def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8);
            def mut invalid: strings.StringBuilder = made.value; invalid.clear(); return 0;)");
        EXPECT_EQ(invoke({"--stdlib-dir", directory, file}).status, -2);
    }
    expect_run(
        invoke({program(
            R"(def made: strings.BuilderResult = strings.StringBuilder.create(&memory,18446744073709551615);
        if made.status != status.NO_MEMORY { return 1; } memory.reset();
        def good: strings.BuilderResult = strings.StringBuilder.create(&memory,1);
        if good.status != status.OK { return 2; } return 0;)")}),
        0);
}

TEST_F(TextLibraryTest, AppendClearAndCursorsAllocateNothingSnapshotFailurePreservesBuilder) {
    fail_allocation(3);
    auto file = program(R"(
        def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8);
        if made.status != status.OK { return 1; }
        def mut b: strings.StringBuilder = made.value;
        def parts: strings.SplitCursorResult = strings.SplitCursor.create("a,b",",");
        def mut c: strings.SplitCursor = parts.value;
        def mut lines: strings.LineCursor = strings.LineCursor.create("x\n");
        if !ok(c.next(),"a") || !ok(lines.next(),"x") { return 2; }
        for def mut i: i32 = 0; i < 10000; i = i + 1 {
            b.clear(); if b.append("abc") != status.OK || b.append_byte(0) != status.OK { return 3; }
        }
        def failed: strings.StringResult = b.to_string(&memory);
        if failed.status != status.NO_MEMORY || !strings.is_empty(failed.value) || b.byte_length() != 4 { return 4; }
        if !ok(b.to_string(&memory),"abc\0") { return 5; } return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}

TEST_F(TextLibraryTest, TransformAllocationFailuresHaveEmptyResults) {
    for (const std::string call :
         {"strings.concat(&memory,\"a\",\"b\",2)", "strings.repeat(&memory,\"x\",3,3)",
          "strings.replace_all(&memory,\"abc\",\"b\",\"X\",3)",
          "strings.lower_ascii(&memory,\"ABC\",3)", "strings.upper_ascii(&memory,\"abc\",3)"}) {
        fail_allocation(1);
        auto file = program("def result: strings.StringResult = " + call +
                            "; if result.status != status.NO_MEMORY || "
                            "!strings.is_empty(result.value) { return 1; } return 0;");
        expect_run(invoke({"--stdlib-dir", directory, file}), 0);
    }
}

TEST_F(TextLibraryTest, MutabilityPrivacyAndArgumentTypesAreEnforced) {
    for (const std::string body :
         {"strings.repeat(&memory,\"a\",-1,8);", "strings.concat(&memory,\"a\",\"b\");",
          "strings.upper_ascii(&memory,42,8);", "strings.SplitCursor.create(\"a\",1);",
          "def c: strings.LineCursor = strings.LineCursor.create(\"a\"); c.next();",
          "def c: strings.LineCursor = strings.LineCursor.create(\"a\"); c.offset;",
          "def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8); def b: "
          "strings.StringBuilder = made.value; b.append(\"a\");",
          "def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8); def mut b: "
          "strings.StringBuilder = made.value; b.state;",
          "def made: strings.BuilderResult = strings.StringBuilder.create(&memory,8); def mut b: "
          "strings.StringBuilder = made.value; b.append_byte(256);"}) {
        auto file = program(body + "return 0;");
        for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(TextLibraryTest, PrivateWritePrimitivesValidateTypesAndModuleIdentity) {
    for (const std::string call :
         {"__strings_store(null,1,0,0);", "__strings_write(null,0,0,\"\");",
          "__strings_buffer_view(null,0);"})
        expect_error(invoke({program(call + "return 0;")}), 1, "Undefined variable");
    const auto file =
        source("import \"@strings\"; def main() -> i32 { strings.probe(); return 0; }");
    for (const std::string body :
         {"__strings_store(null,0,0);", "__strings_write(null,0,0,1);",
          "def byte: u8 = 0; __strings_store(&byte,1,0,0);", "__strings_buffer_view(1,0);"}) {
        source("def pub probe() -> void { " + body + " }", "strings.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "error:");
    }
    source("def __strings_store() -> void {}", "strings.gloin");
    expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "Cannot redeclare");
}

TEST_F(TextLibraryTest, PrivateWriteBoundsAndNullGuardsTrapBeforeAccess) {
    const auto file =
        source("import \"@strings\"; def main() -> i32 { strings.probe(); return 0; }");
    for (const std::string body :
         {"__strings_store(null,1,0,0);", "__strings_store(null,0,0,0);",
          "def mut b: u8 = 0; __strings_store(&b,1,1,0);",
          "def mut b: u8 = 0; __strings_write(&b,1,18446744073709551615,\"\");",
          "def mut b: u8 = 0; __strings_write(&b,1,1,\"x\");", "__strings_write(null,1,0,\"\");",
          "__strings_buffer_view(null,1);"}) {
        source("def pub probe() -> void { " + body + " }", "strings.gloin");
        expect_success(invoke({"--stdlib-dir", directory, "--check", file}), "");
        EXPECT_EQ(invoke({"--stdlib-dir", directory, file}).status, -2) << body;
    }
    source(R"(def pub probe() -> void {
        __strings_write(null,0,0,"");
        def mut b: u8 = 0; __strings_store(&b,1,0,255);
        __strings_write(&b,1,0,"Z"); __strings_write(&b,1,1,"");
        __write_stdout(__strings_buffer_view(&b,1));
    })",
           "strings.gloin");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0, "Z");
}

TEST_F(TextLibraryTest, BuilderAndTransformsExecuteWithExternalRuntime) {
    auto ir = invoke({"--emit-llvm", program(R"(
        def made: strings.BuilderResult = strings.StringBuilder.create(&memory,16);
        if made.status != status.OK { return 1; }
        def mut builder: strings.StringBuilder = made.value;
        def text: strings.StringResult = strings.replace_all(&memory,"a b"," ","_",3);
        if text.status != status.OK || builder.append(text.value) != status.OK || builder.append_byte(0) != status.OK { return 2; }
        if !ok(builder.to_string(&memory),"a_b\0") { return 3; } return 42;)")});
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

TEST_F(TextLibraryTest, PackagedReportExampleAndGuideProgramsRunAsWritten) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "text_lab.gloin";
    auto guide =
        std::filesystem::path(gloin_test::strings_guide).parent_path() / "text-construction.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI")) {
        auto root = std::filesystem::path(binary).parent_path();
        example = root / "../share/gloinc/examples/text_lab.gloin";
        guide = root / "../share/doc/gloinc/docs/text-construction.md";
    }
    expect_run(invoke({example.string()}), 0,
               "NAME=Gloin &amp; friends\nEMPTY=\nCOUNT=42\n---done\n");
    const auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(doc.substr(pos, end - pos))}), 0,
                   blocks == 0 ? "[a]\n[]\n[b]\n[]\n" : "hello\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}
