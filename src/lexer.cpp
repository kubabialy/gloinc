#include "lexer.h"

#include <iostream>
#include <ostream>
#include <unordered_map>
#include <cctype>
#include <vector>

GloinToken Lexer::next_token() {
    GloinToken token;
    token.line_number = line;
    token.column = column;

    while (current_char != '\0') {
        if (isspace(current_char) && current_char != '\n') {
            skip_whitespace();
            continue;
        }

        if (current_char == '/' && peek_token() == '/') {
            skip_comment();
            continue;
        }

        // Capture start position for the token
        const int start_line = line;
        const int start_column = column;

        if (current_char == '"') {
            return read_string(start_line, start_column);
        }

        if (current_char == '\'') {
            return {read_char(), start_line, GLOIN_TOKEN_CHAR, start_column};
        }

        if (isdigit(current_char)) {
            int temp_pos = position;
            int has_decimal = 0;

            while (temp_pos < src.size() &&
                   (isdigit(src[temp_pos]) || src[temp_pos] == '.')) {
                if (src[temp_pos] == '.') {
                    // Check if it is followed by another dot (Range operator ..)
                    if (temp_pos + 1 < src.size() && src[temp_pos + 1] == '.') {
                        has_decimal = 0;
                    } else {
                        has_decimal = 1;
                    }
                    break;
                }
                temp_pos++;
            }

            if (has_decimal) {
                return {read_float(), start_line, GLOIN_TOKEN_FLOAT, start_column};
            }

            return {read_number(), start_line, GLOIN_TOKEN_NUMBER, start_column};
        }

        if (isalpha(current_char) || current_char == '_') {
            const auto identifier = read_identifier();
            return {identifier, start_line, get_keyword_type(identifier), start_column};
        }

        switch (current_char) {
            case '(':
                advance();
                return {{"(", 1}, start_line, GLOIN_TOKEN_LPAREN, start_column};
            case ')':
                advance();
                return {{")", 1}, start_line, GLOIN_TOKEN_RPAREN, start_column};
            case '{':
                advance();
                return {{"{", 1}, start_line, GLOIN_TOKEN_LBRACE, start_column};
            case '}':
                advance();
                return {{"}", 1}, start_line, GLOIN_TOKEN_RBRACE, start_column};
            case '[':
                advance();
                return {{"[", 1}, start_line, GLOIN_TOKEN_LBRACKET, start_column};
            case ']':
                advance();
                return {{"]", 1}, start_line, GLOIN_TOKEN_RBRACKET, start_column};
            case ';':
                advance();
                return {";", start_line, GLOIN_TOKEN_SEMICOLON, start_column};
            case ':':
                if (peek_token() == ':') {
                    advance();
                    advance();
                    return {"::", start_line, GLOIN_TOKEN_DOUBLE_COLON, start_column};
                }
                advance();
                return {":", start_line, GLOIN_TOKEN_COLON, start_column};
            case '=':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"==", start_line, GLOIN_TOKEN_EQ, start_column};
                }

                if (peek_token() == '>') {
                    advance();
                    advance();
                }

                advance();
                return {"=", start_line, GLOIN_TOKEN_ASSIGN, start_column};
            case '+':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"+=", start_line, GLOIN_TOKEN_PLUS_ASSIGN, start_column};
                }
                advance();
                return {"+", start_line, GLOIN_TOKEN_PLUS, start_column};
            case '-':
                if (peek_token() == '>') {
                    advance();
                    advance();
                    return {"->", start_line, GLOIN_TOKEN_ARROW, start_column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"-=", start_line, GLOIN_TOKEN_MINUS_ASSIGN, start_column};
                }
                advance();
                return {"-", start_line, GLOIN_TOKEN_MINUS, start_column};
            case '*':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"*=", start_line, GLOIN_TOKEN_MULTIPLY_ASSIGN, start_column};
                }
                advance();
                return {"*", start_line, GLOIN_TOKEN_MULTIPLY, start_column};
            case '/':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"/=", start_line, GLOIN_TOKEN_DIVIDE_ASSIGN, start_column};
                }
                advance();
                return {"/", start_line, GLOIN_TOKEN_DIVIDE, start_column};
            case '!':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"!=", start_line, GLOIN_TOKEN_NE, start_column};
                }
                advance();
                return {"!", start_line, GLOIN_TOKEN_NOT, start_column};
            case '<':
                if (peek_token() == '<') {
                    advance();
                    advance();
                    return {"<<", start_line, GLOIN_TOKEN_SHL, start_column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"<=", start_line, GLOIN_TOKEN_LE, start_column};
                }
                advance();
                return {"<", start_line, GLOIN_TOKEN_LT, start_column};
            case '>':
                if (peek_token() == '>') {
                    advance();
                    advance();
                    return {">>", start_line, GLOIN_TOKEN_SHR, start_column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {">=", start_line, GLOIN_TOKEN_GE, start_column};
                }

                advance();
                return {">", start_line, GLOIN_TOKEN_GT, start_column};
            case '&':
                if (peek_token() == '&') {
                    advance();
                    advance();
                    return {"&&", start_line, GLOIN_TOKEN_AND, start_column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"&=", start_line, GLOIN_TOKEN_AND_ASSIGN, start_column};
                }
                advance();
                return {"&", start_line, GLOIN_TOKEN_AMPERSAND, start_column};
            case '|':
                if (peek_token() == '|') {
                    advance();
                    advance();
                    return {"||", start_line, GLOIN_TOKEN_OR, start_column};
                }
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"|=", start_line, GLOIN_TOKEN_OR_ASSIGN, start_column};
                }
                advance();
                return {"|", start_line, GLOIN_TOKEN_PIPE, start_column};
            case '^':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"^=", start_line, GLOIN_TOKEN_XOR_ASSIGN, start_column};
                }
                advance();
                return {"^", start_line, GLOIN_TOKEN_CARET, start_column};
            case '~':
                advance();
                return {"~", start_line, GLOIN_TOKEN_TILDE, start_column};
            case '%':
                if (peek_token() == '=') {
                    advance();
                    advance();
                    return {"%=", start_line, GLOIN_TOKEN_MODULO_ASSIGN, start_column};
                }
                advance();
                return {"%", start_line, GLOIN_TOKEN_PERCENT, start_column};
            case '?':
                advance();
                return {"?", start_line, GLOIN_TOKEN_QUESTION, start_column};
            case '.':
                if (peek_token() == '.') {
                    advance();
                    advance();
                }
                advance();
                return {".", start_line, GLOIN_TOKEN_DOT, start_column};
            case ',':
                advance();
                return {",", start_line, GLOIN_TOKEN_COMMA, start_column};
            case '#':
                advance();
                return {"#", start_line, GLOIN_TOKEN_HASH, start_column};
            case '@':
                advance();
                return {"@", start_line, GLOIN_TOKEN_AT, start_column};
            default:
                advance();
                const std::string_view literal(&src[position - 1], 1);
                return {literal, start_line, GLOIN_TOKEN_UNKNOWN, start_column};
        }
    }
    return {{"", 1}, line, GLOIN_TOKEN_EOF, column};
}

