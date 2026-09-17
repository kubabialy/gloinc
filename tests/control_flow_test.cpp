#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include <gtest/gtest.h>
#include <set>

namespace {
void verify_cfg(mlir::ModuleOp module) {
    ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
    for (auto function : module.getOps<mlir::func::FuncOp>()) {
        ASSERT_FALSE(function.isExternal());
        std::set<mlir::Block *> reached;
        std::vector<mlir::Block *> pending{&function.getBody().front()};
        while (!pending.empty()) {
            auto *block = pending.back();
            pending.pop_back();
            if (!reached.insert(block).second)
                continue;
            ASSERT_FALSE(block->empty());
            EXPECT_TRUE(block->back().hasTrait<mlir::OpTrait::IsTerminator>());
            for (auto &operation : *block)
                if (operation.hasTrait<mlir::OpTrait::IsTerminator>())
                    EXPECT_EQ(&operation, &block->back());
            for (auto *successor : block->getSuccessors()) {
                EXPECT_EQ(successor->getParent(), &function.getBody());
                pending.push_back(successor);
            }
        }
        EXPECT_EQ(reached.size(), function.getBody().getBlocks().size())
            << function.getName().str();
    }
}

void execute_module(mlir::ModuleOp module, int expected) {
    ASSERT_NO_FATAL_FAILURE(verify_cfg(module));
    std::string ir;
    llvm::raw_string_ostream stream(ir);
    module.print(stream);
    auto value = gloin_test::run_external_mlir(ir, {gloin_test::mlir_opt, {}},
                                               {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(value)) << llvm::toString(value.takeError());
    EXPECT_EQ(*value, expected);
}
void execute(const std::string &source, int expected = 42) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "flow.gloin", context, CompilationMode::Executable);
    std::ostringstream errors;
    result.diagnostics->render(errors);
    ASSERT_TRUE(result.success()) << errors.str();
    ASSERT_NO_FATAL_FAILURE(execute_module(*result.module, expected));
}
} // namespace

TEST(ControlFlowTest, BackendRejectsStatementsAfterTerminatedPaths) {
    for (const std::string body :
         {"return 1; return 2;", "{ return 1; } def x: i32 = 2;",
          "if true { return 1; } else { return 2; } return 3;",
          "if true { if false { return 1; } else { return 2; } } else { return 3; } return 4;",
          "while true { return 1; return 2; } return 3;"}) {
        SCOPED_TRACE(body);
        GloinParser parser(Lexer("def f() -> i32 { " + body + " }", "unreachable.gloin"));
        auto parsed = parser.parse_checked_program();
        ASSERT_TRUE(parsed.success);
        mlir::MLIRContext context;
        CodeGen codegen(context, parser.diagnostics());
        mlir::OwningOpRef<mlir::ModuleOp> module(
            codegen.generate_unchecked_for_testing(parsed.program));
        EXPECT_FALSE(module);
        ASSERT_FALSE(codegen.diagnostics()->all().empty());
        EXPECT_EQ(codegen.diagnostics()->all().front().stage, DiagnosticStage::Codegen);
        EXPECT_NE(codegen.diagnostics()->all().front().message.find("Unreachable"),
                  std::string::npos);
    }
}

