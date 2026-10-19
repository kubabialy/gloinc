#include "support/cli_fixture.h"
#include "llvm/Support/Program.h"

namespace {
class SelectionTest : public gloin_test::CliFixture {
  protected:
    void all_modes(const std::string &text) {
        auto file = source(text);
        expect_success(invoke({"--check", file}), "");
        auto ir = invoke({"--emit-ir", file});
        ASSERT_EQ(ir.status, 0) << ir.err;
        EXPECT_NE(ir.out.find("gloin.match"), std::string::npos);
        auto llvm = invoke({"--emit-llvm", file});
        ASSERT_EQ(llvm.status, 0) << llvm.err;
        EXPECT_EQ(llvm.out.find("gloin.match"), std::string::npos);
        expect_run(invoke({"--jit", file}), 42);
        for (auto optimization : {"-O0", "-O2"}) {
            auto executable = directory + "/native";
            expect_success(invoke_raw({optimization, "-o", executable, file}), "");
            std::string message;
            bool failed = false;
            int status = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10,
                                                   0, &message, &failed);
            EXPECT_FALSE(failed) << message;
            EXPECT_EQ(status, 42) << message;
        }
    }
};
} // namespace

TEST_F(SelectionTest, DispatchEvaluatesSubjectOnceAndOnlySelectedArm) {
    all_modes(R"(
        def bump(n: &i32) -> i32 { *n = *n + 1; return *n; }
        def main() -> i32 {
            def mut n: i32 = 0;
            switch bump(&n) {
                case 1 { n = n + 10; }
                case 11 { return 1; }
                default { return 2; }
            }
            def value: i32 = match bump(&n) {
                case 12 => bump(&n);
                default => 1 / 0;
            };
            if n != 13 || value != 13 { return 3; }
            def unchanged: i32 = match false { case true => bump(&n); case false => 42; };
            switch n { case 0 { return 4; } }
            if n != 13 { return 5; }
            return unchanged;
        }
    )");
}

TEST_F(SelectionTest, StringsCompareCountedBytesAndSupportConstantLabels) {
    all_modes(R"(
        import "@strings";
        def const PING: string = "PING";
        def classify(text: string) -> i32 {
            return match text {
                case PING => 1;
                case "a\0b" => 2;
                case "" => 3;
                case "abcd" => 4;
                case "abc" => 5;
                default => 6;
            };
        }
        def main() -> i32 {
            def mut bytes: [u8; 3] = {97, 0, 98};
            def borrowed: string = strings.view_bytes(bytes[..]);
            if classify(borrowed) != 2 { return 1; }
            bytes[2] = 99;
            if classify(borrowed) != 6 { return 5; }
            if classify("a") != 6 || classify("a\0c") != 6 { return 2; }
            if classify("") != 3 || classify("PING") != 1 { return 3; }
            if classify("abc") != 5 || classify("abcd") != 4 { return 4; }
            switch "PING" { case "PING" { return 42; } default { return 5; } }
        }
    )");
}

TEST_F(SelectionTest, ExhaustiveEnumsMergeInitializationAndReturnPaths) {
    source("def pub enum Mode { Read, Write, Close }", "modes.gloin");
    all_modes(R"(
        import "./modes";
        def turn(mode: modes.Mode) -> modes.Mode {
            return match mode {
                case modes.Mode.Read => modes.Mode.Write;
                case modes.Mode.Write => modes.Mode.Close;
                case modes.Mode.Close => modes.Mode.Read;
            };
        }
        def classify(mode: modes.Mode) -> i32 {
            def answer: i32;
            switch mode {
                case modes.Mode.Read { answer = 40; }
                case modes.Mode.Write { answer = 41; }
                case modes.Mode.Close { answer = 42; }
            }
            return answer;
        }
        def main() -> i32 { return classify(turn(modes.Mode.Write)); }
    )");
}

TEST_F(SelectionTest, ResultBindingsAreScopedTypedAndExhaustive) {
    all_modes(R"(
        import "@strings";
        def attempt(ok: bool) -> result<i32> {
            if !ok { return error("failed"); }
            return 42;
        }
        def done() -> result<void> { return; }
        def load(p: &const i32) -> i32 { return *p; }
        def recover(r: result<i32>) -> i32 {
            return match r {
                case error(problem) => match strings.equal(problem.message, "failed") {
                    case true => 7;
                    case false => 9;
                };
                case value(answer) => answer;
            };
        }
        def main() -> i32 {
            if recover(attempt(false)) != 7 { return 1; }
            def succeeded: bool = match done() { case value() => true; case error(problem) => false; };
            if !succeeded { return 2; }
            def chosen: result<i32> = match true {
                case true => attempt(true);
                case false => attempt(false);
            };
            if match chosen { case value(n) => n; case error(e) => 0; } != 42 { return 4; }
            def r: result<i32> = attempt(true);
            def addressed: i32 = match r { case value(n) => load(&n); case error(e) => -1; };
            if addressed != 42 { return 3; }
            return match r { case value(n) => r.value; case error(e) => -1; };
        }
    )");
}

