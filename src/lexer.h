#ifndef GLOINC_LEXER_H
#define GLOINC_LEXER_H
#include <cstddef>
#include <string>

enum GloinTokenType {
    GLOIN_TOKEN_EOF,
    GLOIN_TOKEN_IMPORT,
    GLOIN_TOKEN_EXTERN,
    GLOIN_TOKEN_FN,
    GLOIN_TOKEN_DEF,
    GLOIN_TOKEN_MUT,
    GLOIN_TOKEN_CONST,
    GLOIN_TOKEN_RETURN,
    GLOIN_TOKEN_BOOL,
    GLOIN_TOKEN_I8, // 8-bit signed integer
    GLOIN_TOKEN_I16, // 16-bit signed integer
    GLOIN_TOKEN_I32, // 32-bit signed integer
    GLOIN_TOKEN_I64, // 64-bit signed integer
    GLOIN_TOKEN_I128, // 128-bit signed integer
    GLOIN_TOKEN_U8, // 8-bit unsigned integer
    GLOIN_TOKEN_U16, // 16-bit unsigned integer
    GLOIN_TOKEN_U32, // 32-bit unsigned integer
    GLOIN_TOKEN_U64, // 64-bit unsigned integer
    GLOIN_TOKEN_U128, // 128-bit unsigned integer
    GLOIN_TOKEN_F32, // 32-bit float
    GLOIN_TOKEN_F64, // 64-bit float
    GLOIN_TOKEN_F128, // 128-bit float
    GLOIN_TOKEN_VOID,
    GLOIN_TOKEN_TRUE,
    GLOIN_TOKEN_FALSE,
    GLOIN_TOKEN_NULL,
    GLOIN_TOKEN_STRUCT,
    GLOIN_TOKEN_ENUM,
    GLOIN_TOKEN_PUB,
    GLOIN_TOKEN_PRIV,
    GLOIN_TOKEN_STATIC,
    GLOIN_TOKEN_SELF,
    GLOIN_TOKEN_IF,
    GLOIN_TOKEN_UNLESS,
    GLOIN_TOKEN_ELSE,
    GLOIN_TOKEN_FOR,
    GLOIN_TOKEN_WHILE,
    GLOIN_TOKEN_SWITCH,
    GLOIN_TOKEN_MATCH,
    GLOIN_TOKEN_CASE,
    GLOIN_TOKEN_DEFAULT,
    GLOIN_TOKEN_BREAK,
    GLOIN_TOKEN_CONTINUE,
    GLOIN_TOKEN_DEFER,
    GLOIN_TOKEN_DEFERRED,
    GLOIN_TOKEN_SPAWNABLE,
    GLOIN_TOKEN_RUN,
    GLOIN_TOKEN_IDENTIFIER,
    GLOIN_TOKEN_STRING,
    GLOIN_TOKEN_CHAR,
    GLOIN_TOKEN_NUMBER,
    GLOIN_TOKEN_FLOAT,
    GLOIN_TOKEN_LPAREN,
    GLOIN_TOKEN_RPAREN,
    GLOIN_TOKEN_LBRACE,
    GLOIN_TOKEN_RBRACE,
    GLOIN_TOKEN_LBRACKET, // [
    GLOIN_TOKEN_RBRACKET, // ]
    GLOIN_TOKEN_SEMICOLON,
    GLOIN_TOKEN_COLON,
    GLOIN_TOKEN_DOUBLE_COLON,
    GLOIN_TOKEN_ASSIGN,
    GLOIN_TOKEN_ARROW,
    GLOIN_TOKEN_DOUBLE_ARROW,
    GLOIN_TOKEN_DOT,
    GLOIN_TOKEN_AT, // @
    GLOIN_TOKEN_HASH, // #
    GLOIN_TOKEN_QUOTE, // "
    GLOIN_TOKEN_COMMA, // ,
    GLOIN_TOKEN_PLUS, // +
    GLOIN_TOKEN_MINUS, // -
    GLOIN_TOKEN_MULTIPLY, // *
    GLOIN_TOKEN_DIVIDE, // /
    GLOIN_TOKEN_COMMENT, // NEW: // comments
    GLOIN_TOKEN_EQ, // ==
    GLOIN_TOKEN_NE, // !=
    GLOIN_TOKEN_NOT, // !
    GLOIN_TOKEN_LT, // <
    GLOIN_TOKEN_GT, // >
    GLOIN_TOKEN_LE, // <=
    GLOIN_TOKEN_GE, // >=
    GLOIN_TOKEN_AMPERSAND, // & (address-of)
    GLOIN_TOKEN_PIPE, // |
    GLOIN_TOKEN_CARET, // ^
    GLOIN_TOKEN_TILDE, // ~
    GLOIN_TOKEN_PERCENT, // %
    GLOIN_TOKEN_QUESTION, // ?
    GLOIN_TOKEN_AND, // &&
    GLOIN_TOKEN_OR, // ||
    GLOIN_TOKEN_SHL, // <<
    GLOIN_TOKEN_SHR, // >>
    GLOIN_TOKEN_PLUS_ASSIGN, // +=
    GLOIN_TOKEN_MINUS_ASSIGN, // -=
    GLOIN_TOKEN_MULTIPLY_ASSIGN, // *=
    GLOIN_TOKEN_DIVIDE_ASSIGN, // /=
    GLOIN_TOKEN_MODULO_ASSIGN, // %=
    GLOIN_TOKEN_AND_ASSIGN, // &=
    GLOIN_TOKEN_OR_ASSIGN, // |=
    GLOIN_TOKEN_XOR_ASSIGN, // ^=
    GLOIN_TOKEN_UNDERSCORE, // _
    GLOIN_TOKEN_NEWLINE,
    GLOIN_TOKEN_UNKNOWN
};

struct GloinToken {
    std::string_view literal;
    int64_t line_number;
    GloinTokenType type;
    int column;
};

class Lexer {
public:
    explicit Lexer(const std::string &src) : src(src) {
        this->position = 0;
        this->current_char = src.empty() ? '\0' : src[position];
        this->line = 1;
        this->column = 1;
    }

    [[nodiscard]] GloinToken next_token();

private:
    int position;
    int current_char;
    int line;
    int column;
    const std::string &src;

    void skip_whitespace();

    void skip_comment();

    char peek_token();

    void advance();

    GloinToken read_string(int start_line, int start_column);

    std::string_view read_char();

    std::string_view read_number();

    std::string_view read_float();

    std::string_view read_identifier();

    [[nodiscard]] std::string_view capture_view(int start_pos) const;
};

void print_debug_token(const GloinToken &token);

GloinTokenType get_keyword_type(std::string_view identifier);
#endif // GLOINC_LEXER_H
