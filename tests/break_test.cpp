#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "support/cli_fixture.h"
#include <set>

namespace {
class BreakTest : public gloin_test::CliFixture {
  protected:
    void executes(const std::string &text, int expected = 0) {
        const auto file = source(text);
        expect_run(invoke({file}), expected);
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto executable = directory + "/break";
            const auto compiled = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(compiled.status, 0) << compiled.err;
            EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                      expected);
        }
    }
    void rejects(const std::string &text, const std::string &message,
                 DiagnosticStage stage = DiagnosticStage::Semantic) {
        mlir::MLIRContext context;
        auto result = compile_source(text, "break.gloin", context);
        EXPECT_FALSE(result.success()) << text;
        EXPECT_EQ(result.failed_stage, stage) << text;
        std::ostringstream errors;
        result.diagnostics->render(errors);
        EXPECT_NE(errors.str().find(message), std::string::npos) << errors.str();
    }
};
} // namespace

TEST_F(BreakTest, NearestLoopSkipsUpdateAndConditionThroughSwitchAndUnless) {
    executes(R"(
        def condition(i: i32, calls: &i32) -> bool {
            *calls = *calls + 1; return i < 4;
        }
        def step(i: &i32, calls: &i32) -> void {
            *calls = *calls + 1; *i = *i + 1;
        }
        def main() -> i32 {
            def mut checks: i32 = 0; def mut updates: i32 = 0;
            def mut inner_checks: i32 = 0; def mut total: i32 = 0;
            for def mut i: i32 = 0; condition(i,&checks); step(&i,&updates) {
                if i == 1 { continue; }
                while condition(0,&inner_checks) {
                    switch i { case 0 { break; } default { break; } }
                }
                switch i { case 2 { break; } default { total = total + 10; } }
            }
            if total != 10 || checks != 3 || updates != 2 || inner_checks != 2 { return 1; }
            def mut n: i32 = 0; def mut while_checks: i32 = 0;
            while condition(n,&while_checks) {
                for ;; { break; }
                n = n + 1;
                unless n < 2 { break; }
                continue;
            }
            if n != 2 || while_checks != 2 { return 2; }
            for ;; { break; }
            return 0;
        }
    )");
}

TEST_F(BreakTest, BreakSkipsTrappingUpdatesAndLaterConditions) {
    executes(R"(
        def condition(calls: &i32) -> bool {
            *calls = *calls + 1;
            return 1 / (2 - *calls) > 0;
        }
        def main() -> i32 {
            def mut i: i8 = 127;
            for ; true; i = i + 1 { break; }
            def mut calls: i32 = 0;
            while condition(&calls) { break; }
            if i != 127 || calls != 1 { return 1; }
            return 0;
        }
    )");
}

TEST_F(BreakTest, ChecksReachedBeforeBreakStillTrap) {
    for (const auto *loop : {"while true { i = i + 1; break; }", "for ; i + 1 > 0; { break; }"}) {
        const auto file = source(std::string("def main() -> i32 { def mut i: i8 = 127; ") + loop +
                                 " return 0; }");
        auto expect_trap = [](const gloin_test::ProcessResult &result) {
            EXPECT_FALSE(result.launch_failed) << result.message;
            EXPECT_EQ(result.status, -2) << result.err;
            EXPECT_EQ(result.message.find("timed out"), std::string::npos) << result.message;
            EXPECT_TRUE(result.message.find("Trace/BPT trap") != std::string::npos ||
                        result.message.find("Trace/breakpoint trap") != std::string::npos ||
                        result.message.find("Illegal instruction") != std::string::npos)
                << result.message;
        };
        expect_trap(invoke({file}));
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto executable = directory + "/trap";
            const auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err;
            gloin_test::ProcessResult native{};
            native.status =
                llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10, 0,
                                          &native.message, &native.launch_failed);
            expect_trap(native);
        }
    }
}

TEST_F(BreakTest, DefersStayRegisteredUntilFunctionReturn) {
    executes(R"(
        def mark(trace: &i32, digit: i32) -> void { *trace = *trace * 10 + digit; }
        def worker(trace: &i32) -> bool {
            defer mark(trace,1);
            for def mut i: i32 = 2; i < 5; i = i + 1 {
                defer mark(trace,i);
                if i == 3 { break; }
                defer mark(trace,9);
            }
            if *trace != 0 { return false; }
            defer mark(trace,4);
            return true;
        }
        def main() -> i32 {
            def mut trace: i32 = 0;
            if !worker(&trace) || trace != 43921 { return 1; }
            return 0;
        }
    )");
}

TEST_F(BreakTest, GenericCloningAndUpdateJoinExcludeBreakEdges) {
    executes(R"(
        def choose<T>(value: T) -> T {
            while true { break; }
            for ;; { break; }
            return value;
        }
        def main() -> i32 {
            def mut x: i32;
            def mut updates: i32 = 0;
            for def mut i: i32 = 0; i < 5; i = x {
                if i == 2 { break; }
                x = i + 1;
                updates = updates + 1;
                continue;
            }
            if updates != 2 || choose<i32>(42) != 42 || !choose<bool>(true) { return 1; }
            return 0;
        }
    )");
}

