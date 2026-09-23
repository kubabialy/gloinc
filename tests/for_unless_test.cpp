#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include <gtest/gtest.h>

namespace {
void execute_module(mlir::ModuleOp module, int expected) {
    ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
    auto value = gloin_test::run_external_module(module, {gloin_test::mlir_opt, {}},
                                                 {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(value)) << llvm::toString(value.takeError());
    EXPECT_EQ(*value, expected);
}
void execute(const std::string &source, int expected = 42) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "for-unless.gloin", context, CompilationMode::Executable);
    std::ostringstream errors;
    result.diagnostics->render(errors);
    ASSERT_TRUE(result.success()) << errors.str();
    ASSERT_NO_FATAL_FAILURE(execute_module(*result.module, expected));
}
void reject(const std::string &body, DiagnosticStage stage = DiagnosticStage::Semantic) {
    SCOPED_TRACE(body);
    mlir::MLIRContext context;
    auto result =
        compile_source("def main() -> i32 { " + body + " }", "invalid-loop.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, stage);
    EXPECT_TRUE(result.diagnostics->has_errors());
}
} // namespace

TEST(ForUnlessTest, ParsesEveryCombinationOfOmittedComponents) {
    for (unsigned mask = 0; mask < 8; ++mask) {
        const std::string header = std::string(mask & 1 ? "def mut i: i32 = 0" : "") + "; " +
                                   (mask & 2 ? "true" : "") + "; " + (mask & 4 ? "i = i + 1" : "");
        SCOPED_TRACE(header);
        GloinParser parser{Lexer("def main() -> void { for " + header + " {} }")};
        auto parsed = parser.parse_checked_program();
        ASSERT_TRUE(parsed.success);
        auto *fn = dynamic_cast<FunctionDefinition *>(parsed.program[0].get());
        ASSERT_NE(fn, nullptr);
        auto *loop = dynamic_cast<ForStatement *>(fn->body->statements[0].get());
        ASSERT_NE(loop, nullptr);
        EXPECT_EQ(static_cast<bool>(loop->init), static_cast<bool>(mask & 1));
        EXPECT_EQ(static_cast<bool>(loop->condition), static_cast<bool>(mask & 2));
        EXPECT_EQ(static_cast<bool>(loop->increment), static_cast<bool>(mask & 4));
        EXPECT_FALSE(loop->to_string().empty());
    }
}

TEST(ForUnlessTest, RejectsMalformedHeadersAndUnsupportedVariants) {
    for (const std::string body :
         {"for ; {}", "for (def mut i: i32 = 0; i < 3; i = i + 1) {}", "for ;; def i: i32 = 0 {}",
          "for ;; f(), g() {}", "for ;; i = j = 1 {}", "for def f() -> void {} ;; {}",
          "for ;; return;", "unless false {} else {}", "for x in 0..3 {}", "for ;; { continue; }",
          "for defer f();; {}"})
        reject(body + " return 0;", DiagnosticStage::Parsing);
}

TEST(ForUnlessTest, RequiresBooleanConditionsInSourceAndBackend) {
    for (const std::string value : {"1", "0", "1.0"}) {
        for (const std::string header : {"unless " + value, "for ; " + value + ";"}) {
            const auto source = "def main() -> i32 { " + header + " {} return 42; }";
            mlir::MLIRContext context;
            auto result = compile_source(source, "condition.gloin", context);
            ASSERT_FALSE(result.success());
            EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
            const auto &diagnostic = result.diagnostics->all().front();
            EXPECT_NE(diagnostic.message.find("condition must be bool"), std::string::npos);
            EXPECT_EQ(diagnostic.span.source->text.substr(
                          diagnostic.span.begin, diagnostic.span.end - diagnostic.span.begin),
                      value);
            GloinParser parser{Lexer(source)};
            auto parsed = parser.parse_checked_program();
            ASSERT_TRUE(parsed.success);
            CodeGen backend(context);
            EXPECT_FALSE(backend.generate_unchecked_for_testing(parsed.program));
            EXPECT_TRUE(backend.diagnostics()->has_errors());
        }
    }
}