std::vector<GloinToken> Lexer::tokenize() {
    std::vector<GloinToken> tokens;
    GloinToken token = next_token();
    while (token.type != GLOIN_TOKEN_EOF) {
        tokens.push_back(token);
        token = next_token();
    }
    // Optionally include EOF? Parser expects it?
    // Usually parser loops until EOF.
    // If parser takes vector, it might expect EOF at end.
    tokens.push_back(token);
    return tokens;
}

std::string token_type_to_string(const GloinTokenType type) {
    switch (type) {
        case GLOIN_TOKEN_EOF:
            return "EOF";
        case GLOIN_TOKEN_IMPORT:
            return "IMPORT";
        case GLOIN_TOKEN_EXTERN:
            return "EXTERN";
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
        case GLOIN_TOKEN_SPAWNABLE:
            return "SPAWNABLE";
        case GLOIN_TOKEN_RUN:
            return "RUN";
        case GLOIN_TOKEN_PACKED:
            return "PACKED";
        case GLOIN_TOKEN_BIT:
            return "BIT";
        case GLOIN_TOKEN_KEYWORD_AT:
            return "AT_KEYWORD";
        case GLOIN_TOKEN_CUSTOM_WIDTH_INT:
            return "CUSTOM_WIDTH_INT";
        case GLOIN_TOKEN_BE_I8: return "BE_I8";
        case GLOIN_TOKEN_BE_I16: return "BE_I16";
        case GLOIN_TOKEN_BE_I32: return "BE_I32";
        case GLOIN_TOKEN_BE_I64: return "BE_I64";
        case GLOIN_TOKEN_BE_I128: return "BE_I128";
        case GLOIN_TOKEN_BE_U8: return "BE_U8";
        case GLOIN_TOKEN_BE_U16: return "BE_U16";
        case GLOIN_TOKEN_BE_U32: return "BE_U32";
        case GLOIN_TOKEN_BE_U64: return "BE_U64";
        case GLOIN_TOKEN_BE_U128: return "BE_U128";
        case GLOIN_TOKEN_LE_I8: return "LE_I8";
        case GLOIN_TOKEN_LE_I16: return "LE_I16";
        case GLOIN_TOKEN_LE_I32: return "LE_I32";
        case GLOIN_TOKEN_LE_I64: return "LE_I64";
        case GLOIN_TOKEN_LE_I128: return "LE_I128";
        case GLOIN_TOKEN_LE_U8: return "LE_U8";
        case GLOIN_TOKEN_LE_U16: return "LE_U16";
        case GLOIN_TOKEN_LE_U32: return "LE_U32";
        case GLOIN_TOKEN_LE_U64: return "LE_U64";
        case GLOIN_TOKEN_LE_U128: return "LE_U128";
        case GLOIN_TOKEN_COMMENT:
            return "COMMENT";
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
    if (current_char == '/' && peek_token() == '/') {
        advance();
        advance();
        while (current_char != '\0' && current_char != '\n') {
            advance();
        }
    }
}

std::string_view Lexer::capture_view(const int start_pos) const {
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

GloinToken Lexer::read_string(const int start_line, const int start_column) {
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
        {"break", GLOIN_TOKEN_BREAK},
        {"continue", GLOIN_TOKEN_CONTINUE},
        {"defer", GLOIN_TOKEN_DEFER},
        {"deferred", GLOIN_TOKEN_DEFERRED},
        {"spawnable", GLOIN_TOKEN_SPAWNABLE},
        {"run", GLOIN_TOKEN_RUN},
        {"packed", GLOIN_TOKEN_PACKED},
        {"bit", GLOIN_TOKEN_BIT},
        {"at", GLOIN_TOKEN_KEYWORD_AT},
        // Big Endian
        {"be_i8", GLOIN_TOKEN_BE_I8}, {"be_i16", GLOIN_TOKEN_BE_I16}, {"be_i32", GLOIN_TOKEN_BE_I32}, {"be_i64", GLOIN_TOKEN_BE_I64}, {"be_i128", GLOIN_TOKEN_BE_I128},
        {"be_u8", GLOIN_TOKEN_BE_U8}, {"be_u16", GLOIN_TOKEN_BE_U16}, {"be_u32", GLOIN_TOKEN_BE_U32}, {"be_u64", GLOIN_TOKEN_BE_U64}, {"be_u128", GLOIN_TOKEN_BE_U128},
        // Little Endian
        {"le_i8", GLOIN_TOKEN_LE_I8}, {"le_i16", GLOIN_TOKEN_LE_I16}, {"le_i32", GLOIN_TOKEN_LE_I32}, {"le_i64", GLOIN_TOKEN_LE_I64}, {"le_i128", GLOIN_TOKEN_LE_I128},
        {"le_u8", GLOIN_TOKEN_LE_U8}, {"le_u16", GLOIN_TOKEN_LE_U16}, {"le_u32", GLOIN_TOKEN_LE_U32}, {"le_u64", GLOIN_TOKEN_LE_U64}, {"le_u128", GLOIN_TOKEN_LE_U128},
        {"_", GLOIN_TOKEN_UNDERSCORE}
    };
    if (const auto it = keywords.find(identifier); it != keywords.end()) {
        return it->second;
    }

    // Check for custom width integers (u4, u5, etc.)
    if (identifier.size() > 1 && identifier[0] == 'u') {
        bool all_digits = true;
        for (size_t i = 1; i < identifier.size(); ++i) {
            if (!isdigit(identifier[i])) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            return GLOIN_TOKEN_CUSTOM_WIDTH_INT;
        }
    }

    return GLOIN_TOKEN_IDENTIFIER;
}

void print_debug_token(const GloinToken &token) {
    std::cout << "Literal: " << token.literal << std::endl;
    std::cout << "Line Number: " << token.line_number << std::endl;
    std::cout << "Column: " << token.column << std::endl;
    std::cout << "Type: " << token_type_to_string(token.type) << std::endl;
}
