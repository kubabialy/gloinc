#include "lexer.h"

#include <iostream>
#include <ostream>
#include <unordered_map>
#include <cctype>

GloinToken Lexer::next_token() {
    GloinToken token;
    token.line_number = line;
    token.column = column;

    while (current_char != '\0') {
        if (isspace(current_char) && current_char != '\n') {
            skip_whitespace();
            continue;
        }

        if (current_char == '\n') {
            column = 1;
            line++;
            const std::string_view literal(&src[position], 1);
            advance();
            return {literal, line, GLOIN_TOKEN_NEWLINE, column};
        }

        if (current_char == '/' && peek_token() == '/') {
            skip_comment();
            continue;
        }

        if (current_char == '"') {
            return read_string(line, column);
        }

        if (current_char == '\'') {
            return {read_char(), line, GLOIN_TOKEN_CHAR, column};
        }

        if (isdigit(current_char)) {
            int temp_pos = position;
            int has_decimal = 0;

            while (temp_pos < src.size() &&
                   (isdigit(src[temp_pos]) || src[temp_pos] == '.')) {
                if (src[temp_pos] == '.') {
                    has_decimal = 1;
                    break;
                }
                temp_pos++;
            }

            if (has_decimal) {
                return {read_float(), line, GLOIN_TOKEN_FLOAT, column};
            }

            return {read_number(), line, GLOIN_TOKEN_NUMBER, column};
        }

        if (isalpha(current_char) || current_char == '_') {
            const auto identifier = read_identifier();
            return {identifier, line, get_keyword_type(identifier), column};
        }

        switch (current_char) {
            case '(':
                advance();
                return {{"(", 1}, line, GLOIN_TOKEN_LPAREN, column};
            case ')':
                advance();
                return {{")", 1}, line, GLOIN_TOKEN_RPAREN, column};
            case '{':
                advance();
                return {{"{", 1}, line, GLOIN_TOKEN_LBRACE, column};
            case '}':
                advance();
                return {{"}", 1}, line, GLOIN_TOKEN_RBRACE, column};
            case '[':
                advance();
                return {{"[", 1}, line, GLOIN_TOKEN_LBRACKET, column};
            case ']':
                advance();
                return {{"]", 1}, line, GLOIN_TOKEN_RBRACKET, column};
            case ';':
                advance();
                return {";", line, GLOIN_TOKEN_SEMICOLON, column};
            case ':':
                if (peek_token() == ':') {
                    advance();
                    advance();
                    return {"::", line, GLOIN_TOKEN_DOUBLE_COLON, column};
                }
                advance();
                return {":", line, GLOIN_TOKEN_COLON, column};
            case '=':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"==", line, GLOIN_TOKEN_EQ, column};
                }

                if (peek_token() == '>') {
                    advance();
                    advance();
                    return {"=>", line, GLOIN_TOKEN_DOUBLE_ARROW, column};
                }

                advance();
                return {"=", line, GLOIN_TOKEN_ASSIGN, column};
            case '+':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"+=", line, GLOIN_TOKEN_PLUS_ASSIGN, column};
                }
                advance();
                return {"+", line, GLOIN_TOKEN_PLUS, column};
            case '-':
                if (peek_token() == '>') {
                    advance();
                    advance();
                    return {"->", line, GLOIN_TOKEN_ARROW, column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"-=", line, GLOIN_TOKEN_MINUS_ASSIGN, column};
                }
                advance();
                return {"-", line, GLOIN_TOKEN_MINUS, column};
            case '*':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"*=", line, GLOIN_TOKEN_MULTIPLY_ASSIGN, column};
                }
                advance();
                return {"*", line, GLOIN_TOKEN_MULTIPLY, column};
            case '/':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"/=", line, GLOIN_TOKEN_DIVIDE_ASSIGN, column};
                }
                advance();
                return {"/", line, GLOIN_TOKEN_DIVIDE, column};
            case '!':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"!=", line, GLOIN_TOKEN_NE, column};
                }
                advance();
                return {"!", line, GLOIN_TOKEN_NOT, column};
            case '<':
                if (peek_token() == '<') {
                    advance();
                    advance();
                    return {"<<", line, GLOIN_TOKEN_SHL, column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"<=", line, GLOIN_TOKEN_LE, column};
                }
                advance();
                return {"<", line, GLOIN_TOKEN_LT, column};
            case '>':
                if (peek_token() == '>') {
                    advance();
                    advance();
                    return {">>", line, GLOIN_TOKEN_SHR, column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {">=", line, GLOIN_TOKEN_GE, column};
                }

                advance();
                return {">", line, GLOIN_TOKEN_GT, column};
            case '&':
                if (peek_token() == '&') {
                    advance();
                    advance();
                    return {"&&", line, GLOIN_TOKEN_AND, column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"&=", line, GLOIN_TOKEN_AND_ASSIGN, column};
                }
                advance();
                return {"&", line, GLOIN_TOKEN_AMPERSAND, column};
            case '|':
                if (peek_token() == '|') {
                    advance();
                    advance();
                    return {"||", line, GLOIN_TOKEN_OR, column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"|=", line, GLOIN_TOKEN_OR_ASSIGN, column};
                }
                advance();
                return {"|", line, GLOIN_TOKEN_PIPE, column};
            case '^':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"^=", line, GLOIN_TOKEN_XOR_ASSIGN, column};
                }
                advance();
                return {"^", line, GLOIN_TOKEN_CARET, column};
            case '~':
                advance();
                return {"~", line, GLOIN_TOKEN_TILDE, column};
            case '%':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"%=", line, GLOIN_TOKEN_MODULO_ASSIGN, column};
                }
                advance();
                return {"%", line, GLOIN_TOKEN_PERCENT, column};
            case '?':
                advance();
                return {"?", line, GLOIN_TOKEN_QUESTION, column};
            case '.':
                advance();
                return {".", line, GLOIN_TOKEN_DOT, column};
            case ',':
                advance();
                return {",", line, GLOIN_TOKEN_COMMA, column};
            case '#':
                advance();
                return {"#", line, GLOIN_TOKEN_HASH, column};
            case '@':
                advance();
                return {"@", line, GLOIN_TOKEN_AT, column};
            default:
                advance();
                const std::string_view literal(&src[position - 1], 1);
                return {literal, line, GLOIN_TOKEN_UNKNOWN, column};
        }
    }

    return {{"", 1}, line, GLOIN_TOKEN_EOF, column};
}

