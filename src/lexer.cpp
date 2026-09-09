#include "lexer.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>

namespace {
bool ascii_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool identifier_byte(unsigned char c) { return ascii_alpha(c) || ascii_digit(c) || c == '_'; }
bool hex_digit(unsigned char c) {
    return ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Validate Unicode scalar encodings, including overlong forms, surrogates and the upper bound.
size_t utf8_width(std::string_view text, size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80)
        return 1;
    size_t width = first >= 0xc2 && first <= 0xdf   ? 2
                   : first >= 0xe0 && first <= 0xef ? 3
                   : first >= 0xf0 && first <= 0xf4 ? 4
                                                    : 0;
    if (!width || width > text.size() - offset)
        return 0;
    for (size_t i = 1; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if (byte < 0x80 || byte > 0xbf)
            return 0;
    }
    const auto second = static_cast<unsigned char>(text[offset + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
        return 0;
    return width;
}
} // namespace

Lexer::Lexer(std::string text, std::string filename, std::shared_ptr<Diagnostics> diagnostics)
    : source(std::make_shared<SourceFile>(std::move(filename), std::move(text))),
      diagnostics_(std::move(diagnostics)), src(source->text) {
    // Validate the entire file, including trivia that tokenization would otherwise skip.
    for (size_t offset = 0; offset < src.size();) {
        size_t width = utf8_width(src, offset);
        const char *error = nullptr;
        if (!width)
            error = "Invalid UTF-8 in source";
        else if (src[offset] == '\0')
            error = "NUL byte in source";
        else if (offset == 0 && src.compare(0, 3, "\xef\xbb\xbf") == 0)
            error = "UTF-8 BOM is not supported";
        if (error) {
            invalid_source_offsets.push_back(offset);
            diagnostics_->error(DiagnosticStage::Lexing,
                                {source, offset, offset + (width ? width : 1)}, error);
        }
        offset += width ? width : 1;
    }
}

bool Lexer::source_error_in(size_t start, size_t end) const {
    auto it = std::lower_bound(invalid_source_offsets.begin(), invalid_source_offsets.end(), start);
    return it != invalid_source_offsets.end() && *it < end;
}

GloinToken Lexer::token(GloinTokenType type, size_t start, size_t literal_start,
                        size_t literal_end) {
    auto [line, column] = source->line_column(start);
    if (source_error_in(start, position))
        type = GLOIN_TOKEN_UNKNOWN;
    return {std::string_view(src).substr(literal_start, literal_end - literal_start),
            static_cast<int64_t>(line),
            type,
            static_cast<int>(column),
            {source, start, position}};
}

GloinToken Lexer::invalid(size_t start, const std::string &message) {
    if (!source_error_in(start, position))
        diagnostics_->error(DiagnosticStage::Lexing, {source, start, position}, message);
    return token(GLOIN_TOKEN_UNKNOWN, start, start, position);
}

GloinToken Lexer::next_token() {
    while (position < src.size()) {
        if (src[position] == ' ' || src[position] == '\t') {
            ++position;
        } else if (src.compare(position, 2, "//") == 0) {
            position += 2;
            while (position < src.size() && src[position] != '\n' && src[position] != '\r')
                ++position;
        } else {
            break;
        }
    }
    const size_t start = position;
    if (position == src.size())
        return token(GLOIN_TOKEN_EOF, start, start, start);
    const auto c = static_cast<unsigned char>(src[position]);
    if (c == '\n' || src.compare(position, 2, "\r\n") == 0) {
        position += c == '\n' ? 1 : 2;
        return token(GLOIN_TOKEN_NEWLINE, start, start, position);
    }
    if (c == '"' || c == '\'')
        return read_quoted(static_cast<char>(c));
    if (ascii_digit(c))
        return read_number();
    if (ascii_alpha(c) || c == '_' || c >= 0x80) {
        bool non_ascii = false;
        do {
            non_ascii |= static_cast<unsigned char>(src[position]) >= 0x80;
            ++position;
        } while (position < src.size() &&
                 (identifier_byte(static_cast<unsigned char>(src[position])) ||
                  static_cast<unsigned char>(src[position]) >= 0x80));
        if (non_ascii)
            return invalid(start,
                           "Identifiers must contain only ASCII letters, digits and underscores");
        return token(get_keyword_type(std::string_view(src).substr(start, position - start)), start,
                     start, position);
    }
    if (src.compare(position, 2, "/*") == 0) {
        auto end = src.find("*/", position + 2);
        position = end == std::string::npos ? src.size() : end + 2;
        return invalid(start, "Block comments are not supported; use // comments");
    }
    // Longest recognized spelling first; consuming an operator never consumes its next operand.
    static constexpr std::pair<std::string_view, GloinTokenType> pairs[] = {
        {"::", GLOIN_TOKEN_DOUBLE_COLON},
        {"->", GLOIN_TOKEN_ARROW},
        {"=>", GLOIN_TOKEN_DOUBLE_ARROW},
        {"..", GLOIN_TOKEN_RANGE},
        {"==", GLOIN_TOKEN_EQ},
        {"!=", GLOIN_TOKEN_NE},
        {"<=", GLOIN_TOKEN_LE},
        {">=", GLOIN_TOKEN_GE},
        {"&&", GLOIN_TOKEN_AND},
        {"||", GLOIN_TOKEN_OR},
        {"<<", GLOIN_TOKEN_SHL},
        {">>", GLOIN_TOKEN_SHR},
        {"+=", GLOIN_TOKEN_PLUS_ASSIGN},
        {"-=", GLOIN_TOKEN_MINUS_ASSIGN},
        {"*=", GLOIN_TOKEN_MULTIPLY_ASSIGN},
        {"/=", GLOIN_TOKEN_DIVIDE_ASSIGN},
        {"%=", GLOIN_TOKEN_MODULO_ASSIGN},
        {"&=", GLOIN_TOKEN_AND_ASSIGN},
        {"|=", GLOIN_TOKEN_OR_ASSIGN},
        {"^=", GLOIN_TOKEN_XOR_ASSIGN},
    };
    for (auto [spelling, type] : pairs) {
        if (src.compare(position, spelling.size(), spelling) == 0) {
            position += spelling.size();
            return token(type, start, start, position);
        }
    }
    ++position;
    GloinTokenType type;
    switch (c) {
    case '(':
        type = GLOIN_TOKEN_LPAREN;
        break;
    case ')':
        type = GLOIN_TOKEN_RPAREN;
        break;
    case '{':
        type = GLOIN_TOKEN_LBRACE;
        break;
    case '}':
        type = GLOIN_TOKEN_RBRACE;
        break;
    case '[':
        type = GLOIN_TOKEN_LBRACKET;
        break;
    case ']':
        type = GLOIN_TOKEN_RBRACKET;
        break;
    case ';':
        type = GLOIN_TOKEN_SEMICOLON;
        break;
    case ':':
        type = GLOIN_TOKEN_COLON;
        break;
    case ',':
        type = GLOIN_TOKEN_COMMA;
        break;
    case '=':
        type = GLOIN_TOKEN_ASSIGN;
        break;
    case '+':
        type = GLOIN_TOKEN_PLUS;
        break;
    case '-':
        type = GLOIN_TOKEN_MINUS;
        break;
    case '*':
        type = GLOIN_TOKEN_MULTIPLY;
        break;
    case '/':
        type = GLOIN_TOKEN_DIVIDE;
        break;
    case '!':
        type = GLOIN_TOKEN_NOT;
        break;
    case '<':
        type = GLOIN_TOKEN_LT;
        break;
    case '>':
        type = GLOIN_TOKEN_GT;
        break;
    case '&':
        type = GLOIN_TOKEN_AMPERSAND;
        break;
    case '|':
        type = GLOIN_TOKEN_PIPE;
        break;
    case '^':
        type = GLOIN_TOKEN_CARET;
        break;
    case '~':
        type = GLOIN_TOKEN_TILDE;
        break;
    case '%':
        type = GLOIN_TOKEN_PERCENT;
        break;
    case '?':
        type = GLOIN_TOKEN_QUESTION;
        break;
    case '.':
        if (position < src.size() && ascii_digit(src[position])) {
            while (position < src.size() &&
                   (identifier_byte(src[position]) ||
                    (src[position] == '.' && src.compare(position, 2, "..") != 0)))
                ++position;
            return invalid(start, "A floating literal requires digits before the decimal point");
        }
        type = GLOIN_TOKEN_DOT;
        break;
    case '#':
        type = GLOIN_TOKEN_HASH;
        break;
    case '@':
        type = GLOIN_TOKEN_AT;
        break;
    default:
        return invalid(start, "Invalid source character");
    }
    return token(type, start, start, position);
}

GloinToken Lexer::read_number() {
    const size_t start = position;
    bool valid = true;
    bool floating = false;
    if (src[position] == '0' && position + 1 < src.size() &&
        (src[position + 1] == 'x' || src[position + 1] == 'X' || src[position + 1] == 'b' ||
         src[position + 1] == 'B')) {
        bool hex = src[position + 1] == 'x' || src[position + 1] == 'X';
        position += 2;
        const size_t digits = position;
        while (position < src.size() &&
               (hex ? hex_digit(src[position]) : src[position] == '0' || src[position] == '1'))
            ++position;
        valid = position != digits;
    } else {
        while (position < src.size() && ascii_digit(src[position]))
            ++position;
        if (position < src.size() && src[position] == '.' && src.compare(position, 2, "..") != 0) {
            floating = true;
            const size_t digits = ++position;
            while (position < src.size() && ascii_digit(src[position]))
                ++position;
            valid = position != digits;
        }
        if (position < src.size() && (src[position] == 'e' || src[position] == 'E')) {
            floating = true;
            ++position;
            if (position < src.size() && (src[position] == '+' || src[position] == '-'))
                ++position;
            const size_t digits = position;
            while (position < src.size() && ascii_digit(src[position]))
                ++position;
            valid &= position != digits;
        }
    }
    // Invalid suffixes are one erroneous token, not a valid number followed by a name.
    while (position < src.size() &&
           (identifier_byte(static_cast<unsigned char>(src[position])) ||
            static_cast<unsigned char>(src[position]) >= 0x80 ||
            (src[position] == '.' && src.compare(position, 2, "..") != 0))) {
        valid = false;
        ++position;
    }
    if (!valid)
        return invalid(start, "Malformed numeric literal");
    return token(floating ? GLOIN_TOKEN_FLOAT : GLOIN_TOKEN_NUMBER, start, start, position);
}

GloinToken Lexer::read_quoted(char quote) {
    const size_t start = position++;
    const size_t content = position;
    size_t characters = 0;
    bool valid = true;
    while (position < src.size() && src[position] != quote && src[position] != '\n' &&
           src[position] != '\r') {
        if (src[position] == '\\') {
            ++position;
            if (position == src.size() || src[position] == '\n' || src[position] == '\r') {
                valid = false;
                break;
            }
            // Preserve source spelling; escape decoding/storage belongs to SPEC-022.
            switch (src[position]) {
            case '\\':
            case '"':
            case '\'':
            case 'n':
            case 'r':
            case 't':
            case '0':
                break;
            default:
                valid = false;
                break;
            }
            ++position;
        } else {
            size_t width = utf8_width(src, position);
            if (!width || static_cast<unsigned char>(src[position]) < 0x20 ||
                src[position] == '\x7f')
                valid = false;
            position += width ? width : 1;
        }
        ++characters;
    }
    const size_t end = position;
    bool closed = position < src.size() && src[position] == quote;
    if (closed)
        ++position;
    if (!closed || !valid || (quote == '\'' && characters != 1)) {
        auto result = invalid(start, !closed ? "Unterminated quoted literal"
                                             : "Invalid escape or character literal");
        result.literal = std::string_view(src).substr(content, end - content);
        return result;
    }
    return token(quote == '"' ? GLOIN_TOKEN_STRING_LITERAL : GLOIN_TOKEN_CHAR, start, content, end);
}

std::vector<GloinToken> Lexer::tokenize() {
    std::vector<GloinToken> tokens;
    do {
        tokens.push_back(next_token());
    } while (tokens.back().type != GLOIN_TOKEN_EOF);
    return tokens;
}

std::string token_type_to_string(const GloinTokenType type) {
    switch (type) {
    case GLOIN_TOKEN_FN:
        return "FN";
    case GLOIN_TOKEN_SPAWN:
        return "SPAWN";
    case GLOIN_TOKEN_AWAIT:
        return "AWAIT";
    case GLOIN_TOKEN_SWITCH:
        return "SWITCH";
    case GLOIN_TOKEN_MATCH:
        return "MATCH";
    case GLOIN_TOKEN_CASE:
        return "CASE";
    case GLOIN_TOKEN_DEFAULT:
        return "DEFAULT";
    case GLOIN_TOKEN_IN:
        return "IN";
    case GLOIN_TOKEN_RANGE:
        return "RANGE";
    case GLOIN_TOKEN_DOUBLE_ARROW:
        return "DOUBLE_ARROW";
    case GLOIN_TOKEN_NEWLINE:
        return "NEWLINE";
    case GLOIN_TOKEN_INT:
        return "INT";
    case GLOIN_TOKEN_USIZE:
        return "USIZE";
    case GLOIN_TOKEN_CHAR_TYPE:
        return "CHAR_TYPE";
    case GLOIN_TOKEN_STRING_LITERAL:
        return "STRING_LITERAL";

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
    case GLOIN_TOKEN_BE_I8:
        return "BE_I8";
    case GLOIN_TOKEN_BE_I16:
        return "BE_I16";
    case GLOIN_TOKEN_BE_I32:
        return "BE_I32";
    case GLOIN_TOKEN_BE_I64:
        return "BE_I64";
    case GLOIN_TOKEN_BE_I128:
        return "BE_I128";
    case GLOIN_TOKEN_BE_U8:
        return "BE_U8";
    case GLOIN_TOKEN_BE_U16:
        return "BE_U16";
    case GLOIN_TOKEN_BE_U32:
        return "BE_U32";
    case GLOIN_TOKEN_BE_U64:
        return "BE_U64";
    case GLOIN_TOKEN_BE_U128:
        return "BE_U128";
    case GLOIN_TOKEN_LE_I8:
        return "LE_I8";
    case GLOIN_TOKEN_LE_I16:
        return "LE_I16";
    case GLOIN_TOKEN_LE_I32:
        return "LE_I32";
    case GLOIN_TOKEN_LE_I64:
        return "LE_I64";
    case GLOIN_TOKEN_LE_I128:
        return "LE_I128";
    case GLOIN_TOKEN_LE_U8:
        return "LE_U8";
    case GLOIN_TOKEN_LE_U16:
        return "LE_U16";
    case GLOIN_TOKEN_LE_U32:
        return "LE_U32";
    case GLOIN_TOKEN_LE_U64:
        return "LE_U64";
    case GLOIN_TOKEN_LE_U128:
        return "LE_U128";
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

GloinTokenType get_keyword_type(std::string_view identifier) {
    static const std::unordered_map<std::string_view, GloinTokenType> keywords = {
        {"fn", GLOIN_TOKEN_FN},
        {"spawn", GLOIN_TOKEN_SPAWN},
        {"await", GLOIN_TOKEN_AWAIT},
        {"switch", GLOIN_TOKEN_SWITCH},
        {"match", GLOIN_TOKEN_MATCH},
        {"case", GLOIN_TOKEN_CASE},
        {"default", GLOIN_TOKEN_DEFAULT},
        {"in", GLOIN_TOKEN_IN},
        {"int", GLOIN_TOKEN_INT},
        {"usize", GLOIN_TOKEN_USIZE},
        {"char", GLOIN_TOKEN_CHAR_TYPE},
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
        {"be_i8", GLOIN_TOKEN_BE_I8},
        {"be_i16", GLOIN_TOKEN_BE_I16},
        {"be_i32", GLOIN_TOKEN_BE_I32},
        {"be_i64", GLOIN_TOKEN_BE_I64},
        {"be_i128", GLOIN_TOKEN_BE_I128},
        {"be_u8", GLOIN_TOKEN_BE_U8},
        {"be_u16", GLOIN_TOKEN_BE_U16},
        {"be_u32", GLOIN_TOKEN_BE_U32},
        {"be_u64", GLOIN_TOKEN_BE_U64},
        {"be_u128", GLOIN_TOKEN_BE_U128},
        // Little Endian
        {"le_i8", GLOIN_TOKEN_LE_I8},
        {"le_i16", GLOIN_TOKEN_LE_I16},
        {"le_i32", GLOIN_TOKEN_LE_I32},
        {"le_i64", GLOIN_TOKEN_LE_I64},
        {"le_i128", GLOIN_TOKEN_LE_I128},
        {"le_u8", GLOIN_TOKEN_LE_U8},
        {"le_u16", GLOIN_TOKEN_LE_U16},
        {"le_u32", GLOIN_TOKEN_LE_U32},
        {"le_u64", GLOIN_TOKEN_LE_U64},
        {"le_u128", GLOIN_TOKEN_LE_U128},
        {"_", GLOIN_TOKEN_UNDERSCORE}};
    if (const auto it = keywords.find(identifier); it != keywords.end()) {
        return it->second;
    }

    // Reserve signed/unsigned custom widths and both pending endian spelling families.
    if (identifier.starts_with("be_") || identifier.starts_with("le_"))
        identifier.remove_prefix(3);
    if (identifier.ends_with("_be") || identifier.ends_with("_le"))
        identifier.remove_suffix(3);
    if (identifier.size() > 1 && (identifier[0] == 'u' || identifier[0] == 'i') &&
        std::all_of(identifier.begin() + 1, identifier.end(), ascii_digit))
        return GLOIN_TOKEN_CUSTOM_WIDTH_INT;

    return GLOIN_TOKEN_IDENTIFIER;
}

void print_debug_token(const GloinToken &token) {
    std::cout << "Literal: " << token.literal << std::endl;
    std::cout << "Line Number: " << token.line_number << std::endl;
    std::cout << "Column: " << token.column << std::endl;
    std::cout << "Type: " << token_type_to_string(token.type) << std::endl;
}

bool is_type_token(GloinTokenType type) {
    return (type >= GLOIN_TOKEN_BOOL && type <= GLOIN_TOKEN_VOID) ||
           (type >= GLOIN_TOKEN_BE_I8 && type <= GLOIN_TOKEN_LE_U128) ||
           type == GLOIN_TOKEN_STRING || type == GLOIN_TOKEN_BIT ||
           type == GLOIN_TOKEN_CUSTOM_WIDTH_INT;
}
