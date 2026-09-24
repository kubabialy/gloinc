#include "../src/lexer.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// Helper to check token type and literal
void ExpectToken(const GloinToken &token, GloinTokenType type, const std::string &literal) {
    EXPECT_EQ(token.type, type) << "Expected token type " << type << " but got " << token.type
                                << " for literal '" << literal << "'";
    EXPECT_EQ(token.literal, literal)
        << "Expected literal '" << literal << "' but got '" << token.literal << "'";
}

TEST(LexerTest, HandlesKeywords) {
    std::string input =
        "import extern fn def mut const return bool i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 "
        "f64 f128 string void true false null struct enum pub priv static self if unless else for "
        "while switch match case default break continue defer deferred spawnable run";
    Lexer lexer(input);

    std::vector<GloinTokenType> expected_types = {
        GLOIN_TOKEN_IMPORT,   GLOIN_TOKEN_EXTERN,    GLOIN_TOKEN_FN,       GLOIN_TOKEN_DEF,
        GLOIN_TOKEN_MUT,      GLOIN_TOKEN_CONST,     GLOIN_TOKEN_RETURN,   GLOIN_TOKEN_BOOL,
        GLOIN_TOKEN_I8,       GLOIN_TOKEN_I16,       GLOIN_TOKEN_I32,      GLOIN_TOKEN_I64,
        GLOIN_TOKEN_I128,     GLOIN_TOKEN_U8,        GLOIN_TOKEN_U16,      GLOIN_TOKEN_U32,
        GLOIN_TOKEN_U64,      GLOIN_TOKEN_U128,      GLOIN_TOKEN_F32,      GLOIN_TOKEN_F64,
        GLOIN_TOKEN_F128,     GLOIN_TOKEN_STRING,    GLOIN_TOKEN_VOID,     GLOIN_TOKEN_TRUE,
        GLOIN_TOKEN_FALSE,    GLOIN_TOKEN_NULL,      GLOIN_TOKEN_STRUCT,   GLOIN_TOKEN_ENUM,
        GLOIN_TOKEN_PUB,      GLOIN_TOKEN_PRIV,      GLOIN_TOKEN_STATIC,   GLOIN_TOKEN_SELF,
        GLOIN_TOKEN_IF,       GLOIN_TOKEN_UNLESS,    GLOIN_TOKEN_ELSE,     GLOIN_TOKEN_FOR,
        GLOIN_TOKEN_WHILE,    GLOIN_TOKEN_SWITCH,    GLOIN_TOKEN_MATCH,    GLOIN_TOKEN_CASE,
        GLOIN_TOKEN_DEFAULT,  GLOIN_TOKEN_BREAK,     GLOIN_TOKEN_CONTINUE, GLOIN_TOKEN_DEFER,
        GLOIN_TOKEN_DEFERRED, GLOIN_TOKEN_SPAWNABLE, GLOIN_TOKEN_RUN};

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

    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING_LITERAL, "hello world");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING_LITERAL, "");
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
    std::string input = "+ - * / = == => != < <= > >= & | ^ ~ % ? . , ; : :: { } [ ] ( ) ! # @ _ "
                        "&& || << >> += -= *= /= %= &= |= ^=";
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

TEST(LexerTest, HandlesInKeyword) {
    std::string input = "in";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IN, "in");
}

TEST(LexerTest, HandlesRangeOperator) {
    std::string input = "..";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RANGE, "..");
}

TEST(LexerTest, HandlesRangeInContext) {
    std::string input = "0..10";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "0");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RANGE, "..");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "10");
}

TEST(LexerTest, HandlesEndiannessKeywords) {
    std::string input = "be_u32 le_u16 be_i64 le_i8";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_BE_U32, "be_u32");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LE_U16, "le_u16");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_BE_I64, "be_i64");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_LE_I8, "le_i8");
}

TEST(LexerTest, HandlesBitmappingKeywords) {
    std::string input = "packed bit at";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_PACKED, "packed");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_BIT, "bit");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_KEYWORD_AT, "at");
}

TEST(LexerTest, HandlesCustomWidthIntegers) {
    std::string input = "u4 u5 u6 u2 u20";
    Lexer lexer(input);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, "u4");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, "u5");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, "u6");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, "u2");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, "u20");
}

