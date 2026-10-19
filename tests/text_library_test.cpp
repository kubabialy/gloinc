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
            R"(def ok(r: result<string>, text: string) -> bool {
                if r.erroneous { return false; }
                return strings.equal(r.value, text);
            }
            def item_ok(r: strings.TextItem, text: string) -> bool {
                return !r.end && strings.equal(r.value, text);
            }
            def void_code(r: result<void>) -> i32 {
                if r.erroneous { return r.error.code; }
                return status.OK;
            })"
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
        body += "def made" + name + ": result<strings.SplitCursor> = strings.SplitCursor.create(" +
                quoted(input) + ", " + quoted(delimiter) + ");";
        body += "if made" + name + ".erroneous { return 1; } def mut c" + name +
                ": strings.SplitCursor = made" + name + ".value;";
        size_t position = 0;
        for (;;) {
            auto end = input.find(delimiter, position);
            const auto expected =
                input.substr(position, end == std::string::npos ? end : end - position);
            body += "if !item_ok(c" + name + ".next(), " + quoted(expected) + ") { return 2; }";
            if (end == std::string::npos)
                break;
            position = end + delimiter.size();
        }
        body += "def end" + name + ": strings.TextItem = c" + name + ".next(); def again" + name +
                ": strings.TextItem = c" + name + ".next();";
        body += "if !end" + name + ".end || !again" + name + ".end || !strings.is_empty(end" +
                name + ".value) { return 3; }";
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
            body += "if !item_ok(c" + name + ".next(), " + quoted(line) + ") { return 1; }";
        body += "def end" + name + ": strings.TextItem = c" + name + ".next(); def again" + name +
                ": strings.TextItem = c" + name + ".next();";
        body += "if !end" + name + ".end || !again" + name + ".end || !strings.is_empty(end" +
                name + ".value) { return 2; }";
    }
    expect_run(invoke({program(body + "return 0;")}), 0);
}

