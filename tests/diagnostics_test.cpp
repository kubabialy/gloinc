#include "codegen.h"
#include "compiler.h"
#include "parser.h"
#include "sema.h"
#include <gtest/gtest.h>

TEST(DiagnosticsTest, TokensOwnSourceAndTrackByteSpans) {
    auto token = Lexer("  value", "tokens.gloin").next_token();
    EXPECT_EQ(token.literal, "value");
    ASSERT_NE(token.span.source, nullptr);
    EXPECT_EQ(token.span.source->name, "tokens.gloin");
    EXPECT_EQ(token.span.begin, 2u);
    EXPECT_EQ(token.span.end, 7u);
}

TEST(DiagnosticsTest, AstOwnsSourceAfterParserDestruction) {
    auto parsed = GloinParser(Lexer("def x: i32 = 7;", "ast.gloin")).parse_checked_program();
    ASSERT_TRUE(parsed.success);
    ASSERT_EQ(parsed.program.size(), 1u);
    auto *decl = dynamic_cast<VariableDeclaration *>(parsed.program[0].get());
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->span.begin, 0u);
    EXPECT_EQ(decl->span.end, 15u);
    EXPECT_EQ(decl->name->span.begin, 4u);
    EXPECT_EQ(decl->name->span.end, 5u);
    EXPECT_EQ(decl->type->span.begin, 7u);
    EXPECT_EQ(decl->type->span.end, 10u);
    EXPECT_EQ(decl->initializer->span.begin, 13u);
    EXPECT_EQ(decl->initializer->span.end, 14u);
    EXPECT_EQ(decl->name->span.source->name, "ast.gloin");
}

TEST(DiagnosticsTest, MalformedProgramsNeverReturnPartialAst) {
    for (const auto &text :
         {"def good: i32 = 1; def broken: i32 = ;", "def main() -> i32 { return 0;",
          "def main(a: i32", "def main() -> i32 { return f(1; }", "def struct Broken {",
          "def main() -> i32 { return 0 }", "def main() -> void { foo() }", "import \"@std\"",
          "def x: i32 = 999999999999999999999999999999;"}) {
        GloinParser parser(Lexer(text, "broken.gloin"));
        auto result = parser.parse_checked_program();
        EXPECT_FALSE(result.success) << text;
        EXPECT_TRUE(result.program.empty()) << text;
        ASSERT_TRUE(parser.has_error()) << text;
        EXPECT_EQ(parser.diagnostics()->all().front().span.source->name, "broken.gloin");
    }
}