// Helper to check token details including position
void ExpectTokenPos(const GloinToken &token, GloinTokenType type, const std::string &literal,
                    int line, int column) {
    EXPECT_EQ(token.type, type) << "Expected type " << type << " but got " << token.type;
    EXPECT_EQ(token.literal, literal)
        << "Expected literal '" << literal << "' but got '" << token.literal << "'";
    EXPECT_EQ(token.line_number, line) << "Expected line " << line << " but got "
                                       << token.line_number << " for token '" << literal << "'";
    EXPECT_EQ(token.column, column) << "Expected column " << column << " but got " << token.column
                                    << " for token '" << literal << "'";
}

TEST(LexerTest, TrackLineNumbers) {
    std::string input = "a\nb\nc";
    Lexer lexer(input);

    // 'a' at 1:1
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "a", 1, 1);

    GloinToken nl1 = lexer.next_token();
    EXPECT_EQ(nl1.type, GLOIN_TOKEN_NEWLINE);

    GloinToken b = lexer.next_token();
    EXPECT_EQ(b.literal, "b");

    GloinToken nl2 = lexer.next_token();
    EXPECT_EQ(nl2.type, GLOIN_TOKEN_NEWLINE);

    GloinToken c = lexer.next_token();
    EXPECT_EQ(c.literal, "c");

    // Correct behavior should be:
    // a: line 1
    // nl1: line 1
    // b: line 2
    // nl2: line 2
    // c: line 3

    EXPECT_EQ(c.line_number, 3);
}

TEST(LexerTest, TrackColumns) {
    std::string input = "abc ghi";
    Lexer lexer(input);

    // abc at 1:1. Len 3.
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "abc", 1, 1);

    // space at 1:4. Skipped.

    // ghi at 1:5.
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "ghi", 1, 5);
}

TEST(LexerTest, TrackFloatColumns) {
    std::string input = "1.23";
    Lexer lexer(input);

    // 1.23 at 1:1
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_FLOAT, "1.23", 1, 1);
}
TEST(LexerTest, SeparatesTypeKeywordsFromQuotedText) {
    Lexer lexer("int usize char string \"string\" 'c' Int String");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_INT, "int");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_USIZE, "usize");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR_TYPE, "char");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING, "string");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING_LITERAL, "string");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "c");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "Int");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "String");
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, RecognizesReservedWordsWithoutRecognizingIdentifierPrefixes) {
    Lexer lexer("fn spawn await switch match case default in break continue spawnable spawn_value "
                "integer inside");
    for (auto type : {GLOIN_TOKEN_FN, GLOIN_TOKEN_SPAWN, GLOIN_TOKEN_AWAIT, GLOIN_TOKEN_SWITCH,
                      GLOIN_TOKEN_MATCH, GLOIN_TOKEN_CASE, GLOIN_TOKEN_DEFAULT, GLOIN_TOKEN_IN,
                      GLOIN_TOKEN_BREAK, GLOIN_TOKEN_CONTINUE, GLOIN_TOKEN_SPAWNABLE})
        EXPECT_EQ(lexer.next_token().type, type);
    for (auto name : {"spawn_value", "integer", "inside"})
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, name);
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, MultiCharacterOperatorsPreserveAdjacentOperandsAndSpans) {
    Lexer lexer("a=>b 0..10 ==x !=y ->z <=q >=r &&s ||t <<u >>v +=w", "operators.gloin");
    const std::vector<std::pair<GloinTokenType, std::string>> expected = {
        {GLOIN_TOKEN_IDENTIFIER, "a"},   {GLOIN_TOKEN_DOUBLE_ARROW, "=>"},
        {GLOIN_TOKEN_IDENTIFIER, "b"},   {GLOIN_TOKEN_NUMBER, "0"},
        {GLOIN_TOKEN_RANGE, ".."},       {GLOIN_TOKEN_NUMBER, "10"},
        {GLOIN_TOKEN_EQ, "=="},          {GLOIN_TOKEN_IDENTIFIER, "x"},
        {GLOIN_TOKEN_NE, "!="},          {GLOIN_TOKEN_IDENTIFIER, "y"},
        {GLOIN_TOKEN_ARROW, "->"},       {GLOIN_TOKEN_IDENTIFIER, "z"},
        {GLOIN_TOKEN_LE, "<="},          {GLOIN_TOKEN_IDENTIFIER, "q"},
        {GLOIN_TOKEN_GE, ">="},          {GLOIN_TOKEN_IDENTIFIER, "r"},
        {GLOIN_TOKEN_AND, "&&"},         {GLOIN_TOKEN_IDENTIFIER, "s"},
        {GLOIN_TOKEN_OR, "||"},          {GLOIN_TOKEN_IDENTIFIER, "t"},
        {GLOIN_TOKEN_SHL, "<<"},         {GLOIN_TOKEN_IDENTIFIER, "u"},
        {GLOIN_TOKEN_SHR, ">>"},         {GLOIN_TOKEN_IDENTIFIER, "v"},
        {GLOIN_TOKEN_PLUS_ASSIGN, "+="}, {GLOIN_TOKEN_IDENTIFIER, "w"},
    };
    size_t previous_end = 0;
    for (const auto &[type, spelling] : expected) {
        auto token = lexer.next_token();
        ExpectToken(token, type, spelling);
        EXPECT_GE(token.span.begin, previous_end);
        EXPECT_EQ(
            token.span.source->text.substr(token.span.begin, token.span.end - token.span.begin),
            spelling);
        EXPECT_EQ(token.column, token.span.begin + 1);
        previous_end = token.span.end;
    }
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_EOF);
}