TEST_F(TextLibraryTest, CursorCopiesAdvanceIndependentlyAndEmptyDelimiterIsInvalid) {
    expect_run(invoke({program(R"(
        def bad: result<strings.SplitCursor> = strings.SplitCursor.create("abc", "");
        if !bad.erroneous { return 97; }
        if bad.error.code != status.INVALID { return 1; }
        def made: result<strings.SplitCursor> = strings.SplitCursor.create("a,b,c", ",");
        if made.erroneous { return 97; }
        def mut a: strings.SplitCursor = made.value;
        if !item_ok(a.next(), "a") { return 3; }
        def mut b: strings.SplitCursor = a;
        if !item_ok(a.next(), "b") || !item_ok(a.next(), "c") || !item_ok(b.next(), "b") { return 4; }
        def mut first: strings.LineCursor = strings.LineCursor.create("x\ny");
        def saved: strings.TextItem = first.next();
        def mut second: strings.LineCursor = first;
        if !item_ok(first.next(), "y") || !item_ok(second.next(), "y") || !item_ok(saved, "x") { return 5; }
        return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, ConcatAndRepeatCheckLimitsAndOverflowBeforeAllocation) {
    expect_run(invoke({program(R"(
        if !ok(strings.concat(&memory, "a\0", "b", 3), "a\0b")
            || !ok(strings.repeat(&memory, "ab", 3, 6), "ababab") { return 1; }
        def huge: result<string> = strings.repeat(&memory, "x", 18446744073709551615, 18446744073709551615);
        if !huge.erroneous { return 97; }
        if huge.error.code != status.NO_MEMORY || huge.error.os_error != 0 { return 2; }
        memory.free(); // Prove limit/empty paths do not even inspect arena state.
        def a: result<string> = strings.concat(&memory, "a", "bc", 2);
        if !a.erroneous { return 97; }
        def b: result<string> = strings.repeat(&memory, "ab", 18446744073709551615, 18446744073709551615);
        if !b.erroneous { return 97; }
        if a.error.code != status.TOO_LONG || b.error.code != status.TOO_LONG || a.error.os_error != 0 || b.error.os_error != 0 { return 3; }
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
            body += "def bad" + name + ": result<string> = strings.replace_all(&memory, " + args +
                    std::to_string(expected.size() - 1) + ");";
            body += "if !bad" + name + ".erroneous { return 2; } if bad" + name +
                    ".error.code != status.TOO_LONG || bad" + name +
                    ".error.os_error != 0 { return 2; }";
        }
    }
    body +=
        R"(memory.free(); def bad: result<string> = strings.replace_all(&memory, "x", "", "z", 0);
        if !bad.erroneous { return 97; }
        if bad.error.code != status.INVALID { return 3; }
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
        def a_result: result<std.InputLine> = std.input(&memory, 256);
        if a_result.erroneous { return 97; }
        def a: std.InputLine = a_result.value;
        def b_result: result<std.InputLine> = std.input(&memory, 256);
        if b_result.erroneous { return 97; }
        def b: std.InputLine = b_result.value;
        if a.end || b.end { return 1; }
        def prefix: result<string> = strings.concat(&memory, a.value, "\n", 256);
        if prefix.erroneous { return 97; }
        def all: result<string> = strings.concat(&memory, prefix.value, b.value, 256);
        if all.erroneous { return 97; }
        if all.erroneous || strings.byte_length(all.value) != 256 { return 2; }
        def low: result<string> = strings.lower_ascii(&memory, all.value, 256);
        if low.erroneous { return 97; }
        def high: result<string> = strings.upper_ascii(&memory, all.value, 256);
        if high.erroneous { return 97; }
        if low.erroneous || high.erroneous { return 3; }
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
        def original: result<string> = strings.copy(&origin, "Abc");
        if original.erroneous { return 97; }
        def a: result<string> = strings.concat(&memory, original.value, "", 3);
        if a.erroneous { return 97; }
        def b: result<string> = strings.repeat(&memory, original.value, 1, 3);
        if b.erroneous { return 97; }
        def c: result<string> = strings.replace_all(&memory, original.value, "z", "x", 3);
        if c.erroneous { return 97; }
        def d: result<string> = strings.lower_ascii(&memory, original.value, 3);
        if d.erroneous { return 97; }
        def e: result<string> = strings.upper_ascii(&memory, original.value, 3);
        if e.erroneous { return 97; }
        origin.reset(); if !ok(strings.copy(&origin, "xxx"), "xxx") { return 97; } origin.free();
        if !ok(a,"Abc") || !ok(b,"Abc") || !ok(c,"Abc") || !ok(d,"abc") || !ok(e,"ABC") { return 1; }
        memory.free();
        def low: result<string> = strings.lower_ascii(&memory, "ABC", 2);
        if !low.erroneous { return 97; }
        def high: result<string> = strings.upper_ascii(&memory, "abc", 2);
        if !high.erroneous { return 97; }
        if low.error.code != status.TOO_LONG || high.error.code != status.TOO_LONG { return 2; }
        if !ok(strings.lower_ascii(&memory,"",0),"") || !ok(strings.upper_ascii(&memory,"",0),"") { return 3; }
        return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, BuilderAliasesShareLengthAndFailedAppendIsAtomic) {
    expect_run(invoke({program(R"(
        def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory, 4);
        if made.erroneous { return 97; }
        if made.erroneous { return 1; }
        def mut a: strings.StringBuilder = made.value; def mut b: strings.StringBuilder = a;
        if void_code(a.append("ab")) != status.OK || b.byte_length() != 2 || void_code(b.append_byte(0)) != status.OK { return 2; }
        if void_code(a.append("XY")) != status.TOO_LONG || b.byte_length() != 3 || !ok(a.to_string(&memory),"ab\0") { return 3; }
        if void_code(b.append_byte(255)) != status.OK || a.byte_length() != 4 || a.capacity() != 4 { return 4; }
        if void_code(a.append_byte(1)) != status.TOO_LONG || void_code(a.append("")) != status.OK { return 5; }
        def saved: result<string> = a.to_string(&memory);
        if saved.erroneous { return 97; }
        def last: result<u8> = strings.byte_at(saved.value,3);
        if last.erroneous { return 97; }
        if saved.erroneous || last.value != 255 { return 6; }
        b.clear();
        if a.byte_length() != 0 || a.capacity() != 4 || void_code(a.append("new")) != status.OK { return 7; }
        if strings.byte_length(saved.value) != 4 || !ok(b.to_string(&memory),"new") { return 8; }
        def view: &const strings.StringBuilder = &a;
        if view.byte_length() != 3 || view.capacity() != 4 { return 9; } return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, ZeroCapacityBuilderAndSnapshotLifetime) {
    expect_run(invoke({program(R"(
        def zero: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,0);
        if zero.erroneous { return 97; }
        if zero.erroneous { return 1; }
        def mut empty: strings.StringBuilder = zero.value;
        if void_code(empty.append("")) != status.OK || void_code(empty.append_byte(0)) != status.TOO_LONG || void_code(empty.append("x")) != status.TOO_LONG { return 2; }
        empty.clear(); if !ok(empty.to_string(&memory),"") || empty.capacity() != 0 { return 3; }
        def mut storage: arena.GeneralArena = arena.GeneralArena.create();
        def made: result<strings.StringBuilder> = strings.StringBuilder.create(&storage,8);
        if made.erroneous { return 97; }
        if made.erroneous { return 4; }
        def mut b: strings.StringBuilder = made.value;
        if void_code(b.append("keep")) != status.OK { return 5; }
        def saved: result<string> = b.to_string(&memory);
        if saved.erroneous { return 97; }
        storage.reset(); if !ok(strings.copy(&storage,"overwrite"), "overwrite") { return 97; } storage.free();
        if !ok(saved,"keep") { return 6; } return 0;)")}),
               0);
}

TEST_F(TextLibraryTest, BuilderReportsBothAllocationFailuresAndRejectsUncheckedHandle) {
    for (unsigned allocation : {1u, 2u}) {
        fail_allocation(allocation);
        auto file = program(
            R"(def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8);
        if !made.erroneous { return 97; }
            if made.error.code != status.NO_MEMORY { return 1; } return 0;)");
        expect_run(invoke({"--stdlib-dir", directory, file}), 0);
        file = program(
            R"(def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8);
            def mut invalid: strings.StringBuilder = made.value; invalid.clear(); return 0;)");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "proven non-erroneous path");
    }
    expect_run(
        invoke({program(
            R"(def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,18446744073709551615);
        if !made.erroneous { return 97; }
        if made.error.code != status.NO_MEMORY { return 1; } memory.reset();
        def good: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,1);
        if good.erroneous { return 97; }
        if good.erroneous { return 2; } return 0;)")}),
        0);
}

TEST_F(TextLibraryTest, AppendClearAndCursorsAllocateNothingSnapshotFailurePreservesBuilder) {
    fail_allocation(3);
    auto file = program(R"(
        def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8);
        if made.erroneous { return 97; }
        if made.erroneous { return 1; }
        def mut b: strings.StringBuilder = made.value;
        def parts: result<strings.SplitCursor> = strings.SplitCursor.create("a,b",",");
        if parts.erroneous { return 97; }
        def mut c: strings.SplitCursor = parts.value;
        def mut lines: strings.LineCursor = strings.LineCursor.create("x\n");
        if !item_ok(c.next(),"a") || !item_ok(lines.next(),"x") { return 2; }
        for def mut i: i32 = 0; i < 10000; i = i + 1 {
            b.clear(); if void_code(b.append("abc")) != status.OK || void_code(b.append_byte(0)) != status.OK { return 3; }
        }
        def failed: result<string> = b.to_string(&memory);
        if !failed.erroneous { return 97; }
        if failed.error.code != status.NO_MEMORY || failed.error.os_error != 0 || b.byte_length() != 4 { return 4; }
        if !ok(b.to_string(&memory),"abc\0") { return 5; } return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}

TEST_F(TextLibraryTest, TransformAllocationFailuresPreserveDiagnosticCodes) {
    for (const std::string call :
         {"strings.concat(&memory,\"a\",\"b\",2)", "strings.repeat(&memory,\"x\",3,3)",
          "strings.replace_all(&memory,\"abc\",\"b\",\"X\",3)",
          "strings.lower_ascii(&memory,\"ABC\",3)", "strings.upper_ascii(&memory,\"abc\",3)"}) {
        fail_allocation(1);
        auto file = program("def outcome: result<string> = " + call +
                            "; if !outcome.erroneous { return 1; } "
                            "if outcome.error.code != status.NO_MEMORY || outcome.error.os_error "
                            "!= 0 { return 1; } return 0;");
        expect_run(invoke({"--stdlib-dir", directory, file}), 0);
    }
}

TEST_F(TextLibraryTest, MutabilityPrivacyAndArgumentTypesAreEnforced) {
    for (const std::string body :
         {"strings.repeat(&memory,\"a\",-1,8);", "strings.concat(&memory,\"a\",\"b\");",
          "strings.upper_ascii(&memory,42,8);", "strings.SplitCursor.create(\"a\",1);",
          "def c: strings.LineCursor = strings.LineCursor.create(\"a\"); c.next();",
          "def c: strings.LineCursor = strings.LineCursor.create(\"a\"); c.offset;",
          "def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8); def "
          "b: "
          "strings.StringBuilder = made.value; b.append(\"a\");",
          "def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8); def "
          "mut b: "
          "strings.StringBuilder = made.value; b.state;",
          "def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,8); def "
          "mut b: "
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
        def made: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,16);
        if made.erroneous { return 97; }
        if made.erroneous { return 1; }
        def mut builder: strings.StringBuilder = made.value;
        def text: result<string> = strings.replace_all(&memory,"a b"," ","_",3);
        if text.erroneous { return 97; }
        if text.erroneous || void_code(builder.append(text.value)) != status.OK || void_code(builder.append_byte(0)) != status.OK { return 2; }
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

TEST_F(TextLibraryTest, ConstructorsProtectInvariantsAndAppendResultsMustBeHandled) {
    for (const std::string body :
         {"def r: result<strings.SplitCursor> = strings.SplitCursor.create(\"x\",\"\"); "
          "if r.erroneous { def mut cursor: strings.SplitCursor = r.value; cursor.next(); }",
          "def cursor: strings.SplitCursor = strings.SplitCursor { "
          "text: \"x\", delimiter: \"\", offset: 0, done: false };",
          "def cursors: [strings.SplitCursor; 1] = zeroed;",
          "def r: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,4); "
          "if r.erroneous { return 1; } def mut b: strings.StringBuilder = r.value; "
          "b.append(\"a\");",
          "def r: result<strings.StringBuilder> = strings.StringBuilder.create(&memory,4); "
          "if r.erroneous { return 1; } def mut b: strings.StringBuilder = r.value; "
          "b.append_byte(0);",
          "def r: strings.SplitCursorResult = strings.SplitCursor.create(\"x\",\",\");",
          "def r: strings.BuilderResult = strings.StringBuilder.create(&memory,4);"}) {
        const auto file = program(body + " return 0;");
        for (const auto *mode : {"--check", "--emit-ir", "--emit-llvm", "--jit"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(TextLibraryTest, CursorEndAndAtomicAppendOutcomesExecuteInNativeModes) {
    const auto file = program(R"(
        def invalid: result<strings.SplitCursor> = strings.SplitCursor.create("a", "");
        if !invalid.erroneous { return 1; }
        if invalid.error.code != status.INVALID || invalid.error.os_error != 0
            || !strings.equal(invalid.error.message, "split delimiter must not be empty") { return 2; }
        def made: result<strings.SplitCursor> = strings.SplitCursor.create(",,", ",");
        if made.erroneous { return 3; }
        def mut cursor: strings.SplitCursor = made.value;
        if !item_ok(cursor.next(), "") || !item_ok(cursor.next(), "")
            || !item_ok(cursor.next(), "") { return 4; }
        def end: strings.TextItem = cursor.next();
        if !end.end || !strings.is_empty(end.value) || !cursor.next().end { return 5; }
        def mut lines: strings.LineCursor = strings.LineCursor.create("\nlast\r");
        if !item_ok(lines.next(), "") || !item_ok(lines.next(), "last\r")
            || !lines.next().end || !lines.next().end { return 6; }
        def builder: result<strings.StringBuilder> = strings.StringBuilder.create(&memory, 3);
        if builder.erroneous { return 7; }
        def mut a: strings.StringBuilder = builder.value;
        def mut alias: strings.StringBuilder = a;
        if void_code(a.append("a\0")) != status.OK { return 8; }
        def failed: result<void> = alias.append("XY");
        if !failed.erroneous { return 9; }
        if failed.error.code != status.TOO_LONG || failed.error.os_error != 0
            || !strings.equal(failed.error.message, "string builder capacity exceeded") { return 10; }
        if alias.byte_length() != 2 || !ok(a.to_string(&memory), "a\0") { return 11; }
        if void_code(alias.append_byte(98)) != status.OK || void_code(a.append("")) != status.OK { return 12; }
        if !ok(a.to_string(&memory), "a\0b") { return 13; }
        def copied: result<string> = a.to_string(&memory);
        if copied.erroneous { return 14; }
        alias.clear();
        if a.byte_length() != 0 || !strings.equal(copied.value, "a\0b") { return 15; }
        return 42;
    )");
    const auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation :
         {"gloin.result_failure", "gloin.result_success", "gloin.struct_literal"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({file}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/text-results";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        std::string message;
        bool failed = false;
        const auto code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10,
                                                    0, &message, &failed);
        EXPECT_FALSE(failed) << message;
        EXPECT_EQ(code, 42) << message;
    }
}