TEST(ForUnlessTest, UnlessSelectsFalseArmAndPreservesReturningPaths) {
    execute(R"(
        def choose(b: bool) -> i32 {
            def x: i32;
            unless b { return 10; }
            x = 11;
            return x;
        }
        def main() -> i32 {
            def mut x: i32 = 0;
            unless false { x = x + 20; }
            unless true { x = 99; }
            unless false { unless true { return 0; } }
            return x + choose(false) + choose(true) + 1;
        }
    )");
}

TEST(ForUnlessTest, ThreeIterationCounterAndZeroIterationLoopExecute) {
    execute(R"(
        def main() -> i32 {
            def mut count: i32 = 0;
            for def mut i: i32 = 0; i < 3; i = i + 1 { count = count + 1; }
            for ; false; count = count / 0 { return 99; }
            return count;
        }
    )",
            3);
}

TEST(ForUnlessTest, HeaderScopeShadowsOuterAndBodyShadowsHeader) {
    execute(R"(
        def main() -> i32 {
            def i: i32 = 2;
            def mut total: i32 = 0;
            for def mut i: i32 = i; i < 4; i = i + 1 {
                def i: i32 = 10;
                total = total + i;
            }
            for def const i: i32 = 20; i < 0; {} return total + i + 20;
        }
    )");
}

TEST(ForUnlessTest, RejectsLeakingNamesAndChecksUnexecutedBodiesAndUpdates) {
    for (const std::string body :
         {"for def i: i32 = 0; false; {} return i;",
          "for ; false; x = 1 { def mut x: i32 = 0; } return 0;",
          "unless true { def x: i32 = 1; } return x;", "unless true { missing(); } return 0;",
          "for ; false; { missing(); } return 0;", "for ;; missing() { return 1; } return 0;",
          "for ;; { return missing; } return 0;", "for ;; { return 1; return 2; } return 0;",
          "unless false { return 1; return 2; } return 0;",
          "def mut x: i32 = 0; for ; false; x = true {} return 0;"})
        reject(body);
}

TEST(ForUnlessTest, InitializerEffectsAreDefiniteButRepeatedEffectsAreNot) {
    execute(R"(
        def main() -> i32 {
            def x: i32;
            for x = 42; false; {} return x;
        }
    )");
    for (const std::string body : {"def mut x: i32; for ; true; { x = 1; } return x;",
                                   "def mut x: i32; for ; true; x = 1 {} return x;",
                                   "def mut x: i32; for ;; { x = 1; } return x;",
                                   "def mut x: i32; unless false { x = 1; } return x;",
                                   "def mut x: i32; for ; x < 3; x = 1 {} return 0;",
                                   "def mut x: i32; for ;; x + 1 { x = 1; return 1; } return 0;"})
        reject(body);
}

TEST(ForUnlessTest, UpdateUsesOnlyContinuingBodyInitialization) {
    execute(R"(
        def choose_path(b: bool) -> i32 {
            def mut x: i32;
            def mut i: i32 = 0;
            for ; i < 1; i = x {
                if b { return 7; } else { x = 1; }
            }
            return 35;
        }
        def main() -> i32 { return choose_path(true) + choose_path(false); }
    )");
    reject("def mut x: i32; for ; false; x + 1 { if true { x = 1; } } return 0;");
}

TEST(ForUnlessTest, RejectsRepeatedWritesToImmutableBindings) {
    for (const std::string body : {"for def i: i32 = 0; i < 3; i = i + 1 {} return 0;",
                                   "for def i: i32; false; i = 1 {} return 0;",
                                   "def x: i32; for ; false; { x = 1; } return 0;",
                                   "def x: i32; for ; false; x = 1 {} return 0;",
                                   "for def mut i: i32 = 0; i < 1; i = i + 1 { def x: i32; for ; "
                                   "false; x = 1 {} } return 0;"})
        reject(body);
    execute(R"(
        def main() -> i32 {
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                def x: i32; x = 14; total = total + x;
            }
            return total;
        }
    )");
}

TEST(ForUnlessTest, OmittedComponentsVoidCallsAndDiscardedExpressionsExecute) {
    execute(R"(
        def noop() -> void {}
        def main() -> i32 {
            def mut i: i32 = 0;
            for noop(); i < 1; noop() { i = i + 1; }
            for ; i < 2; { i = i + 1; }
            for ; i < 3; i + 1 { i = i + 1; }
            for ;; { return i + 39; } return 0;
        }
    )");
    reject("for ;; { return 42; }");
    reject("unless false { return 42; }");
}

