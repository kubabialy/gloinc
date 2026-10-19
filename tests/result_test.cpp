#include "support/cli_fixture.h"

namespace {
class ResultTest : public gloin_test::CliFixture {};
} // namespace

TEST_F(ResultTest, SuccessFailureAndExplicitForwardingRunInJitAndAot) {
    auto file = source(R"(
        def divide(a: i32, b: i32) -> result<i32> {
            if b == 0 { return error("division by zero"); }
            return a / b;
        }
        def forward(a: i32, b: i32) -> result<i32> {
            def inner: result<i32> = divide(a, b);
            if inner.erroneous { return inner.error; }
            return inner.value + 1;
        }
        def main() -> i32 {
            def first: result<i32> = forward(6, 2);
            if first.erroneous { return 1; }
            def second: result<i32> = forward(6, 0);
            if second.erroneous { return first.value + 39; }
            return 2;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({"--jit", file}), 43);
    const auto executable = directory + "/result-aot";
    expect_success(invoke_raw({"-o", executable, file}), "");
    const auto out = directory + "/result-aot.out";
    const auto err = directory + "/result-aot.err";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                               10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 43) << message << read(err);
    EXPECT_TRUE(read(out).empty());
    EXPECT_TRUE(read(err).empty());
    auto ir = invoke({"--emit-ir", file});
    EXPECT_EQ(ir.status, 0) << ir.err;
    EXPECT_NE(ir.out.find("!gloin.result<i32>"), std::string::npos);
    EXPECT_NE(ir.out.find("gloin.result_success"), std::string::npos);
    EXPECT_NE(ir.out.find("gloin.result_failure"), std::string::npos);
    EXPECT_NE(ir.out.find("gloin.result_is_error"), std::string::npos);
}

TEST_F(ResultTest, VoidResultAndErrorMessage) {
    auto file = source(R"(
        import "@std";
        def action(fail: bool) -> result<void> {
            if fail { return error("bad action"); }
            return;
        }
        def main() -> i32 {
            def first: result<void> = action(false);
            if first.erroneous { return 1; }
            def second: result<void> = action(true);
            if second.erroneous {
                std.println(second.error.message);
                return 42;
            }
            return 2;
        }
    )");
    expect_run(invoke({file}), 42, "bad action\n");
}