std::string token_type_to_string(const GloinTokenType type) {
    switch (type) {
        case GLOIN_TOKEN_EOF:
            return "EOF";
        case GLOIN_TOKEN_IMPORT:
            return "IMPORT";
        case GLOIN_TOKEN_EXTERN:
            return "EXTERN";
        case GLOIN_TOKEN_FN:
            return "FN";
        case GLOIN_TOKEN_DEF:
            return "DEF";
        case GLOIN_TOKEN_MUT:
            return "MUT";
        case GLOIN_TOKEN_CONST:
            return "CONST";
        case GLOIN_TOKEN_RETURN:
            return "RETURN";
        case GLOIN_TOKEN_BOOL:
            return "BOOL";
        case GLOIN_TOKEN_I8:
            return "I8";
        case GLOIN_TOKEN_I16:
            return "I16";
        case GLOIN_TOKEN_I32:
            return "I32";
        case GLOIN_TOKEN_I64:
            return "I64";
        case GLOIN_TOKEN_I128:
            return "I128";
        case GLOIN_TOKEN_U8:
            return "U8";
        case GLOIN_TOKEN_U16:
            return "U16";
        case GLOIN_TOKEN_U32:
            return "U32";
        case GLOIN_TOKEN_U64:
            return "U64";
        case GLOIN_TOKEN_U128:
            return "U128";
        case GLOIN_TOKEN_F32:
            return "F32";
        case GLOIN_TOKEN_F64:
            return "F64";
        case GLOIN_TOKEN_F128:
            return "F128";
        case GLOIN_TOKEN_STRING:
            return "STRING";
        case GLOIN_TOKEN_CHAR:
            return "CHAR";
        case GLOIN_TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case GLOIN_TOKEN_NUMBER:
            return "NUMBER";
        case GLOIN_TOKEN_FLOAT:
            return "FLOAT";
        case GLOIN_TOKEN_LPAREN:
            return "LPAREN";
        case GLOIN_TOKEN_RPAREN:
            return "RPAREN";
        case GLOIN_TOKEN_LBRACE:
            return "LBRACE";
        case GLOIN_TOKEN_RBRACE:
            return "RBRACE";
        case GLOIN_TOKEN_LBRACKET:
            return "LBRACKET";
        case GLOIN_TOKEN_RBRACKET:
            return "RBRACKET";
        case GLOIN_TOKEN_COMMA:
            return "COMMA";
        case GLOIN_TOKEN_COLON:
            return "COLON";
        case GLOIN_TOKEN_SEMICOLON:
            return "SEMICOLON";
        case GLOIN_TOKEN_STRUCT:
            return "STRUCT";
        case GLOIN_TOKEN_ENUM:
            return "ENUM";
        case GLOIN_TOKEN_PUB:
            return "PUB";
        case GLOIN_TOKEN_PRIV:
            return "PRIV";
        case GLOIN_TOKEN_TRUE:
            return "TRUE";
        case GLOIN_TOKEN_FALSE:
            return "FALSE";
        case GLOIN_TOKEN_NULL:
            return "NULL";
        case GLOIN_TOKEN_ASSIGN:
            return "ASSIGN";
        case GLOIN_TOKEN_PLUS:
            return "PLUS";
        case GLOIN_TOKEN_MINUS:
            return "MINUS";
        case GLOIN_TOKEN_MULTIPLY:
            return "MULTIPLY";
        case GLOIN_TOKEN_DIVIDE:
            return "DIVIDE";
        case GLOIN_TOKEN_VOID:
            return "VOID";
        case GLOIN_TOKEN_ARROW:
            return "ARROW";
        case GLOIN_TOKEN_DOUBLE_ARROW:
            return "DOUBLE_ARROW";
        case GLOIN_TOKEN_DOT:
            return "DOT";
        case GLOIN_TOKEN_DOUBLE_COLON:
            return "DOUBLE_COLON";
        case GLOIN_TOKEN_EQ:
            return "EQ";
        case GLOIN_TOKEN_NE:
            return "NE";
        case GLOIN_TOKEN_NOT:
            return "NOT";
        case GLOIN_TOKEN_LT:
            return "LT";
        case GLOIN_TOKEN_GT:
            return "GT";
        case GLOIN_TOKEN_LE:
            return "LE";
        case GLOIN_TOKEN_GE:
            return "GE";
        case GLOIN_TOKEN_STATIC:
            return "STATIC";
        case GLOIN_TOKEN_SELF:
            return "SELF";
        case GLOIN_TOKEN_IF:
            return "IF";
        case GLOIN_TOKEN_UNLESS:
            return "UNLESS";
        case GLOIN_TOKEN_ELSE:
            return "ELSE";
        case GLOIN_TOKEN_FOR:
            return "FOR";
        case GLOIN_TOKEN_WHILE:
            return "WHILE";
        case GLOIN_TOKEN_MATCH:
            return "MATCH";
        case GLOIN_TOKEN_SWITCH:
            return "SWITCH";
        case GLOIN_TOKEN_CASE:
            return "CASE";
        case GLOIN_TOKEN_DEFAULT:
            return "DEFAULT";
        case GLOIN_TOKEN_SPAWNABLE:
            return "SPAWNABLE";
        case GLOIN_TOKEN_RUN:
            return "RUN";
        case GLOIN_TOKEN_COMMENT:
            return "COMMENT";
        case GLOIN_TOKEN_NEWLINE:
            return "NEWLINE";
        case GLOIN_TOKEN_DEFER:
            return "DEFER";
        case GLOIN_TOKEN_DEFERRED:
            return "DEFERRED";
        case GLOIN_TOKEN_AMPERSAND:
            return "AMPERSAND";
        case GLOIN_TOKEN_PIPE:
            return "PIPE";
        case GLOIN_TOKEN_CARET:
            return "CARET";
        case GLOIN_TOKEN_TILDE:
            return "TILDE";
        case GLOIN_TOKEN_PERCENT:
            return "PERCENT";
        case GLOIN_TOKEN_QUESTION:
            return "QUESTION";
        case GLOIN_TOKEN_AND:
            return "AND";
        case GLOIN_TOKEN_OR:
            return "OR";
        case GLOIN_TOKEN_SHL:
            return "SHL";
        case GLOIN_TOKEN_SHR:
            return "SHR";
        case GLOIN_TOKEN_PLUS_ASSIGN:
            return "PLUS_ASSIGN";
        case GLOIN_TOKEN_MINUS_ASSIGN:
            return "MINUS_ASSIGN";
        case GLOIN_TOKEN_MULTIPLY_ASSIGN:
            return "MULTIPLY_ASSIGN";
        case GLOIN_TOKEN_DIVIDE_ASSIGN:
            return "DIVIDE_ASSIGN";
        case GLOIN_TOKEN_MODULO_ASSIGN:
            return "MODULO_ASSIGN";
        case GLOIN_TOKEN_AND_ASSIGN:
            return "AND_ASSIGN";
        case GLOIN_TOKEN_OR_ASSIGN:
            return "OR_ASSIGN";
        case GLOIN_TOKEN_XOR_ASSIGN:
            return "XOR_ASSIGN";
        case GLOIN_TOKEN_UNDERSCORE:
            return "UNDERSCORE";
        case GLOIN_TOKEN_HASH:
            return "HASH";
        case GLOIN_TOKEN_AT:
            return "AT";
        case GLOIN_TOKEN_QUOTE:
            return "QUOTE";
        case GLOIN_TOKEN_BREAK:
            return "BREAK";
        case GLOIN_TOKEN_CONTINUE:
            return "CONTINUE";
        case GLOIN_TOKEN_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

char Lexer::peek_token() {
    int peek_position = position + 1;
    if (peek_position >= src.size())
        return '\0';

    return src[peek_position];
}

void Lexer::skip_whitespace() {
    while (isspace(current_char) && current_char != '\n') {
        advance();
    }
}

void Lexer::advance() {
    position++;
    if (current_char == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }

    current_char = position >= src.size() ? '\0' : src[position];
}

void Lexer::skip_comment() {
    static const std::unordered_map<std::string_view, GloinTokenType> keywords = {
        {"import", GLOIN_TOKEN_IMPORT},
        {"extern", GLOIN_TOKEN_EXTERN},
        {"fn", GLOIN_TOKEN_FN},
        {"def", GLOIN_TOKEN_DEF},
        {"mut", GLOIN_TOKEN_MUT},
        {"const", GLOIN_TOKEN_CONST},
        {"return", GLOIN_TOKEN_RETURN},
        {"bool", GLOIN_TOKEN_BOOL},
        {"i8", GLOIN_TOKEN_I8},
        {"i16", GLOIN_TOKEN_I16},
        {"i32", GLOIN_TOKEN_I32},
        {"i64", GLOIN_TOKEN_I64},
        {"i128", GLOIN_TOKEN_I128},
        {"u8", GLOIN_TOKEN_U8},
        {"u16", GLOIN_TOKEN_U16},
        {"u32", GLOIN_TOKEN_U32},
        {"u64", GLOIN_TOKEN_U64},
        {"u128", GLOIN_TOKEN_U128},
        {"f32", GLOIN_TOKEN_F32},
        {"f64", GLOIN_TOKEN_F64},
        {"f128", GLOIN_TOKEN_F128},
        {"string", GLOIN_TOKEN_STRING},
        {"void", GLOIN_TOKEN_VOID},
        {"true", GLOIN_TOKEN_TRUE},
        {"false", GLOIN_TOKEN_FALSE},
        {"null", GLOIN_TOKEN_NULL},
        {"struct", GLOIN_TOKEN_STRUCT},
        {"enum", GLOIN_TOKEN_ENUM},
        {"pub", GLOIN_TOKEN_PUB},
        {"priv", GLOIN_TOKEN_PRIV},
        {"static", GLOIN_TOKEN_STATIC},
        {"self", GLOIN_TOKEN_SELF},
        {"if", GLOIN_TOKEN_IF},
        {"unless", GLOIN_TOKEN_UNLESS},
        {"else", GLOIN_TOKEN_ELSE},
        {"for", GLOIN_TOKEN_FOR},
        {"while", GLOIN_TOKEN_WHILE},
        {"switch", GLOIN_TOKEN_SWITCH},
        {"match", GLOIN_TOKEN_MATCH},
        {"case", GLOIN_TOKEN_CASE},
        {"default", GLOIN_TOKEN_DEFAULT},
        {"break", GLOIN_TOKEN_BREAK},
        {"continue", GLOIN_TOKEN_CONTINUE},
        {"defer", GLOIN_TOKEN_DEFER},
        {"deferred", GLOIN_TOKEN_DEFERRED},
        {"spawnable", GLOIN_TOKEN_SPAWNABLE},
        {"run", GLOIN_TOKEN_RUN}
    };

    if (current_char == '/' && peek_token() == '/') {
        advance();
        advance();
        while (current_char != '\0' && current_char != '\n') {
            advance();
        }
    }
}

std::string_view Lexer::capture_view(int start_pos) const {
    return std::string_view(src).substr(start_pos, position - start_pos);
}

std::string_view Lexer::read_number() {
    const int start_pos = position;

    if (current_char == '0') {
        const char next = peek_token();
        if (next == 'x' || next == 'X') {
            advance(); // 0
            advance(); // x
            while (current_char != '\0' && isxdigit(current_char)) {
                advance();
            }
            return capture_view(start_pos);
        }
        if (next == 'b' || next == 'B') {
            advance(); // 0
            advance(); // b
            while (current_char != '\0' && (current_char == '0' || current_char == '1')) {
                advance();
            }
            return capture_view(start_pos);
        }
    }

    while (current_char != '\0' && isdigit(current_char)) {
        advance();
    }

    return capture_view(start_pos);
}

GloinToken Lexer::read_string(int start_line, int start_column) {
    advance(); // Skip opening quote
    const int start_pos = position;
    while (current_char != '\0' && current_char != '"') {
        if (current_char == '\\' && peek_token() != '\0') {
            advance(); // Skip backslash
        }
        advance();
    }

    const auto lit = capture_view(start_pos);
    if (current_char == '"') {
        advance();
        return {lit, start_line, GLOIN_TOKEN_STRING, start_column};
    }

    return {lit, start_line, GLOIN_TOKEN_UNKNOWN, start_column};
}

std::string_view Lexer::read_char() {
    advance(); // Skip opening quote
    const int start_pos = position;
    
    if (current_char != '\0' && current_char != '\'') {
        if (current_char == '\\' && peek_token() != '\0') {
            advance(); // Skip backslash
        }
        advance();
    }

    const auto lit = capture_view(start_pos);
    if (current_char == '\'') {
        advance();
    }

    return lit;
}

std::string_view Lexer::read_float() {
    const int start_pos = position;
    while (current_char != '\0' && isdigit(current_char)) {
        advance();
    }

    if (current_char == '.') {
        advance();
        while (current_char != '\0' && isdigit(current_char)) {
            advance();
        }
    }

    return capture_view(start_pos);
}

std::string_view Lexer::read_identifier() {
    const int start_pos = position;
    while (current_char != '\0' && (isalnum(current_char) || current_char == '_')) {
        advance();
    }

    return capture_view(start_pos);
}

GloinTokenType get_keyword_type(std::string_view identifier) {
    static const std::unordered_map<std::string_view, GloinTokenType> keywords = {
        {"import", GLOIN_TOKEN_IMPORT},
        {"extern", GLOIN_TOKEN_EXTERN},
        {"fn", GLOIN_TOKEN_FN},
        {"def", GLOIN_TOKEN_DEF},
        {"mut", GLOIN_TOKEN_MUT},
        {"const", GLOIN_TOKEN_CONST},
        {"return", GLOIN_TOKEN_RETURN},
        {"bool", GLOIN_TOKEN_BOOL},
        {"i8", GLOIN_TOKEN_I8},
        {"i16", GLOIN_TOKEN_I16},
        {"i32", GLOIN_TOKEN_I32},
        {"i64", GLOIN_TOKEN_I64},
        {"i128", GLOIN_TOKEN_I128},
        {"u8", GLOIN_TOKEN_U8},
        {"u16", GLOIN_TOKEN_U16},
        {"u32", GLOIN_TOKEN_U32},
        {"u64", GLOIN_TOKEN_U64},
        {"u128", GLOIN_TOKEN_U128},
        {"f32", GLOIN_TOKEN_F32},
        {"f64", GLOIN_TOKEN_F64},
        {"f128", GLOIN_TOKEN_F128},
        {"string", GLOIN_TOKEN_STRING},
        {"void", GLOIN_TOKEN_VOID},
        {"true", GLOIN_TOKEN_TRUE},
        {"false", GLOIN_TOKEN_FALSE},
        {"null", GLOIN_TOKEN_NULL},
        {"struct", GLOIN_TOKEN_STRUCT},
        {"enum", GLOIN_TOKEN_ENUM},
        {"pub", GLOIN_TOKEN_PUB},
        {"priv", GLOIN_TOKEN_PRIV},
        {"static", GLOIN_TOKEN_STATIC},
        {"self", GLOIN_TOKEN_SELF},
        {"if", GLOIN_TOKEN_IF},
        {"unless", GLOIN_TOKEN_UNLESS},
        {"else", GLOIN_TOKEN_ELSE},
        {"for", GLOIN_TOKEN_FOR},
        {"while", GLOIN_TOKEN_WHILE},
        {"switch", GLOIN_TOKEN_SWITCH},
        {"match", GLOIN_TOKEN_MATCH},
        {"case", GLOIN_TOKEN_CASE},
        {"default", GLOIN_TOKEN_DEFAULT},
        {"break", GLOIN_TOKEN_BREAK},
        {"continue", GLOIN_TOKEN_CONTINUE},
        {"defer", GLOIN_TOKEN_DEFER},
        {"deferred", GLOIN_TOKEN_DEFERRED},
        {"spawnable", GLOIN_TOKEN_SPAWNABLE},
        {"run", GLOIN_TOKEN_RUN},
        {"_", GLOIN_TOKEN_UNDERSCORE}
    };
    if (const auto it = keywords.find(identifier); it != keywords.end()) {
        return it->second;
    }
    return GLOIN_TOKEN_IDENTIFIER;
}

void print_debug_token(const GloinToken &token) {
    std::cout << "Literal: " << token.literal << std::endl;
    std::cout << "Line Number: " << token.line_number << std::endl;
    std::cout << "Column: " << token.column << std::endl;
    std::cout << "Type: " << token_type_to_string(token.type) << std::endl;
}