TEST(DiagnosticsTest, LexicalFailureStopsBeforeSemanticChecking) {
    mlir::MLIRContext context;
    for (std::string text : {std::string("def main() -> i32 { return $; }"),
                             std::string("def x: string = \"unterminated"),
                             (std::string("def x: i32 = 0;") + '\0' + "def y: i32 = 1;")}) {
        auto result = compile_source(text, "lex.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Lexing);
    }
}

TEST(DiagnosticsTest, ParseFailureStopsBeforeSemanticChecking) {
    mlir::MLIRContext context;
    auto result = compile_source("def x: i32 = missing; def y: i32 = ;", "parse.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    ASSERT_EQ(result.failed_stage, DiagnosticStage::Parsing);
    for (const auto &diagnostic : result.diagnostics->all())
        EXPECT_EQ(diagnostic.stage, DiagnosticStage::Parsing);
}

TEST(DiagnosticsTest, SemanticFailureHasExactSourceLocationAndNoModule) {
    mlir::MLIRContext context;
    auto result =
        compile_source("def main() -> i32 {\r\n    return missing;\r\n}", "missing.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
    std::ostringstream rendered;
    result.diagnostics->render(rendered);
    EXPECT_EQ(rendered.str(), "missing.gloin:2:12: error: Undefined variable 'missing'\n");
}

TEST(DiagnosticsTest, NonBooleanConditionsFailSemanticChecking) {
    mlir::MLIRContext context;
    for (const auto &keyword : {"if", "while"}) {
        auto result = compile_source(std::string("def main() -> i32 {\n    ") + keyword +
                                         " 1 { return 0; } return 1; }",
                                     "condition.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
        ASSERT_FALSE(result.diagnostics->all().empty());
        EXPECT_NE(result.diagnostics->all().front().message.find("condition must be bool"),
                  std::string::npos);
        auto [line, column] = result.diagnostics->all().front().span.source->line_column(
            result.diagnostics->all().front().span.begin);
        EXPECT_EQ(line, 2u);
        EXPECT_EQ(column, std::string(keyword).size() + 6);
    }
}

TEST(DiagnosticsTest, UnsupportedStatementsCannotDisappear) {
    mlir::MLIRContext context;
    for (const auto &text :
         {"def main() -> i32 { unless false { return 0; } return 1; }",
          "def main() -> i32 { for def mut i: i32 = 0; i < 2; i = i + 1 {} return 0; }",
          "import \"@std\"; def main() -> i32 { return 0; }"}) {
        auto result = compile_source(text, "unsupported.gloin", context);
        EXPECT_FALSE(result.success()) << text;
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic) << text;
    }
}

TEST(DiagnosticsTest, CodegenFailureDiscardsThePartialModule) {
    mlir::MLIRContext context;
    auto result =
        compile_source("def main() -> i32 {\n    return 1 != 2;\n}", "codegen.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Codegen);
    ASSERT_FALSE(result.diagnostics->all().empty());
    EXPECT_EQ(result.diagnostics->all().front().span.source->name, "codegen.gloin");
}

TEST(DiagnosticsTest, UnknownAstNodesFailInBothVisitors) {
    struct UnknownStatement : Statement {
        std::string to_string() const override { return "unknown"; }
    };
    struct UnknownExpression : Expression {
        std::string to_string() const override { return "unknown"; }
    };
    std::vector<std::unique_ptr<Statement>> program;
    program.push_back(std::make_unique<UnknownStatement>());
    Sema sema;
    EXPECT_FALSE(sema.check_program(program));
    mlir::MLIRContext context;
    CodeGen codegen(context);
    EXPECT_FALSE(codegen.generate(program));
    EXPECT_TRUE(codegen.diagnostics()->has_errors());
    Sema expression_sema;
    UnknownExpression unknown;
    EXPECT_EQ(expression_sema.check_expression(&unknown), nullptr);
    EXPECT_TRUE(expression_sema.has_error());
}

TEST(DiagnosticsTest, SuccessfulCompilationOwnsModuleWithSourceLocations) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 { return 42; }", "ok.gloin", context);
    ASSERT_TRUE(result.success());
    EXPECT_FALSE(result.failed_stage.has_value());
    auto main = result.module->lookupSymbol<mlir::func::FuncOp>("main");
    ASSERT_TRUE(main);
    auto loc = llvm::dyn_cast<mlir::FileLineColLoc>(main.getLoc());
    ASSERT_TRUE(loc);
    EXPECT_EQ(loc.getFilename().str(), "ok.gloin");
    EXPECT_EQ(loc.getLine(), 1u);
    EXPECT_EQ(loc.getColumn(), 1u);
}

TEST(DiagnosticsTest, StageErrorsDoNotPrintImplicitly) {
    mlir::MLIRContext context;
    testing::internal::CaptureStderr();
    auto result = compile_source("def main() -> i32 { return missing; }", "quiet.gloin", context);
    auto output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(output.empty());
    EXPECT_FALSE(result.success());
    EXPECT_TRUE(result.diagnostics->has_errors());
}

TEST(DiagnosticsTest, CodegenRejectsUnaddressableAssignmentsAndUnknownExpressions) {
    mlir::MLIRContext context;
    for (const auto &text :
         {"def main() -> i32 { def x: i32 = 1; x = 2; return x; }",
          "def main() -> i32 { return -1; }", "def main() -> i32 { return missing(); }",
          "def main() -> i32 { if 1 { return 0; } return 1; }"}) {
        GloinParser parser(Lexer(text, "direct.gloin"));
        auto parsed = parser.parse_checked_program();
        ASSERT_TRUE(parsed.success) << text;
        CodeGen codegen(context);
        EXPECT_FALSE(codegen.generate(parsed.program)) << text;
        ASSERT_TRUE(codegen.diagnostics()->has_errors());
        EXPECT_EQ(codegen.diagnostics()->all().front().span.source->name, "direct.gloin");
    }
}

TEST(DiagnosticsTest, VoidCallsAreAllowedOnlyAsStatements) {
    mlir::MLIRContext context;
    auto good = compile_source("def work() -> void {} def main() -> i32 { work(); return 0; }",
                               "void.gloin", context);
    EXPECT_TRUE(good.success());
    GloinParser parser(
        Lexer("def work() -> void {} def main() -> i32 { return work(); }", "void.gloin"));
    auto parsed = parser.parse_checked_program();
    ASSERT_TRUE(parsed.success);
    CodeGen codegen(context);
    EXPECT_FALSE(codegen.generate(parsed.program));
    ASSERT_TRUE(codegen.diagnostics()->has_errors());
    EXPECT_EQ(codegen.diagnostics()->all().front().message,
              "A void call cannot be used as a value");
}

TEST(DiagnosticsTest, GeneratedExpressionUsesItsOwnSourceLocation) {
    mlir::MLIRContext context;
    auto result =
        compile_source("def main() -> i32 {\n    return 42;\n}", "expression.gloin", context);
    ASSERT_TRUE(result.success());
    auto main = result.module->lookupSymbol<mlir::func::FuncOp>("main");
    auto constant = llvm::dyn_cast<mlir::arith::ConstantIntOp>(main.getBody().front().front());
    ASSERT_TRUE(constant);
    auto loc = llvm::dyn_cast<mlir::FileLineColLoc>(constant.getLoc());
    ASSERT_TRUE(loc);
    EXPECT_EQ(loc.getFilename().str(), "expression.gloin");
    EXPECT_EQ(loc.getLine(), 2u);
    EXPECT_EQ(loc.getColumn(), 12u);
}

TEST(DiagnosticsTest, ReservedLegacyTokensDoNotActivateParserExtensions) {
    mlir::MLIRContext context;
    for (std::string expression :
         {"spawn work()", "await work()", "fn", "match", "'a'", "string"}) {
        auto result = compile_source("def main() -> i32 { return " + expression + "; }",
                                     "reserved.gloin", context);
        EXPECT_FALSE(result.success()) << expression;
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Parsing) << expression;
        EXPECT_FALSE(result.module);
    }
    for (std::string text :
         {"def main() -> i32 { return 0..10; }", "def main() -> i32 { return 0=>10; }",
          "def in: i32 = 0;", "def _: i32 = 0;"}) {
        auto result = compile_source(text, "reserved.gloin", context);
        EXPECT_FALSE(result.success()) << text;
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Parsing) << text;
    }
}

TEST(DiagnosticsTest, EncodingErrorsInTrailingCommentsPreventCompilation) {
    mlir::MLIRContext context;
    for (const auto &suffix : {std::string("\xff"), std::string(1, '\0')}) {
        auto result = compile_source("def main() -> i32 { return 0; } //" + suffix,
                                     "encoding.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Lexing);
        EXPECT_FALSE(result.module);
    }
}

TEST(DiagnosticsTest, LiteralAndKeywordTokensCannotSubstituteForTypes) {
    for (std::string type : {"true", "return", "spawn", "\"i32\"", "'i'"}) {
        GloinParser parser(Lexer("def x: " + type + ";", "types.gloin"));
        EXPECT_FALSE(parser.parse_checked_program().success) << type;
    }
    GloinParser parser(Lexer("def x: int; def y: usize; def s: string;", "types.gloin"));
    auto result = parser.parse_checked_program();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.program.size(), 3u);
    for (size_t i = 0; i < result.program.size(); ++i) {
        auto *decl = dynamic_cast<VariableDeclaration *>(result.program[i].get());
        ASSERT_NE(decl, nullptr);
        EXPECT_EQ(decl->type->value, (std::vector<std::string>{"int", "usize", "string"})[i]);
    }
}