TEST(LexerTest, EndOfFileOperatorsNeverAdvancePastSource) {
    for (std::string spelling : {"=", "=>", ".", "..", "->", "!=", "<<", "+="}) {
        Lexer lexer(spelling);
        auto token = lexer.next_token();
        EXPECT_EQ(token.literal, spelling);
        EXPECT_EQ(token.span.end, spelling.size());
        for (int i = 0; i < 3; ++i) {
            auto end = lexer.next_token();
            EXPECT_EQ(end.type, GLOIN_TOKEN_EOF);
            EXPECT_TRUE(end.literal.empty());
            EXPECT_EQ(end.span.begin, spelling.size());
            EXPECT_EQ(end.span.end, spelling.size());
        }
    }
}

TEST(LexerTest, TracksCrLfCommentsAndEofPositions) {
    Lexer lexer("a// café\r\n\tb\n// trailing", "lines.gloin");
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "a", 1, 1);
    auto newline = lexer.next_token();
    ExpectTokenPos(newline, GLOIN_TOKEN_NEWLINE, "\r\n", 1, 10);
    EXPECT_EQ(newline.span.begin, 9u);
    EXPECT_EQ(newline.span.end, 11u);
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "b", 2, 2);
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_NEWLINE, "\n", 2, 3);
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_EOF, "", 3, 12);
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, ValidatesDecimalBaseAndExponentSpellingsWithoutConvertingValues) {
    Lexer lexer("0X2A 0B101 1.25 1e3 2.5E-2 3E+4 999999999999999999999999999999 1..2 1.5..3");
    for (auto spelling : {"0X2A", "0B101"})
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, spelling);
    for (auto spelling : {"1.25", "1e3", "2.5E-2", "3E+4"})
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_FLOAT, spelling);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "999999999999999999999999999999");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "1");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RANGE, "..");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "2");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_FLOAT, "1.5");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_RANGE, "..");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_NUMBER, "3");
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, RejectsMalformedNumbersAsWholeTokens) {
    for (std::string spelling : {"0x", "0XG", "0b", "0b102", "123abc", "1_000", "42u8", "1.", ".5",
                                 "1.2.3", "1e", "1e+", "1e-foo", "0x1.2"}) {
        SCOPED_TRACE(spelling);
        Lexer lexer(spelling + ";next");
        auto bad = lexer.next_token();
        ExpectToken(bad, GLOIN_TOKEN_UNKNOWN, spelling);
        EXPECT_EQ(bad.span.begin, 0u);
        EXPECT_EQ(bad.span.end, spelling.size());
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_SEMICOLON, ";");
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "next");
        ASSERT_EQ(lexer.diagnostics()->all().size(), 1u);
        EXPECT_EQ(lexer.diagnostics()->all()[0].stage, DiagnosticStage::Lexing);
    }
}

TEST(LexerTest, PreservesQuotedEscapeSpellingsAndUnicodeScalars) {
    Lexer lexer(R"("é😀" "\n\r\t\0\\\"\'" 'é' '😀' '\0')");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING_LITERAL, "é😀");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_STRING_LITERAL, R"(\n\r\t\0\\\"\')");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "é");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, "😀");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_CHAR, R"(\0)");
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, RejectsMalformedQuotedLiterals) {
    for (std::string spelling : {"''", "'ab'", "'éx'", R"('\q')", R"("\q")", R"("\x41")", "'a",
                                 "\"trailing\\", "\"raw\t tab\"", "\"line\nbreak\""}) {
        SCOPED_TRACE(spelling);
        Lexer lexer(spelling);
        EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_UNKNOWN);
        EXPECT_TRUE(lexer.diagnostics()->has_errors());
    }
    Lexer recover("\"unterminated\r\nnext");
    EXPECT_EQ(recover.next_token().type, GLOIN_TOKEN_UNKNOWN);
    ExpectToken(recover.next_token(), GLOIN_TOKEN_NEWLINE, "\r\n");
    ExpectTokenPos(recover.next_token(), GLOIN_TOKEN_IDENTIFIER, "next", 2, 1);
}

