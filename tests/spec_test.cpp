#include <gtest/gtest.h>
#include "../src/parser.h"

TEST(SpecTest, PointerTypeParsing) {
    Lexer l("def x: *i32 = 0;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def x: *i32 = 0;");
}

TEST(SpecTest, ReferenceTypeParsing) {
    Lexer l("def x: &i32 = y;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def x: &i32 = y;");
}

TEST(SpecTest, ComplexPointerType) {
    Lexer l("def x: **i32 = 0;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def x: **i32 = 0;");
}

TEST(SpecTest, PackedStructParsing) {
    Lexer l("def packed struct(u32) Flags { def a: bit at 0, def b: u4 at 1 }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def packed struct(u32) Flags { def a: bit at 0, def b: u4 at 1, }");
}

TEST(SpecTest, DeferStatement) {
    Lexer l("defer free(ptr);");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "defer free(ptr);");
}

TEST(SpecTest, StringLiteral) {
    Lexer l("def s: string = \"Hello\";");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def s: string = \"Hello\";");
}

TEST(SpecTest, AddressOfOperator) {
    Lexer l("&x");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(&x)");
}

TEST(SpecTest, DereferenceOperator) {
    Lexer l("*ptr");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(*ptr)");
}