TEST(ControlFlowTest, CheckedSourceRejectsUnreachableStatementsBeforeGeneration) {
    for (const std::string body :
         {"return 1; {}", "if b { return 1; } else { return 2; } return 3;",
          "while b { { return 1; } def x: i32 = 2; } return 3;"}) {
        mlir::MLIRContext context;
        auto result =
            compile_source("def f(b: bool) -> i32 { " + body + " }", "flow.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
        ASSERT_FALSE(result.diagnostics->all().empty());
        EXPECT_NE(result.diagnostics->all().front().message.find("Unreachable"), std::string::npos);
    }
}

TEST(ControlFlowTest, ConditionsRequireBoolInSourceAndBackend) {
    for (const std::string kind : {"if", "while"}) {
        for (const std::string value : {"1", "0", "1.0"}) {
            const auto source = "def main() -> i32 { " + kind + " " + value + " {} return 42; }";
            mlir::MLIRContext context;
            auto result = compile_source(source, "condition.gloin", context);
            EXPECT_FALSE(result.success());
            EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
            ASSERT_FALSE(result.diagnostics->all().empty());
            auto span = result.diagnostics->all().front().span;
            ASSERT_TRUE(span.source);
            EXPECT_EQ(span.source->text.substr(span.begin, span.end - span.begin), value);
            GloinParser parser{Lexer(source)};
            auto parsed = parser.parse_checked_program();
            ASSERT_TRUE(parsed.success);
            CodeGen backend(context);
            EXPECT_FALSE(backend.generate_unchecked_for_testing(parsed.program));
            EXPECT_TRUE(backend.diagnostics()->has_errors());
        }
    }
}

TEST(ControlFlowTest, NestedBranchesAndElseIfExecuteEveryLeaf) {
    execute(R"(
        def choose(a: bool, b: bool, c: bool) -> i32 {
            if a {
                if b { if c { return 1; } else { return 2; } }
                else { if c { return 3; } else { return 4; } }
            } else if b { if c { return 5; } else { return 6; } }
            else if c { return 7; } else { return 8; }
        }
        def main() -> i32 {
            if choose(true, true, true) != 1 || choose(true, true, false) != 2 { return 0; }
            if choose(true, false, true) != 3 || choose(true, false, false) != 4 { return 0; }
            if choose(false, true, true) != 5 || choose(false, true, false) != 6 { return 0; }
            if choose(false, false, true) != 7 || choose(false, false, false) != 8 { return 0; }
            return 42;
        }
    )");
}

TEST(ControlFlowTest, PartialReturningArmsKeepOnlyTheirLiveContinuations) {
    execute(R"(
        def choose(a: bool, b: bool) -> i32 {
            def mut x: i32 = 0;
            if a { if b { return 10; } else { x = 11; } }
            else { if b { x = 12; } else { return 13; } }
            x = x + 1;
            return x;
        }
        def main() -> i32 {
            if choose(true, true) != 10 || choose(true, false) != 12 { return 0; }
            if choose(false, true) != 13 || choose(false, false) != 13 { return 0; }
            return 42;
        }
    )");
}

TEST(ControlFlowTest, NestedLoopsWithBranchesPreserveBothBackedges) {
    execute(R"(
        def main() -> i32 {
            def mut outer: i32 = 0;
            def mut total: i32 = 0;
            while outer < 3 {
                def mut inner: i32 = 0;
                while inner < 4 {
                    if inner % 2 == 0 { total = total + 2; }
                    else { if outer == 1 { total = total + 3; } else { total = total + 4; } }
                    inner = inner + 1;
                }
                outer = outer + 1;
            }
            return total;
        }
    )",
            34);
}

TEST(ControlFlowTest, ReturnsExitNestedLoopsWithoutExecutingLaterStores) {
    execute(R"(
        def search(limit: i32) -> i32 {
            def mut outer: i32 = 0;
            while outer < limit {
                def mut inner: i32 = 0;
                while inner < 3 {
                    if outer == 1 { if inner == 2 { return 42; } }
                    inner = inner + 1;
                }
                outer = outer + 1;
            }
            return 7;
        }
        def main() -> i32 {
            if search(0) != 7 || search(1) != 7 || search(3) != 42 { return 0; }
            return 42;
        }
    )");
}

TEST(ControlFlowTest, BothReturningLoopArmsHaveNoBackedge) {
    execute(R"(
        def choose(enabled: bool, side: bool) -> i32 {
            while enabled { if side { return 10; } else { { return 11; } } }
            return 21;
        }
        def main() -> i32 { return choose(true, true) + choose(true, false) + choose(false, true); }
    )");
}

TEST(ControlFlowTest, EmptyBodiesAndImplicitVoidReturnsProduceTerminatedBlocks) {
    execute(R"(
        def empty(a: bool) -> void {
            {}
            if a {} else {}
            if a { if a {} }
            while false {}
        }
        def early(a: bool) -> void { if a { return; } else { empty(a); } }
        def all(a: bool) -> void { if a { return; } else { return; } }
        def main() -> i32 { empty(true); empty(false); early(true); early(false); all(true); all(false); return 42; }
    )");
}