TEST(ForUnlessTest, NestedLoopsAndReturningArmsSkipUpdates) {
    execute(R"(
        def choose(b: bool) -> i32 {
            def mut i: i32 = 1;
            for ;; i = i / 0 { if b { return 10; } else { return 11; } }
            return 0;
        }
        def search() -> i32 {
            for def mut i: i32 = 0; i < 3; i = i + 1 {
                for def mut j: i32 = 0; j < 3; j = j + 1 {
                    unless i != 1 || j != 2 { return 21; }
                }
            }
            return 0;
        }
        def main() -> i32 { return choose(true) + choose(false) + search(); }
    )");
}

TEST(ForUnlessTest, GuardedAndShortCircuitExpressionsKeepLoopContinuations) {
    execute(R"(
        def main() -> i32 {
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i / 2 < 2 && (i != 99 || false); i = i + 2 / 2 {
                unless i % 2 == 1 && true { total = total + 10; }
                if i % 2 == 1 { total = total + 11; }
            }
            return total;
        }
    )");
}

TEST(ForUnlessTest, MalformedAstInitializerCannotReturnOrDeclareFunction) {
    for (bool function_init : {false, true}) {
        GloinParser parser{Lexer("def main() -> i32 { for ;; {} return 42; }")};
        auto parsed = parser.parse_checked_program();
        ASSERT_TRUE(parsed.success);
        auto *fn = dynamic_cast<FunctionDefinition *>(parsed.program[0].get());
        auto *loop = dynamic_cast<ForStatement *>(fn->body->statements[0].get());
        GloinParser init_parser{Lexer(function_init ? "def nested() -> void {}" : "return 1;")};
        loop->init = init_parser.parse_statement();
        ASSERT_NE(loop->init, nullptr);
        mlir::MLIRContext context;
        CodeGen backend(context);
        EXPECT_FALSE(backend.generate_unchecked_for_testing(parsed.program));
        Sema sema;
        auto checked = sema.check_for_codegen(std::move(parsed.program));
        EXPECT_FALSE(checked);
    }
}

TEST(ForUnlessTest, ObservableOrderIsInitializerConditionBodyUpdateAndFinalCondition) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def mark(tag: i32) -> void {}
        def condition(answer: bool) -> bool { return answer; }
        def read_trace() -> i32 { return 0; }
        def main() -> i32 {
            def mut i: i32 = 0;
            for mark(1); condition(i < 2); mark(4) { mark(3); i = i + 1; }
            unless condition(true) { mark(9); }
            return read_trace();
        }
    )",
                                 "loop-effects.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(result.success());
    // Add observability to helper bodies only; loop/unless lowering is untouched.
    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();
    builder.setInsertionPointToStart(result.module->getBody());
    auto trace = builder.create<mlir::LLVM::GlobalOp>(loc, builder.getI32Type(), false,
                                                      mlir::LLVM::Linkage::Internal, "test.trace",
                                                      builder.getI32IntegerAttr(0));
    for (const std::string name : {"mark", "condition", "read_trace"}) {
        auto function = result.module->lookupSymbol<mlir::func::FuncOp>(name);
        builder.setInsertionPointToStart(&function.getBody().front());
        auto address = builder.create<mlir::LLVM::AddressOfOp>(loc, trace);
        auto prior = builder.create<mlir::LLVM::LoadOp>(loc, builder.getI32Type(), address);
        if (name == "read_trace") {
            function.getBody().front().getTerminator()->setOperand(0, prior);
        } else {
            mlir::Value tag =
                name == "mark" ? function.getArgument(0)
                               : builder.create<mlir::arith::ConstantIntOp>(loc, 2, 32).getResult();
            auto ten = builder.create<mlir::arith::ConstantIntOp>(loc, 10, 32);
            auto shifted = builder.create<mlir::arith::MulIOp>(loc, prior, ten);
            auto next = builder.create<mlir::arith::AddIOp>(loc, shifted, tag);
            builder.create<mlir::LLVM::StoreOp>(loc, next, address);
        }
    }
    execute_module(*result.module, 123423422);
}
