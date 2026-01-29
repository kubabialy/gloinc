#include <gtest/gtest.h>
#include "../src/parser.h"

TEST(ParserTest, IntegerLiteral) {
    Lexer l("123");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "123");
}

TEST(ParserTest, FloatLiteral) {
    Lexer l("123.456");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "123.456");
}

TEST(ParserTest, Identifier) {
    Lexer l("myVar");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "myVar");
}

TEST(ParserTest, PrefixExpressionMinus) {
    Lexer l("-5");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(-5)");
}

TEST(ParserTest, PrefixExpressionBang) {
    Lexer l("!true");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(!true)");
}

TEST(ParserTest, InfixExpression) {
    Lexer l("1 + 2");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(1 + 2)");
}

TEST(ParserTest, OperatorPrecedence) {
    Lexer l("1 + 2 * 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be (1 + (2 * 3)) because * has higher precedence
    EXPECT_EQ(expr->to_string(), "(1 + (2 * 3))");
}

TEST(ParserTest, OperatorPrecedence2) {
    Lexer l("1 * 2 + 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be ((1 * 2) + 3)
    EXPECT_EQ(expr->to_string(), "((1 * 2) + 3)");
}

TEST(ParserTest, GroupedExpression) {
    Lexer l("(1 + 2) * 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be ((1 + 2) * 3)
    EXPECT_EQ(expr->to_string(), "((1 + 2) * 3)");
}

TEST(ParserTest, FunctionCall) {
    Lexer l("add(1, 2 * 3)");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "add(1, (2 * 3))");
}

TEST(ParserTest, FunctionCallNoArgs) {
    Lexer l("foo()");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "foo()");
}

TEST(ParserTest, MemberAccess) {
    Lexer l("person.name");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(person.name)");
}

TEST(ParserTest, ComplexMemberAccess) {
    Lexer l("a.b.c");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // ((a.b).c) - left associative
    EXPECT_EQ(expr->to_string(), "((a.b).c)");
}

TEST(ParserTest, IndexExpression) {
    Lexer l("arr[1]");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(arr[1])");
}

TEST(ParserTest, IndexExpressionComplex) {
    Lexer l("arr[i + 1]");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(arr[(i + 1)])");
}

TEST(ParserTest, ChainedCallsAndAccess) {
    Lexer l("obj.method(1).field");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // ((obj.method)(1)).field -> ((obj.method)(1).field)
    EXPECT_EQ(expr->to_string(), "((obj.method)(1).field)");
}

TEST(ParserTest, BooleanLiteral) {
    Lexer l("true != false");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(true != false)");
}

TEST(ParserTest, StringLiteral) {
    Lexer l("\"hello\"");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "\"hello\"");
}

TEST(ParserTest, VariableDeclaration) {
    Lexer l("def x: i32 = 5;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def x: i32 = 5;");
}

TEST(ParserTest, MutableVariableDeclaration) {
    Lexer l("def mut y: i32 = 10;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def mut y: i32 = 10;");
}

TEST(ParserTest, ReturnStatement) {
    Lexer l("return 0;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "return 0;");
}

TEST(ParserTest, ReturnVoidStatement) {
    Lexer l("return;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "return;");
}

TEST(ParserTest, BlockStatement) {
    Lexer l("{ def x: i32 = 5; return x; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "{ def x: i32 = 5; return x; }");
}

TEST(ParserTest, IfStatement) {
    Lexer l("if x > 5 { return true; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "if (x > 5) { return true; }");
}

TEST(ParserTest, IfElseStatement) {
    Lexer l("if x > 5 { return true; } else { return false; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "if (x > 5) { return true; } else { return false; }");
}

TEST(ParserTest, WhileStatement) {
    Lexer l("while i < 10 { i = i + 1; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "while (i < 10) { (i = (i + 1)); }");
}

TEST(ParserTest, DeferStatement) {
    Lexer l("defer x.free();");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "defer (x.free)();");
}