TEST(ControlFlowTest, ExpressionContinuationsFeedEnclosingBranchesAndLoops) {
    execute(R"(
        def value(x: i32) -> bool { return x / 2 <= 3; }
        def main() -> i32 {
            def mut i: i32 = 0;
            def mut total: i32 = 0;
            while i + 1 < 5 && (value(i) || i == 99) {
                if (i * 2 <= 6 && !(i == 99)) || i < 0 {
                    if i % 2 == 0 { total = total + 10; } else { total = total + 11; }
                } else { return 0; }
                i = i + 1;
            }
            return total;
        }
    )");
}

TEST(ControlFlowTest, ConditionsEvaluateOnceAndLoopsRecheckAfterEachIteration) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def mark(tag: i32, answer: bool) -> bool { return answer; }
        def read_trace() -> i32 { return 0; }
        def main() -> i32 {
            if mark(1, true) { if mark(2, false) {} else {} }
            else if mark(9, true) {}
            def mut i: i32 = 0;
            while mark(3, i < 3) { if mark(4, i == 1) {} i = i + 1; }
            while mark(5, false) {}
            return read_trace();
        }
    )",
                                 "condition-effects.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(result.success());
    // Instrument helper bodies only; source branches/loops/calls are untouched.
    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();
    builder.setInsertionPointToStart(result.module->getBody());
    auto trace = builder.create<mlir::LLVM::GlobalOp>(loc, builder.getI32Type(), false,
                                                      mlir::LLVM::Linkage::Internal, "test.trace",
                                                      builder.getI32IntegerAttr(0));
    for (const std::string name : {"mark", "read_trace"}) {
        auto function = result.module->lookupSymbol<mlir::func::FuncOp>(name);
        builder.setInsertionPointToStart(&function.getBody().front());
        auto address = builder.create<mlir::LLVM::AddressOfOp>(loc, trace);
        auto prior = builder.create<mlir::LLVM::LoadOp>(loc, builder.getI32Type(), address);
        if (name == "read_trace") {
            function.getBody().front().getTerminator()->setOperand(0, prior);
        } else {
            auto ten = builder.create<mlir::arith::ConstantIntOp>(loc, 10, 32);
            auto shifted = builder.create<mlir::arith::MulIOp>(loc, prior, ten);
            auto next = builder.create<mlir::arith::AddIOp>(loc, shifted, function.getArgument(0));
            builder.create<mlir::LLVM::StoreOp>(loc, next, address);
        }
    }
    execute_module(*result.module, 1234343435);
}

TEST(ControlFlowTest, EmptyLoopRepeatsConditionUntilItBecomesFalse) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def next() -> bool { return false; }
        def read_count() -> i32 { return 0; }
        def main() -> i32 { while next() {} return read_count(); }
    )",
                                 "empty-loop.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(result.success());
    // Make the condition stateful in test IR without adding language globals.
    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();
    builder.setInsertionPointToStart(result.module->getBody());
    auto counter = builder.create<mlir::LLVM::GlobalOp>(loc, builder.getI32Type(), false,
                                                        mlir::LLVM::Linkage::Internal, "test.count",
                                                        builder.getI32IntegerAttr(0));
    for (const std::string name : {"next", "read_count"}) {
        auto function = result.module->lookupSymbol<mlir::func::FuncOp>(name);
        builder.setInsertionPointToStart(&function.getBody().front());
        auto address = builder.create<mlir::LLVM::AddressOfOp>(loc, counter);
        auto prior = builder.create<mlir::LLVM::LoadOp>(loc, builder.getI32Type(), address);
        mlir::Value answer = prior;
        if (name == "next") {
            auto one = builder.create<mlir::arith::ConstantIntOp>(loc, 1, 32);
            auto four = builder.create<mlir::arith::ConstantIntOp>(loc, 4, 32);
            auto next = builder.create<mlir::arith::AddIOp>(loc, prior, one);
            builder.create<mlir::LLVM::StoreOp>(loc, next, address);
            answer = builder.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::slt, next,
                                                         four);
        }
        function.getBody().front().getTerminator()->setOperand(0, answer);
    }
    execute_module(*result.module, 4);
}