TEST(LexerTest, RejectsInvalidUtf8EverywhereIncludingComments) {
    for (std::string bytes :
         {"\x80", "\xc0\xaf", "\xc2", "\xe0\x80\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80",
          "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff", "\xe2\x28\xa1"}) {
        for (const auto &text : {bytes, "//" + bytes, "\"" + bytes + "\"", "'" + bytes + "'"}) {
            Lexer lexer(text, "encoding.gloin");
            auto tokens = lexer.tokenize();
            EXPECT_EQ(tokens.back().type, GLOIN_TOKEN_EOF);
            ASSERT_TRUE(lexer.diagnostics()->has_errors());
            EXPECT_EQ(lexer.diagnostics()->all().front().message, "Invalid UTF-8 in source");
            EXPECT_EQ(lexer.diagnostics()->all().front().span.source->name, "encoding.gloin");
        }
    }
}

TEST(LexerTest, RejectsNonAsciiIdentifiersButAcceptsUtf8Comments) {
    for (std::string spelling : {"café", "变量", "a😀", "_é", "é_name"}) {
        Lexer lexer(spelling + ";ok");
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_UNKNOWN, spelling);
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_SEMICOLON, ";");
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "ok");
        EXPECT_TRUE(lexer.diagnostics()->has_errors());
    }
    Lexer lexer("// café 变量 😀\nvalid");
    EXPECT_EQ(lexer.next_token().type, GLOIN_TOKEN_NEWLINE);
    ExpectTokenPos(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "valid", 2, 1);
    EXPECT_FALSE(lexer.diagnostics()->has_errors());
}

TEST(LexerTest, RejectsBomNulAndUnsupportedTrivia) {
    for (const auto &text :
         {std::string("\xef\xbb\xbf") + "def x: i32 = 0;",
          std::string("// comment") + '\0' + "tail", std::string("\"string") + '\0' + "data\"",
          std::string("/* comment */next"), std::string("/* unclosed"), std::string("\r"),
          std::string("\v"), std::string("\f")}) {
        Lexer lexer(text);
        auto tokens = lexer.tokenize();
        EXPECT_EQ(tokens.back().type, GLOIN_TOKEN_EOF);
        EXPECT_TRUE(lexer.diagnostics()->has_errors());
    }
}

TEST(LexerTest, ReservesCustomWidthAndEndianTypeNames) {
    Lexer lexer("i4 u20 be_u4 le_i20 u16_be i32_le u0 u123abc be_name");
    for (auto spelling : {"i4", "u20", "be_u4", "le_i20", "u16_be", "i32_le", "u0"})
        ExpectToken(lexer.next_token(), GLOIN_TOKEN_CUSTOM_WIDTH_INT, spelling);
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "u123abc");
    ExpectToken(lexer.next_token(), GLOIN_TOKEN_IDENTIFIER, "be_name");
}

TEST(LexerTest, EveryTokenKindHasADiagnosticName) {
    for (int type = GLOIN_TOKEN_FN; type < GLOIN_TOKEN_UNKNOWN; ++type)
        EXPECT_NE(token_type_to_string(static_cast<GloinTokenType>(type)), "UNKNOWN") << type;
}

TEST(LexerTest, EveryByteMakesProgressWithBoundedSpans) {
    for (int byte = 0; byte <= 255; ++byte) {
        Lexer lexer(std::string(1, static_cast<char>(byte)) + ";tail");
        size_t previous_end = 0;
        bool eof = false;
        for (int count = 0; count < 8; ++count) {
            auto token = lexer.next_token();
            EXPECT_GE(token.span.begin, previous_end);
            EXPECT_LE(token.span.begin, token.span.end);
            EXPECT_LE(token.span.end, token.span.source->text.size());
            if (token.type == GLOIN_TOKEN_EOF) {
                eof = true;
                break;
            }
            EXPECT_GT(token.span.end, previous_end);
            previous_end = token.span.end;
        }
        EXPECT_TRUE(eof) << byte;
    }
}