TEST_F(ResultTest, PayloadsAnchorNumericLiteralsWithoutRelaxingAccessOrConversions) {
    for (std::string type : {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64"}) {
        const std::string literal = type[0] == 'f' ? "42.5" : "42";
        const std::string lower = type[0] == 'f' ? "40.5" : "40";
        const std::string step = type[0] == 'f' ? "2.0" : "2";
        const auto header = "def answer() -> result<" + type + "> { return " + literal + "; } ";
        auto file = source(header + "def main() -> i32 { def x: result<" + type +
                           "> = answer(); if x.erroneous { return 1; } if x.value != " + literal +
                           " || " + literal + " != x.value || x.value - " + step + " != " + lower +
                           " { return 2; } return 42; }");
        expect_run(invoke({file}), 42);
        expect_error(invoke({"--check", source(header + "def main() -> i32 { def x: result<" +
                                               type + "> = answer(); if x.value == " + literal +
                                               " { return 0; } return 1; }")}),
                     1, "requires a proven non-erroneous path");
    }
    expect_error(invoke({"--check", source(R"(
        def answer() -> result<f64> { return 42.0; }
        def main() -> i32 {
            def x: result<f64> = answer(); if x.erroneous { return 1; }
            def other: f32 = 42.0;
            if x.value == other { return 0; } return 1;
        }
    )")}),
                 1, "Type mismatch in binary expression");
}

TEST_F(ResultTest, StandardParserExposesCheckedAlternative) {
    auto file = source(R"(
        import "@std";
        def main() -> i32 {
            def valid: result<i32> = std.parse_i32_checked("42");
            if valid.erroneous { return 1; }
            def invalid: result<i32> = std.parse_i32_checked("oops");
            if invalid.erroneous {
                std.println(invalid.error.message);
                return valid.value;
            }
            return 2;
        }
    )");
    expect_run(invoke({file}), 42, "invalid signed decimal i32\n");
}

TEST_F(ResultTest, AggregatePayloadsKeepTheirSourceTypesInJitAndAot) {
    auto file = source(R"(
        import "@strings";
        def struct Pair { def left: i32, def right: i32, }
        def make_pair(fail: bool) -> result<Pair> {
            if fail { return error("bad pair"); }
            return Pair { left: 17, right: 25 };
        }
        def make_text(fail: bool) -> result<string> {
            if fail { return error("bad text"); }
            return "hello";
        }
        def make_array(fail: bool) -> result<[i32; 2]> {
            if fail { return error("bad array"); }
            def values: [i32; 2] = {10, 32};
            return values;
        }
        def main() -> i32 {
            def pair: result<Pair> = make_pair(false);
            if pair.erroneous { return 1; }
            def text: result<string> = make_text(false);
            if text.erroneous { return 2; }
            def values: result<[i32; 2]> = make_array(false);
            if values.erroneous { return 3; }
            def bad: result<Pair> = make_pair(true);
            if !bad.erroneous { return 4; }
            if !strings.equal(bad.error.message, "bad pair") { return 5; }
            if !strings.equal(text.value, "hello") { return 6; }
            return pair.value.left + pair.value.right + values.value[0];
        }
    )");
    auto ir = invoke({"--emit-ir", file});
    EXPECT_EQ(ir.status, 0) << ir.err;
    EXPECT_NE(ir.out.find("!gloin.result<!gloin.struct<"), std::string::npos);
    expect_run(invoke({"--jit", file}), 52);
    const auto executable = directory + "/result-aggregate-aot";
    expect_success(invoke_raw({"-o", executable, file}), "");
    const auto err = directory + "/result-aggregate-aot.err";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, std::nullopt, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                               10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 52) << message << read(err);
    EXPECT_TRUE(read(err).empty());
}

TEST_F(ResultTest, CheckedStringAccessDistinguishesValidZeroAndEmptyFromFailure) {
    auto file = source(R"(
        import "@strings";
        def main() -> i32 {
            def zero: result<u8> = strings.byte_at_checked("a\0b", 1);
            if zero.erroneous { return 1; }
            def bad_byte: result<u8> = strings.byte_at_checked("abc", 18446744073709551615);
            if !bad_byte.erroneous { return 2; }
            def zero_byte: u8 = 0;
            if zero.value != zero_byte ||
                !strings.equal(bad_byte.error.message, "string byte index is out of range") {
                return 3;
            }
            def empty: result<string> = strings.slice_bytes_checked("abc", 3, 0);
            if empty.erroneous { return 4; }
            def bad_slice: result<string> =
                strings.slice_bytes_checked("abc", 2, 18446744073709551615);
            if !bad_slice.erroneous { return 5; }
            if !strings.is_empty(empty.value) ||
                !strings.equal(bad_slice.error.message, "string byte range is out of bounds") {
                return 6;
            }
            return 42;
        }
    )");
    expect_run(invoke({"--jit", file}), 42);
}

TEST_F(ResultTest, NegatedAndUnlessChecksKeepTheCorrectVariantProof) {
    auto file = source(R"(
        def compute(fail: bool) -> result<i32> {
            if fail { return error("failed"); }
            return 42;
        }
        def use(value: result<i32>) -> i32 {
            if !value.erroneous { return value.value; }
            return 0;
        }
        def main() -> i32 {
            def first: result<i32> = compute(false);
            unless first.erroneous { return use(compute(false)); }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(ResultTest, RequiresProofAndHandling) {
    auto require_error = [&](std::string program, std::string reason) {
        auto file = source(std::move(program));
        auto result = invoke({"--check", file});
        expect_error(result, 1, reason);
    };
    const std::string header = R"(
        def compute() -> result<i32> { return 7; }
    )";
    require_error(header + R"(
        def main() -> i32 {
            def x: result<i32> = compute();
            return x.value;
        }
    )",
                  "requires a proven non-erroneous path");
    require_error(header + R"(
        def main() -> i32 { compute(); return 0; }
    )",
                  "result cannot be discarded");
    require_error(header + R"(
        def main() -> i32 {
            def x: result<i32> = compute();
            return 0;
        }
    )",
                  "must be checked with .erroneous");
    require_error(header + R"(
        def main() -> i32 {
            def x: result<i32> = compute();
            if true { return 0; }
            if x.erroneous { return 1; }
            return x.value;
        }
    )",
                  "must be checked with .erroneous");
    require_error(header + R"(
        def main() -> i32 {
            def x: result<i32> = compute();
            if x.erroneous { return x.value; }
            return 0;
        }
    )",
                  "requires a proven non-erroneous path");
    require_error(header + R"(
        def main() -> i32 {
            def mut x: result<i32> = compute();
            if x.erroneous { return 0; }
            x = compute();
            return x.value;
        }
    )",
                  "requires a proven non-erroneous path");
}

