#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "support/cli_fixture.h"
#include <set>

namespace {
bool is_runtime_trap(const gloin_test::ProcessResult &result) {
    return !result.launch_failed && result.status == -2 &&
           result.message.find("timed out") == std::string::npos &&
           (result.message.find("Trace/BPT trap") != std::string::npos ||
            result.message.find("Trace/breakpoint trap") != std::string::npos ||
            result.message.find("Illegal instruction") != std::string::npos);
}

gloin_test::ProcessResult run_process(llvm::StringRef executable,
                                      llvm::ArrayRef<llvm::StringRef> arguments,
                                      unsigned timeout = 10) {
    gloin_test::ProcessResult result{};
    result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt, {}, timeout, 0,
                                              &result.message, &result.launch_failed);
    return result;
}

class ContinueTest : public gloin_test::CliFixture {
  protected:
    void executes(const std::string &text, int expected = 0) {
        const auto file = source(text);
        expect_run(invoke({file}), expected);
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto executable = directory + "/continue";
            const auto compiled = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(compiled.status, 0) << compiled.err;
            EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                      expected);
        }
    }
    void rejects(const std::string &text, const std::string &message,
                 DiagnosticStage stage = DiagnosticStage::Semantic) {
        mlir::MLIRContext context;
        auto result = compile_source(text, "continue.gloin", context);
        EXPECT_FALSE(result.success()) << text;
        EXPECT_EQ(result.failed_stage, stage) << text;
        std::ostringstream errors;
        result.diagnostics->render(errors);
        EXPECT_NE(errors.str().find(message), std::string::npos) << errors.str();
    }
};
} // namespace

TEST_F(ContinueTest, NestedLoopsSwitchAndUpdatesChooseTheNearestLoop) {
    executes(R"(
        def condition(i: i32, calls: &i32) -> bool {
            *calls = *calls + 1; return i < 4;
        }
        def step(i: &i32, calls: &i32) -> void {
            *calls = *calls + 1; *i = *i + 1;
        }
        def main() -> i32 {
            def mut checks: i32 = 0; def mut updates: i32 = 0;
            def mut total: i32 = 0;
            for def mut i: i32 = 0; condition(i,&checks); step(&i,&updates) {
                if i == 1 { continue; }
                def mut j: i32 = 0;
                while j < 2 {
                    j = j + 1;
                    unless j == 2 { continue; }
                    total = total + i;
                }
                switch i { case 0 { continue; } default { total = total + 10; } }
            }
            if total != 25 || checks != 5 || updates != 4 { return 1; }
            def mut i: i32 = 0;
            for ;; {
                if i == 3 { return 0; }
                i = i + 1;
                continue;
            }
            return 2;
        }
    )");
}

TEST_F(ContinueTest, DefersRemainFunctionScopedAndCaptureEachReachedIteration) {
    executes(R"(
        def mark(trace: &i32, digit: i32) -> void { *trace = *trace * 10 + digit; }
        def worker(trace: &i32) -> bool {
            for def mut i: i32 = 1; i < 4; i = i + 1 {
                defer mark(trace,i);
                if i < 3 { continue; }
                defer mark(trace,9);
            }
            return *trace == 0;
        }
        def main() -> i32 {
            def mut trace: i32 = 0;
            if !worker(&trace) || trace != 9321 { return 1; }
            return 0;
        }
    )");
}

TEST_F(ContinueTest, GenericCloningAndInitializationJoinIncludeContinueEdges) {
    executes(R"(
        def choose<T>(value: T) -> T {
            for def mut i: i32 = 0; i < 2; i = i + 1 {
                if i == 0 { continue; }
                return value;
            }
            return value;
        }
        def main() -> i32 {
            def mut x: i32;
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 3; i = x {
                if i == 0 { x = 1; continue; }
                if i == 1 { x = 2; continue; } else { x = 3; }
                total = total + 1;
            }
            if total != 1 || choose<i32>(42) != 42 || !choose<bool>(true) { return 1; }
            return 0;
        }
    )");
}

