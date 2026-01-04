#include <gtest/gtest.h>
#include "../src/lexer.h"
#include <vector>
#include <string>

// Helper to check token type and literal
void ExpectToken(const GloinToken& token, GloinTokenType type, const std::string& literal) {
    EXPECT_EQ(token.type, type) << "Expected token type " << type << " but got " << token.type << " for literal '" << literal << "'";
    EXPECT_EQ(token.literal, literal) << "Expected literal '" << literal << "' but got '" << token.literal << "'";
}

TEST(LexerTest, HandlesKeywords) {
    std::string input = "import extern fn def mut const return bool i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64 f128 string void true false null struct enum pub priv static self if unless else for while switch match case default break continue defer deferred spawnable run";
    Lexer lexer(input);

    std::vector<GloinTokenType> expected_types = {
        GLOIN_TOKEN_IMPORT, GLOIN_TOKEN_EXTERN, GLOIN_TOKEN_FN, GLOIN_TOKEN_DEF, GLOIN_TOKEN_MUT, GLOIN_TOKEN_CONST, GLOIN_TOKEN_RETURN,
        GLOIN_TOKEN_BOOL, GLOIN_TOKEN_I8, GLOIN_TOKEN_I16, GLOIN_TOKEN_I32, GLOIN_TOKEN_I64, GLOIN_TOKEN_I128,
        GLOIN_TOKEN_U8, GLOIN_TOKEN_U16, GLOIN_TOKEN_U32, GLOIN_TOKEN_U64, GLOIN_TOKEN_U128,
        GLOIN_TOKEN_F32, GLOIN_TOKEN_F64, GLOIN_TOKEN_F128, GLOIN_TOKEN_STRING, GLOIN_TOKEN_VOID,
        GLOIN_TOKEN_TRUE, GLOIN_TOKEN_FALSE, GLOIN_TOKEN_NULL, GLOIN_TOKEN_STRUCT, GLOIN_TOKEN_ENUM,
        GLOIN_TOKEN_PUB, GLOIN_TOKEN_PRIV, GLOIN_TOKEN_STATIC, GLOIN_TOKEN_SELF,
        GLOIN_TOKEN_IF, GLOIN_TOKEN_UNLESS, GLOIN_TOKEN_ELSE, GLOIN_TOKEN_FOR, GLOIN_TOKEN_WHILE,
        GLOIN_TOKEN_SWITCH, GLOIN_TOKEN_MATCH, GLOIN_TOKEN_CASE, GLOIN_TOKEN_DEFAULT,
        GLOIN_TOKEN_BREAK, GLOIN_TOKEN_CONTINUE, GLOIN_TOKEN_DEFER, GLOIN_TOKEN_DEFERRED,
        GLOIN_TOKEN_SPAWNABLE, GLOIN_TOKEN_RUN
    };

    for (auto type : expected_types) {
        GloinToken token = lexer.next_token();
        EXPECT_EQ(token.type, type);
    }
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesIdentifiers) {
    std::string input = "myVar _privateVar var123";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "myVar");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "_privateVar");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "var123");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesNumbers) {
    std::string input = "123 0 123.456 0.05 0x123 0xABC 0b101 0b0";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "123");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_FLOAT, "123.456");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_FLOAT, "0.05");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0x123");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0xABC");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0b101");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0b0");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesStrings) {
    std::string input = "\"hello world\" \"\"";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING, "hello world");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING, "");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesUnterminatedString) {
    std::string input = "\"hello world";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_UNKNOWN, "hello world");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesChars) {
    std::string input = "'a' '\\n' '\\''";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "a");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "\\n");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "\\'");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesComments) {
    std::string input = "val // this is a comment\n next";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "val");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NEWLINE, "\n");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "next");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesOperatorsAndPunctuation) {
    std::string input = "+ - * / = == => != < <= > >= & | ^ ~ % ? . , ; : :: { } [ ] ( ) ! # @ _ && || << >> += -= *= /= %= &= |= ^=";
    Lexer lexer(input);

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_PLUS, "+");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_MINUS, "-");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_MULTIPLY, "*");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_DIVIDE, "/");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_ASSIGN, "=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_EQ, "==");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_DOUBLE_ARROW, "=>");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NE, "!=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LT, "<");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LE, "<=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_GT, ">");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_GE, ">=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_AMPERSAND, "&");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_PIPE, "|");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CARET, "^");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_TILDE, "~");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_PERCENT, "%");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_QUESTION, "?");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_DOT, ".");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_COMMA, ",");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_SEMICOLON, ";");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_COLON, ":");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_DOUBLE_COLON, "::");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LBRACE, "{");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RBRACE, "}");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LBRACKET, "[");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RBRACKET, "]");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LPAREN, "(");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RPAREN, ")");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NOT, "!");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_HASH, "#");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_AT, "@");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_UNDERSCORE, "_");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_AND, "&&");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_OR, "||");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_SHL, "<<");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_SHR, ">>");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_PLUS_ASSIGN, "+=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_MINUS_ASSIGN, "-=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_MULTIPLY_ASSIGN, "*=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_DIVIDE_ASSIGN, "/=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_MODULO_ASSIGN, "%=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_AND_ASSIGN, "&=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_OR_ASSIGN, "|=");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_XOR_ASSIGN, "^=");
    
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, HandlesArrow) {
    std::string input = "->";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_ARROW, "->");
}

TEST(LexerTest, HandlesUnknown) {
    std::string input = "$";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_UNKNOWN, "$");
}