TEST_F(ResultTest, RejectsInvalidTypeAndConstruction) {
    auto invalid_type = source(R"(
        def bad() -> result<error> { return error("bad"); }
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({"--check", invalid_type}), 1,
                 "result value must be a supported type other than error");
    auto dynamic_message = source(R"(
        def bad(message: string) -> result<i32> { return error(message); }
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({"--check", dynamic_message}), 1,
                 "error construction requires one string literal");
    auto nested = source(R"(
        def main() -> i32 {
            def values: [result<i32>; 2];
            return 0;
        }
    )");
    expect_error(invoke({"--check", nested}), 1, "Invalid fixed-array type");
    auto field = source(R"(
        def struct Box { def value: result<i32>, }
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({"--check", field}), 1, "Result fields are not supported");
    auto unhandled_parameter = source(R"(
        def forget(value: result<i32>) -> i32 { return 0; }
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({"--check", unhandled_parameter}), 1, "must be checked with .erroneous");
    auto implicit_forward = source(R"(
        def compute() -> result<i32> { return 1; }
        def forward() -> result<i32> { return compute(); }
        def main() -> i32 { return 0; }
    )");
    expect_error(invoke({"--check", implicit_forward}), 1, "result return must be a value");
}

TEST_F(ResultTest, ErrorDetailsSurviveCopiesForwardingAndMatchingInJitAndAot) {
    auto file = source(R"(
        import "@strings";
        def next(sequence: &i32) -> i32 {
            *sequence = *sequence + 1;
            return *sequence;
        }
        def fail(sequence: &i32) -> result<i32> {
            return error("detailed failure", next(sequence), next(sequence));
        }
        def forward(sequence: &i32) -> result<i32> {
            def attempt: result<i32> = fail(sequence);
            if attempt.erroneous {
                def original: error = attempt.error;
                def copied: error = original;
                return copied;
            }
            return attempt.value;
        }
        def inspect(problem: error) -> i32 {
            if !strings.equal(problem.message, "detailed failure") { return 10; }
            if problem.code != 1 || problem.os_error != 2 { return 11; }
            return 42;
        }
        def empty_failure() -> result<void> { return error("unspecified"); }
        def main() -> i32 {
            def plain: result<void> = empty_failure();
            if !plain.erroneous { return 1; }
            if plain.error.code != 0 || plain.error.os_error != 0 { return 2; }
            def bounds: error = error("bounds", -2147483648, 2147483647);
            if bounds.code != -2147483648 || bounds.os_error != 2147483647 { return 3; }
            def mut sequence: i32 = 0;
            def answer: i32 = match forward(&sequence) {
                case value(number) => 4;
                case error(problem) => inspect(problem);
            };
            if sequence != 2 { return 5; }
            return answer;
        }
    )");
    auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation :
         {"!gloin.error", "gloin.error_literal", "gloin.error_message", "gloin.error_code",
          "gloin.error_os_error", "gloin.result_failure"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({"--jit", file}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/error-details" + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        const auto err = executable + ".err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, std::nullopt, err};
        std::string message;
        bool launch_failed = false;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10,
                                            0, &message, &launch_failed),
                  42)
            << message << read(err);
        EXPECT_FALSE(launch_failed) << message;
        EXPECT_TRUE(read(err).empty());
    }
}

TEST_F(ResultTest, ErrorDetailsRejectInvalidConstructorsWritesAndUnprovenAccess) {
    for (const auto *expression :
         {"error()", "error(\"bad\", 1)", "error(\"bad\", 1, 2, 3)", "error(message, 1, 2)"}) {
        expect_error(invoke({"--check", source("def main() -> i32 { def message: string = "
                                               "\"dynamic\"; def problem: error = " +
                                               std::string(expression) + "; return 0; }")}),
                     1, "error construction requires one string literal");
    }
    for (const auto *arguments : {"true, 0", "0, false", "wide, 0", "0, wide"}) {
        expect_error(
            invoke(
                {"--check",
                 source(
                     "def main() -> i32 { def wide: i64 = 1; def problem: error = error(\"bad\", " +
                     std::string(arguments) + "); return 0; }")}),
            1, "error code and os_error must have type i32");
    }
    for (const auto *field : {"code", "os_error"}) {
        const auto result = invoke(
            {"--check",
             source("def main() -> i32 { def mut problem: error = error(\"bad\", 1, 2); problem." +
                    std::string(field) + " = 3; return 0; }")});
        expect_error(result, 1, "Result and error members are read-only");
        expect_error(
            invoke({"--check", source("def fail() -> result<i32> { return error(\"bad\", 1, 2); } "
                                      "def main() -> i32 { def attempt: result<i32> = fail(); "
                                      "return attempt.error." +
                                      std::string(field) + "; }")}),
            1, "requires a proven erroneous path");
    }
}