TEST_F(ContinueTest, ResultRefinementAndIterationLocalHandlingArePreserved) {
    executes(R"(
        def checked(i: i32) -> result<i32> {
            if i == 1 { return error("skip"); } return i;
        }
        def main() -> i32 {
            def outer: result<i32> = checked(4);
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                def item: result<i32> = checked(i);
                if item.erroneous { continue; }
                total = total + item.value;
            }
            if outer.erroneous { return 1; }
            if total != 2 || outer.value != 4 { return 2; }
            return 0;
        }
    )");
    rejects(R"(
        def f() -> result<i32> { return 1; }
        def test() -> void { while true { def r: result<i32> = f(); continue; } }
    )",
            "must be checked");
    rejects(R"(
        def f() -> result<i32> { return error("bad"); }
        def test() -> void {
            def r: result<i32> = f();
            for ; true; r.value { if r.erroneous { continue; } }
        }
    )",
            "proven");
}

TEST_F(ContinueTest, RejectsInvalidTransfersAndUnsafeUpdateReads) {
    for (const auto *body : {"continue;", "switch true { case true { continue; } }"})
        rejects(std::string("def test() -> void { ") + body + " }", "inside a loop body");
    for (const auto *body : {"while true { continue; return; }",
                             "while true { if true { continue; } else { return; } {} }"})
        rejects(std::string("def test() -> void { ") + body + " }", "Unreachable");
    rejects("def f(b: bool) -> void { def mut x: i32; "
            "for ;; x = x + 1 { if b { continue; } x = 1; } }",
            "initializ");
    rejects("def f() -> void { def mut x: i32; for ;; x = x + 1 { continue; } }", "initializ");
    rejects("def f() -> void { for ;; x = x + 1 { def mut x: i32 = 0; continue; } }", "Undefined");
    rejects("def f() -> i32 { while true { continue; } }", "without returning");
    for (const auto *body : {"while true { continue }", "while true { continue outer; }",
                             "for continue;; {}", "while true { defer continue; }"})
        rejects(std::string("def test() -> void { ") + body + " }", "", DiagnosticStage::Parsing);
}

TEST_F(ContinueTest, UpdateTrapsRemainObservableAfterContinue) {
    const auto file = source(R"(
        def main() -> i32 {
            def mut i: i8 = 127;
            for ; true; i = i + 1 { continue; }
            return 0;
        }
    )");
    const auto jit = invoke({file});
    EXPECT_TRUE(is_runtime_trap(jit)) << jit.message << jit.err;
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto executable = directory + "/trap";
        const auto built = invoke_raw({mode, "-o", executable, file});
        ASSERT_EQ(built.status, 0) << built.err;
        const auto native = run_process(executable, {executable});
        EXPECT_TRUE(is_runtime_trap(native)) << mode << ": " << native.message;
    }
}

TEST_F(ContinueTest, TrapChecksRejectTimeoutsLaunchFailuresAndOrdinaryExits) {
    const std::string fixture = gloin_test::process_fixture;
    const auto timed_out = run_process(fixture, {fixture, "hang", "unused"}, 1);
    ASSERT_FALSE(timed_out.launch_failed) << timed_out.message;
    ASSERT_EQ(timed_out.status, -2);
    ASSERT_NE(timed_out.message.find("timed out"), std::string::npos) << timed_out.message;
    EXPECT_FALSE(is_runtime_trap(timed_out));

    const std::string missing = directory + "/missing-executable";
    const auto not_started = run_process(missing, {missing});
    ASSERT_TRUE(not_started.launch_failed) << not_started.message;
    ASSERT_EQ(not_started.status, -1);
    EXPECT_FALSE(is_runtime_trap(not_started));

    const auto exited = run_process(fixture, {fixture, "fail", "unused"});
    ASSERT_FALSE(exited.launch_failed) << exited.message;
    ASSERT_EQ(exited.status, 7);
    EXPECT_FALSE(is_runtime_trap(exited));
}

TEST_F(ContinueTest, GeneratedControlFlowIsVerifiedAndUncheckedTargetsAreRejected) {
    const std::string text = R"(
        def main() -> i32 {
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                if i == 0 { continue; } else { return i; }
            }
            return 0;
        }
    )";
    mlir::MLIRContext context;
    auto result = compile_source(text, "continue.gloin", context);
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
    auto parsed = GloinParser(Lexer(text)).parse_checked_program();
    ASSERT_TRUE(parsed.success);
    CodeGen backend(context);
    EXPECT_FALSE(backend.generate_unchecked_for_testing(parsed.program));
}