TEST_F(SelectionTest, GenericAndAggregateArmsPreserveSourceTypes) {
    all_modes(R"(
        def choose<T>(flag: bool, a: T, b: T) -> T {
            return match flag { case true => a; case false => b; };
        }
        def struct Pair { def value: i32, }
        def main() -> i32 {
            def mut x: i32 = 42;
            def p: *const i32 = match true { case true => &x; case false => null; };
            if p == null { return 1; }
            def pair: Pair = choose<Pair>(true, Pair { value: 42 }, Pair { value: 0 });
            if match true { case true => Pair { value: 42 }; case false => Pair { value: 0 }; }.value != 42 {
                return 4;
            }
            def a: [i32; 2] = match true { case true => {20, 22}; case false => zeroed; };
            if a[0] + a[1] != pair.value { return 2; }
            def slice: [const i32] = choose<[const i32]>(false, a[0..1], a[1..]);
            if slice[0] != 22 { return 3; }
            return *p;
        }
    )");
}

TEST_F(SelectionTest, IntegerLabelsUseExactWidthsAndConstantValues) {
    all_modes(R"(
        def const LAST: u64 = 18446744073709551615;
        def high(n: u64) -> i32 { return match n { case LAST => 40; default => 0; }; }
        def low(n: i8) -> i32 { return match n { case -128 => 2; default => 0; }; }
        def main() -> i32 { return high(18446744073709551615) + low(-128); }
    )");
}

TEST_F(SelectionTest, InvalidCasesCoverageAndTypesAreDiagnosed) {
    for (const auto &[text, error] :
         {std::pair{"return match true { case true => 1; };", "at least two arms"},
          std::pair{"return match true { case true => 1; case true => 2; };", "Duplicate case"},
          std::pair{"return match 1 { case 1 => 1; case 2 => 2; };", "require default"},
          std::pair{"return match true { case true => 1; case false => false; };",
                    "arm type mismatch"},
          std::pair{"return match true { case true => 1; case false => 2; default => 3; };",
                    "Unreachable default"},
          std::pair{"return match 1 { default => 1; case 1 => 2; };", "default must be last"},
          std::pair{"return match 1 { case 1 => 1; case 0x1 => 2; default => 3; };",
                    "Duplicate case"},
          std::pair{"switch 1.0 { default { return 1; } } return 0;", "Unsupported switch subject"},
          std::pair{"def n: i32 = 1; switch n { case n { return 1; } } return 0;",
                    "runtime binding"},
          std::pair{"switch 1 { case true { return 1; } } return 0;", "Case type"},
          std::pair{"def n: i32; switch 1 { case 1 { n = 1; } } return n;", "uninitialized"},
          std::pair{"return match true { case true { return 1; } case false => 2; };",
                    "Expected '=>'"},
          std::pair{"return match true { case true => 1 case false => 2; };", "Expected ';'"},
          std::pair{"return match true { case value(n) => n; default => 0; };",
                    "Result pattern requires"}}) {
        SCOPED_TRACE(text);
        expect_error(invoke({"--check", source(std::string("def main() -> i32 { ") + text + " }")}),
                     1, error);
    }
    for (const auto &[text, error] :
         {std::pair{"def target() -> i32 { return 42; } def main() -> i32 { match true { "
                    "case true => match true { case true => target; case false => target; }; "
                    "case false => target; }; return 0; }",
                    "Function values are not supported"},
          std::pair{"def enum E { A } def f(e: E) -> i32 { return match e { case E.A => 1; }; }",
                    "at least two arms"},
          std::pair{"def enum E { A, B, C } def f(e: E) -> i32 { return match e { case E.A => 1; "
                    "case E.B => 2; }; }",
                    "Non-exhaustive"},
          std::pair{"def f(r: result<void>) -> i32 { return match r { case value(n) => 1; case "
                    "error(e) => 2; }; }",
                    "without a binding"},
          std::pair{"def f(r: result<i32>) -> i32 { return match r { case value() => 1; case "
                    "error(e) => 2; }; }",
                    "requires a payload binding"},
          std::pair{"def f(r: result<i32>) -> i32 { def n: i32 = match r { case value(n) => n; "
                    "case error(e) => 0; }; return r.value; }",
                    "proven non-erroneous"},
          std::pair{"def f(r: result<i32>) -> i32 { def n: i32 = match r { case value(n) => n; "
                    "case error(e) => 0; }; return e.message; }",
                    "Undefined variable"},
          std::pair{"def attempt() -> result<i32> { return 42; } def main() -> i32 { "
                    "def mut r: result<i32> = attempt(); def n: i32 = match r { "
                    "case value(n) => n; case error(e) => 0; }; r = attempt(); return r.value; }",
                    "proven non-erroneous"},
          std::pair{"def f(r: result<i32>) -> i32 { return match r { case error(e) => 0; "
                    "default => r.value; }; }",
                    "proven non-erroneous"},
          std::pair{"def f(r: result<i32>) -> i32 { return match r { case value(n) => "
                    "r.error.message; case error(e) => 0; }; }",
                    "proven erroneous"}}) {
        SCOPED_TRACE(text);
        expect_error(invoke({"--check", source(text)}), 1, error);
    }
}