TEST_F(BreakTest, ResultsOnExitedAndSurvivingScopesKeepTheirObligations) {
    executes(R"(
        def checked(i: i32) -> result<i32> {
            if i == 2 { return error("stop"); } return i;
        }
        def main() -> i32 {
            def outer: result<i32> = checked(4);
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 4; i = i + 1 {
                def item: result<i32> = checked(i);
                if item.erroneous { break; }
                total = total + item.value;
            }
            if outer.erroneous { return 1; }
            for def header: result<i32> = checked(3); !header.erroneous; {
                while true { break; }
                if header.erroneous { break; }
                total = total + header.value;
                break;
            }
            if outer.erroneous { return 1; }
            if total != 4 || outer.value != 4 { return 2; }
            return 0;
        }
    )");
    const std::string prefix = "def f() -> result<i32> { return 1; } ";
    for (const auto *loop : {"while true", "for ;;"}) {
        rejects(prefix + "def test() -> void { " + loop +
                    " { def r: result<i32> = f(); { break; } } }",
                "must be checked");
        rejects(prefix +
                    "def test() -> void { def mut r: result<i32> = f(); "
                    "if r.erroneous { return; } " +
                    loop + " { r = f(); break; } }",
                "must be checked");
        rejects(prefix +
                    "def test() -> i32 { def mut r: result<i32> = f(); "
                    "if r.erroneous { return 0; } " +
                    loop + " { r = f(); if r.erroneous { break; } } return r.value; }",
                "proven");
    }
    rejects(prefix + "def test() -> void { "
                     "for def r: result<i32> = f(); true; r.erroneous { break; } }",
            "must be checked");
    rejects(prefix + "def test() -> void { while true { def r: result<i32> = f(); "
                     "while true { break; } break; } }",
            "must be checked");
}

TEST_F(BreakTest, RejectsInvalidTransfersAndRetainsConservativeInitialization) {
    for (const auto *body :
         {"break;", "switch true { case true { break; } }", "while false {} break;"})
        rejects(std::string("def test() -> void { ") + body + " }", "inside a loop body");
    for (const auto *body :
         {"while true { break; return; }", "for ;; { if true { break; } else { continue; } {} }"})
        rejects(std::string("def test() -> void { ") + body + " }", "Unreachable");
    for (const auto *loop : {"while true", "for ;;"}) {
        rejects(std::string("def test() -> i32 { def mut x: i32; ") + loop +
                    " { x = 1; break; } return x; }",
                "initializ");
        rejects(std::string("def test() -> i32 { ") + loop + " { break; } }", "without returning");
    }
    rejects("def test() -> void { def mut x: i32; for ;; x = x + 1 { break; } }", "initializ");
    rejects("def test() -> void { for ;; x = x + 1 { def mut x: i32 = 0; break; } }", "Undefined");
    for (const auto *body :
         {"while true { break }", "while true { break outer; }", "while true { break 1; }",
          "for break;; {}", "for ;; break {}", "while true { defer break; }"})
        rejects(std::string("def test() -> void { ") + body + " }", "", DiagnosticStage::Parsing);
}

TEST_F(BreakTest, GeneratedExitEdgesAreVerifiedAndUncheckedTargetsAreRejected) {
    const std::string text = R"(
        def main() -> i32 {
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                if i == 0 { continue; } else { break; }
            }
            for ;; { break; }
            while true {
                switch true { case true { break; } case false { return 1; } }
            }
            return 0;
        }
    )";
    mlir::MLIRContext context;
    auto result = compile_source(text, "break.gloin", context);
    ASSERT_TRUE(result.success());
    ASSERT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
    for (auto function : result.module->getOps<mlir::func::FuncOp>()) {
        if (function.isExternal())
            continue;
        std::set<mlir::Block *> reached;
        std::vector<mlir::Block *> pending{&function.getBody().front()};
        while (!pending.empty()) {
            auto *block = pending.back();
            pending.pop_back();
            if (!reached.insert(block).second)
                continue;
            ASSERT_FALSE(block->empty());
            EXPECT_TRUE(block->back().hasTrait<mlir::OpTrait::IsTerminator>());
            for (auto &op : *block)
                if (op.hasTrait<mlir::OpTrait::IsTerminator>())
                    EXPECT_EQ(&op, &block->back());
            for (auto *successor : block->getSuccessors()) {
                EXPECT_EQ(successor->getParent(), &function.getBody());
                pending.push_back(successor);
            }
        }
        EXPECT_EQ(reached.size(), function.getBody().getBlocks().size());
    }
    auto parsed =
        GloinParser(Lexer("def test() -> void { while true { break; } }")).parse_checked_program();
    ASSERT_TRUE(parsed.success);
    CodeGen backend(context);
    EXPECT_FALSE(backend.generate_unchecked_for_testing(parsed.program));
}
