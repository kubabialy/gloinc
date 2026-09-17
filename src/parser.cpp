#include "parser.h"

namespace {
// Restore contextual grammar flags even when a parse failure unwinds the stack.
template <typename T> struct Restore {
    T &target;
    T previous;
    Restore(T &target, T value) : target(target), previous(target) { target = value; }
    ~Restore() { target = previous; }
};
} // namespace

GloinParser::GloinParser(Lexer l, ParseMode mode) : lexer(std::move(l)), mode(mode) {
    for (auto token : lexer.tokenize()) {
        if (token.type != GLOIN_TOKEN_NEWLINE && token.type != GLOIN_TOKEN_COMMENT)
            tokens.push_back(token);
    }
    current_token = tokens.front();
    next_token = tokens[std::min(size_t{1}, tokens.size() - 1)];
}

// Every parser consumes its complete construct and leaves the first unconsumed token current.
void GloinParser::advance_token() {
    consumed_end = current_token.span.end;
    if (cursor + 1 < tokens.size())
        ++cursor;
    current_token = tokens[cursor];
    next_token = tokens[std::min(cursor + 1, tokens.size() - 1)];
}

bool GloinParser::accept(GloinTokenType type) {
    if (current_token.type != type)
        return false;
    advance_token();
    return true;
}

void GloinParser::expect(GloinTokenType type, const std::string &message) {
    if (!accept(type))
        fail(message);
}

void GloinParser::require_extended(const std::string &feature) {
    if (mode == ParseMode::Core)
        fail(feature + " is not supported in the core language");
}

std::unique_ptr<Identifier> GloinParser::parse_name(bool receiver) {
    if (current_token.type != GLOIN_TOKEN_IDENTIFIER &&
        !(receiver && current_token.type == GLOIN_TOKEN_SELF))
        fail("Expected identifier");
    auto name = located_node<Identifier>(std::string(current_token.literal));
    advance_token();
    return name;
}

int GloinParser::get_binding_power(GloinTokenType type) {
    switch (type) {
    case GLOIN_TOKEN_OR:
        return 10;
    case GLOIN_TOKEN_AND:
        return 20;
    case GLOIN_TOKEN_EQ:
    case GLOIN_TOKEN_NE:
        return 30;
    case GLOIN_TOKEN_LT:
    case GLOIN_TOKEN_LE:
    case GLOIN_TOKEN_GT:
    case GLOIN_TOKEN_GE:
        return 40;
    case GLOIN_TOKEN_PLUS:
    case GLOIN_TOKEN_MINUS:
        return 50;
    case GLOIN_TOKEN_MULTIPLY:
    case GLOIN_TOKEN_DIVIDE:
    case GLOIN_TOKEN_PERCENT:
        return 60;
    // All postfix constructs bind tighter than prefix operators (70).
    case GLOIN_TOKEN_LPAREN:
    case GLOIN_TOKEN_DOT:
    case GLOIN_TOKEN_LBRACKET:
        return 80;
    default:
        return 0;
    }
}

std::unique_ptr<Expression> GloinParser::parse_expression_impl(int min_binding_power) {
    auto left = parse_prefix();
    while (get_binding_power(current_token.type) > min_binding_power)
        left = parse_infix(std::move(left));
    return left;
}

// In deferred expression syntax, only a balanced type-argument list followed by '{'
// can introduce a generic aggregate literal. Identifier capitalization plays no role.
bool GloinParser::generic_literal_ahead() const {
    if (!allow_struct_literal || current_token.type != GLOIN_TOKEN_IDENTIFIER ||
        next_token.type != GLOIN_TOKEN_LT)
        return false;
    int depth = 0;
    for (size_t i = cursor + 1; i + 1 < tokens.size(); ++i) {
        auto type = tokens[i].type;
        if (type == GLOIN_TOKEN_LT)
            ++depth;
        else if (type == GLOIN_TOKEN_GT)
            --depth;
        else if (type == GLOIN_TOKEN_SHR)
            depth -= 2;
        else if (type != GLOIN_TOKEN_IDENTIFIER && !is_type_token(type) &&
                 type != GLOIN_TOKEN_COMMA && type != GLOIN_TOKEN_DOT &&
                 type != GLOIN_TOKEN_MULTIPLY && type != GLOIN_TOKEN_AMPERSAND)
            return false;
        if (depth <= 0)
            return depth == 0 && tokens[i + 1].type == GLOIN_TOKEN_LBRACE;
    }
    return false;
}

std::unique_ptr<Expression> GloinParser::parse_prefix_impl() {
    switch (current_token.type) {
    case GLOIN_TOKEN_NUMBER: {
        auto node = located_node<IntegerLiteral>(std::string(current_token.literal));
        advance_token();
        return node;
    }
    case GLOIN_TOKEN_FLOAT: {
        auto node = located_node<FloatLiteral>(std::string(current_token.literal));
        advance_token();
        return node;
    }
    case GLOIN_TOKEN_TRUE:
    case GLOIN_TOKEN_FALSE: {
        auto node = located_node<BooleanLiteral>(current_token.type == GLOIN_TOKEN_TRUE);
        advance_token();
        return node;
    }
    case GLOIN_TOKEN_STRING_LITERAL: {
        require_extended("String literals");
        auto node = located_node<StringLiteral>(std::string(current_token.literal));
        advance_token();
        return node;
    }
    case GLOIN_TOKEN_LBRACKET: {
        require_extended("Array literals");
        advance_token();
        return std::make_unique<ArrayLiteral>(parse_expression_list(GLOIN_TOKEN_RBRACKET));
    }
    case GLOIN_TOKEN_SELF:
        require_extended("Receiver expressions");
        return parse_name(true);
    case GLOIN_TOKEN_IDENTIFIER: {
        bool generic = generic_literal_ahead();
        auto name = generic ? parse_type() : parse_name();
        if (!allow_struct_literal || current_token.type != GLOIN_TOKEN_LBRACE)
            return name;
        require_extended("Struct literals");
        advance_token();
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        while (current_token.type != GLOIN_TOKEN_RBRACE) {
            auto field = parse_name();
            expect(GLOIN_TOKEN_COLON, "Expected ':' after struct literal field");
            fields.emplace_back(field->value, parse_expression(0));
            if (!accept(GLOIN_TOKEN_COMMA))
                break;
        }
        expect(GLOIN_TOKEN_RBRACE, "Expected ',' or '}' in struct literal");
        return std::make_unique<StructLiteral>(std::move(name), std::move(fields));
    }
    case GLOIN_TOKEN_RUN: {
        require_extended("Run expressions");
        auto op = current_token.type;
        advance_token();
        return std::make_unique<SpawnExpression>(op, parse_expression(70));
    }
    case GLOIN_TOKEN_SPAWN:
    case GLOIN_TOKEN_AWAIT:
        fail("Reserved legacy syntax is not supported: " + std::string(current_token.literal));
    case GLOIN_TOKEN_CHAR:
        fail("Character literals are not supported in the core language");
    case GLOIN_TOKEN_AMPERSAND:
    case GLOIN_TOKEN_MULTIPLY:
        require_extended("Pointer expressions");
        [[fallthrough]];
    case GLOIN_TOKEN_MINUS:
    case GLOIN_TOKEN_NOT: {
        std::string op(current_token.literal);
        advance_token();
        return std::make_unique<PrefixExpression>(op, parse_expression(70));
    }
    case GLOIN_TOKEN_LPAREN: {
        advance_token();
        Restore grouped(allow_struct_literal, true);
        auto expression = parse_expression(0);
        expect(GLOIN_TOKEN_RPAREN, "Expected ')' after expression");
        return expression;
    }
    default:
        fail("Expected expression, got '" + std::string(current_token.literal) + "'");
    }
}

std::unique_ptr<Expression> GloinParser::parse_infix_impl(std::unique_ptr<Expression> left) {
    auto type = current_token.type;
    if (type == GLOIN_TOKEN_LPAREN) {
        advance_token();
        Restore arguments(allow_struct_literal, true);
        auto args = parse_expression_list(GLOIN_TOKEN_RPAREN);
        return std::make_unique<CallExpression>(std::move(left), std::move(args));
    }
    if (type == GLOIN_TOKEN_LBRACKET) {
        require_extended("Index expressions");
        advance_token();
        Restore index_context(allow_struct_literal, true);
        auto index = parse_expression(0);
        expect(GLOIN_TOKEN_RBRACKET, "Expected ']' after index");
        return std::make_unique<IndexExpression>(std::move(left), std::move(index));
    }
    if (type == GLOIN_TOKEN_DOT) {
        require_extended("Member access");
        advance_token();
        return std::make_unique<MemberAccessExpression>(std::move(left), parse_name());
    }
    std::string op(current_token.literal);
    int precedence = get_binding_power(type);
    advance_token();
    auto right = parse_expression(precedence);
    return std::make_unique<InfixExpression>(std::move(left), op, std::move(right));
}

std::vector<std::unique_ptr<Expression>> GloinParser::parse_expression_list(GloinTokenType end) {
    std::vector<std::unique_ptr<Expression>> list;
    while (current_token.type != end) {
        list.push_back(parse_expression(0));
        if (!accept(GLOIN_TOKEN_COMMA))
            break;
    }
    expect(end, "Expected ',' or closing delimiter in expression list");
    return list;
}

std::unique_ptr<Expression> GloinParser::parse_assignment() {
    auto left = parse_expression(0);
    if (!accept(GLOIN_TOKEN_ASSIGN))
        return left;
    auto span = left->span;
    bool addressable = dynamic_cast<Identifier *>(left.get()) ||
                       dynamic_cast<MemberAccessExpression *>(left.get()) ||
                       dynamic_cast<IndexExpression *>(left.get());
    if (auto *prefix = dynamic_cast<PrefixExpression *>(left.get()))
        addressable = prefix->op == "*";
    if (!addressable)
        fail("Expected assignable target");
    auto right = parse_expression(0);
    if (current_token.type == GLOIN_TOKEN_ASSIGN)
        fail("Chained assignment is not supported");
    auto assignment = std::make_unique<AssignmentExpression>(std::move(left), std::move(right));
    assignment->span = {span.source, span.begin, consumed_end};
    return assignment;
}

std::unique_ptr<Expression> GloinParser::parse_condition() {
    Restore condition_context(allow_struct_literal, false);
    return parse_expression(0);
}

std::unique_ptr<Statement> GloinParser::parse_def_statement_impl() {
    expect(GLOIN_TOKEN_DEF, "Expected 'def'");
    bool visibility =
        current_token.type == GLOIN_TOKEN_PUB || current_token.type == GLOIN_TOKEN_PRIV;
    bool is_public = current_token.type == GLOIN_TOKEN_PUB;
    if (visibility) {
        if (block_depth)
            fail("Visibility is not allowed on local declarations");
        advance_token();
    }
    bool is_const = accept(GLOIN_TOKEN_CONST);
    bool spawnable = false, deferred = false;
    if (!is_const && (current_token.type == GLOIN_TOKEN_SPAWNABLE ||
                      current_token.type == GLOIN_TOKEN_DEFERRED)) {
        require_extended("Concurrency declarations");
        spawnable = current_token.type == GLOIN_TOKEN_SPAWNABLE;
        deferred = current_token.type == GLOIN_TOKEN_DEFERRED;
        advance_token();
    }
    if (!is_const && !spawnable && !deferred &&
        (current_token.type == GLOIN_TOKEN_STRUCT || current_token.type == GLOIN_TOKEN_PACKED)) {
        require_extended("Struct declarations");
        bool packed = accept(GLOIN_TOKEN_PACKED);
        auto definition = parse_struct_definition(packed);
        static_cast<StructDefinition *>(definition.get())->is_public = is_public;
        return definition;
    }
    if (!is_const &&
        (spawnable || deferred ||
         (current_token.type == GLOIN_TOKEN_IDENTIFIER &&
          (next_token.type == GLOIN_TOKEN_LPAREN || next_token.type == GLOIN_TOKEN_LT)))) {
        if (block_depth)
            fail("Nested functions are not supported");
        auto function = parse_function_definition(spawnable, deferred);
        function->is_public = is_public;
        return function;
    }
    if (is_const && current_token.type == GLOIN_TOKEN_MUT)
        fail("'const' and 'mut' cannot be combined");
    if (visibility && !is_const)
        fail("Visibility requires a function or constant declaration");
    auto variable = parse_variable_declaration();
    variable->is_const = is_const;
    variable->is_public = is_public;
    if (is_const && !variable->initializer)
        fail("Constant declaration requires an initializer");
    return variable;
}

std::unique_ptr<Statement> GloinParser::parse_statement_impl() {
    switch (current_token.type) {
    case GLOIN_TOKEN_DEF:
        return parse_def_statement();
    case GLOIN_TOKEN_RETURN:
        return parse_return_statement();
    case GLOIN_TOKEN_IF:
        return parse_if_statement();
    case GLOIN_TOKEN_UNLESS:
        return parse_unless_statement();
    case GLOIN_TOKEN_WHILE:
        return parse_while_statement();
    case GLOIN_TOKEN_FOR:
        return parse_for_statement();
    case GLOIN_TOKEN_DEFER:
        require_extended("Defer statements");
        return parse_defer_statement();
    case GLOIN_TOKEN_IMPORT:
        require_extended("Imports");
        return parse_import_statement();
    case GLOIN_TOKEN_LBRACE:
        return parse_block_statement();
    default:
        return parse_expression_statement();
    }
}

std::vector<std::string> GloinParser::parse_generic_params() {
    std::vector<std::string> params;
    if (!accept(GLOIN_TOKEN_LT))
        return params;
    require_extended("Generic declarations");
    do {
        params.push_back(parse_name()->value);
        if (!accept(GLOIN_TOKEN_COMMA) || current_token.type == GLOIN_TOKEN_GT)
            break;
    } while (true);
    expect(GLOIN_TOKEN_GT, "Expected ',' or '>' after generic parameter");
    return params;
}

void GloinParser::consume_type_close() {
    if (current_token.type == GLOIN_TOKEN_SHR) {
        // Consume only the first '>' and retain the second, with its own byte position.
        consumed_end = current_token.span.begin + 1;
        ++current_token.span.begin;
        ++current_token.column;
        current_token.literal.remove_prefix(1);
        current_token.type = GLOIN_TOKEN_GT;
        return;
    }
    expect(GLOIN_TOKEN_GT, "Expected ',' or '>' after type argument");
}

std::unique_ptr<Identifier> GloinParser::parse_type_impl() {
    std::string text;
    while (current_token.type == GLOIN_TOKEN_MULTIPLY ||
           current_token.type == GLOIN_TOKEN_AMPERSAND) {
        require_extended("Pointer types");
        text += current_token.literal;
        advance_token();
    }
    if (accept(GLOIN_TOKEN_LBRACKET)) {
        require_extended("Array types");
        auto element = parse_type();
        expect(GLOIN_TOKEN_SEMICOLON, "Expected ';' in array type");
        if (current_token.type != GLOIN_TOKEN_NUMBER)
            fail("Expected array size number");
        std::string size(current_token.literal);
        advance_token();
        expect(GLOIN_TOKEN_RBRACKET, "Expected ']' after array size");
        text += "[" + element->value + "; " + size + "]";
    } else {
        if (current_token.type != GLOIN_TOKEN_IDENTIFIER && !is_type_token(current_token.type))
            fail("Expected type annotation");
        // Type identity and unsupported scalar widths are checked in SPEC-010/SPEC-013.
        text += current_token.literal;
        advance_token();
        while (accept(GLOIN_TOKEN_DOT)) {
            require_extended("Qualified types");
            text += "." + parse_name()->value;
        }
        if (accept(GLOIN_TOKEN_LT)) {
            require_extended("Generic types");
            text += "<";
            bool first = true;
            do {
                if (!first)
                    text += ", ";
                text += parse_type()->value;
                first = false;
                if (!accept(GLOIN_TOKEN_COMMA) || current_token.type == GLOIN_TOKEN_GT ||
                    current_token.type == GLOIN_TOKEN_SHR)
                    break;
            } while (true);
            consume_type_close();
            text += ">";
        }
    }
    return std::make_unique<Identifier>(text);
}

std::unique_ptr<VariableDeclaration> GloinParser::parse_variable_declaration_impl() {
    bool mut = accept(GLOIN_TOKEN_MUT);
    auto name = parse_name();
    expect(GLOIN_TOKEN_COLON, "Expected ':' and explicit binding type");
    auto type = parse_type();
    std::unique_ptr<Expression> initializer;
    if (accept(GLOIN_TOKEN_ASSIGN))
        initializer = parse_expression(0);
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after variable declaration");
    return std::make_unique<VariableDeclaration>(mut, std::move(name), std::move(type),
                                                 std::move(initializer));
}

std::unique_ptr<FunctionDefinition> GloinParser::parse_function_definition_impl(bool spawnable,
                                                                                bool deferred) {
    auto name = parse_name();
    auto generics = parse_generic_params();
    expect(GLOIN_TOKEN_LPAREN, "Expected '(' after function name");
    std::vector<Parameter> params;
    while (current_token.type != GLOIN_TOKEN_RPAREN) {
        auto param_name = parse_name(in_method);
        expect(GLOIN_TOKEN_COLON, "Expected ':' and explicit parameter type");
        auto type = parse_type();
        params.emplace_back(std::move(param_name), std::move(type));
        if (!accept(GLOIN_TOKEN_COMMA))
            break;
    }
    expect(GLOIN_TOKEN_RPAREN, "Expected ',' or ')' after parameter");
    expect(GLOIN_TOKEN_ARROW, "Expected '->' and explicit return type");
    auto result = parse_type();
    auto body = parse_block_statement();
    return std::make_unique<FunctionDefinition>(std::move(name), std::move(params),
                                                std::move(result), std::move(body), spawnable,
                                                deferred, std::move(generics));
}

std::unique_ptr<ReturnStatement> GloinParser::parse_return_statement_impl() {
    expect(GLOIN_TOKEN_RETURN, "Expected 'return'");
    std::unique_ptr<Expression> value;
    if (current_token.type != GLOIN_TOKEN_SEMICOLON)
        value = parse_expression(0);
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after return");
    return std::make_unique<ReturnStatement>(std::move(value));
}

std::unique_ptr<BlockStatement> GloinParser::parse_block_statement_impl() {
    expect(GLOIN_TOKEN_LBRACE, "Expected '{' to start block");
    Restore depth(block_depth, block_depth + 1);
    auto block = std::make_unique<BlockStatement>();
    while (current_token.type != GLOIN_TOKEN_RBRACE && !at_end())
        block->statements.push_back(parse_statement());
    expect(GLOIN_TOKEN_RBRACE, "Expected closing brace");
    return block;
}

std::unique_ptr<IfStatement> GloinParser::parse_if_statement_impl() {
    expect(GLOIN_TOKEN_IF, "Expected 'if'");
    auto condition = parse_condition();
    auto consequence = parse_block_statement();
    std::unique_ptr<Statement> alternative;
    if (accept(GLOIN_TOKEN_ELSE)) {
        if (current_token.type == GLOIN_TOKEN_IF)
            alternative = parse_if_statement();
        else
            alternative = parse_block_statement();
    }
    return std::make_unique<IfStatement>(std::move(condition), std::move(consequence),
                                         std::move(alternative));
}

std::unique_ptr<WhileStatement> GloinParser::parse_while_statement_impl() {
    expect(GLOIN_TOKEN_WHILE, "Expected 'while'");
    auto condition = parse_condition();
    auto body = parse_block_statement();
    return std::make_unique<WhileStatement>(std::move(condition), std::move(body));
}

std::unique_ptr<UnlessStatement> GloinParser::parse_unless_statement_impl() {
    expect(GLOIN_TOKEN_UNLESS, "Expected 'unless'");
    auto condition = parse_condition();
    auto body = parse_block_statement();
    return std::make_unique<UnlessStatement>(std::move(condition), std::move(body));
}

std::unique_ptr<DeferStatement> GloinParser::parse_defer_statement_impl() {
    require_extended("Defer statements");
    expect(GLOIN_TOKEN_DEFER, "Expected 'defer'");
    auto call = parse_expression(0);
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after defer");
    return std::make_unique<DeferStatement>(std::move(call));
}

std::unique_ptr<ExpressionStatement> GloinParser::parse_expression_statement_impl() {
    auto expression = parse_assignment();
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after expression");
    return std::make_unique<ExpressionStatement>(std::move(expression));
}

std::unique_ptr<Statement> GloinParser::parse_struct_definition_impl(bool packed) {
    require_extended("Struct declarations");
    expect(GLOIN_TOKEN_STRUCT, "Expected 'struct'");
    std::unique_ptr<Identifier> backing;
    if (packed && accept(GLOIN_TOKEN_LPAREN)) {
        backing = parse_type();
        expect(GLOIN_TOKEN_RPAREN, "Expected ')' after backing type");
    }
    auto name = parse_name();
    auto generics = parse_generic_params();
    expect(GLOIN_TOKEN_LBRACE, "Expected '{' after struct name");
    std::vector<StructField> fields;
    std::vector<std::unique_ptr<FunctionDefinition>> methods;
    while (current_token.type != GLOIN_TOKEN_RBRACE) {
        expect(GLOIN_TOKEN_DEF, "Expected 'def' in struct body");
        bool pub = current_token.type == GLOIN_TOKEN_PUB;
        if (pub || current_token.type == GLOIN_TOKEN_PRIV)
            advance_token();
        bool mut = accept(GLOIN_TOKEN_MUT);
        bool stat = accept(GLOIN_TOKEN_STATIC);
        bool deferred = accept(GLOIN_TOKEN_DEFERRED);
        bool spawnable = accept(GLOIN_TOKEN_SPAWNABLE);
        if (int(mut) + int(stat) + int(deferred) + int(spawnable) > 1)
            fail("Unsupported member modifier combination");
        if (current_token.type == GLOIN_TOKEN_IDENTIFIER &&
            (next_token.type == GLOIN_TOKEN_LPAREN || next_token.type == GLOIN_TOKEN_LT)) {
            if (mut)
                fail("'mut' is only allowed on fields");
            Restore method(in_method, true);
            auto function = parse_function_definition(spawnable, deferred);
            function->is_public = pub;
            function->is_static = stat;
            methods.push_back(std::move(function));
        } else {
            if (stat || deferred || spawnable)
                fail("Expected method after modifier");
            auto field = parse_name();
            expect(GLOIN_TOKEN_COLON, "Expected ':' and explicit field type");
            auto type = parse_type();
            int offset = -1;
            if (accept(GLOIN_TOKEN_KEYWORD_AT)) {
                if (!packed)
                    fail("Field offsets require a packed struct");
                if (current_token.type != GLOIN_TOKEN_NUMBER)
                    fail("Expected field offset number");
                offset = std::stoi(std::string(current_token.literal));
                advance_token();
            }
            fields.emplace_back(pub, std::move(field), std::move(type), offset);
            fields.back().is_mutable = mut;
            if (!accept(GLOIN_TOKEN_COMMA) && current_token.type != GLOIN_TOKEN_RBRACE)
                fail("Expected ',' after struct field");
        }
    }
    expect(GLOIN_TOKEN_RBRACE, "Expected closing struct brace");
    return std::make_unique<StructDefinition>(std::move(name), std::move(fields),
                                              std::move(methods), packed, std::move(backing),
                                              std::move(generics));
}

std::unique_ptr<ImportStatement> GloinParser::parse_import_statement_impl() {
    require_extended("Imports");
    expect(GLOIN_TOKEN_IMPORT, "Expected 'import'");
    if (current_token.type != GLOIN_TOKEN_STRING_LITERAL)
        fail("Expected string literal after import");
    std::string path(current_token.literal);
    advance_token();
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after import");
    return std::make_unique<ImportStatement>(path);
}

std::unique_ptr<ForStatement> GloinParser::parse_for_statement_impl() {
    expect(GLOIN_TOKEN_FOR, "Expected 'for'");
    std::unique_ptr<Statement> init;
    if (!accept(GLOIN_TOKEN_SEMICOLON)) {
        Restore header_scope(block_depth, block_depth + 1);
        if (current_token.type == GLOIN_TOKEN_DEF) {
            init = parse_def_statement();
            if (!dynamic_cast<VariableDeclaration *>(init.get()))
                fail("Expected loop binding");
        } else {
            init = parse_expression_statement();
        }
    }
    std::unique_ptr<Expression> condition;
    if (current_token.type != GLOIN_TOKEN_SEMICOLON)
        condition = parse_condition();
    expect(GLOIN_TOKEN_SEMICOLON, "Expected semicolon after for condition");
    std::unique_ptr<Expression> update;
    if (current_token.type != GLOIN_TOKEN_LBRACE) {
        Restore header_context(allow_struct_literal, false);
        update = parse_assignment();
    }
    auto body = parse_block_statement();
    return std::make_unique<ForStatement>(std::move(init), std::move(condition), std::move(update),
                                          std::move(body));
}

void GloinParser::parse() { (void)parse_checked_program(); }

std::vector<std::unique_ptr<Statement>> GloinParser::parse_program() {
    std::vector<std::unique_ptr<Statement>> program;
    while (!has_error() && !at_end()) {
        auto start = current_token.span;
        if (mode == ParseMode::Core && current_token.type != GLOIN_TOKEN_DEF) {
            diagnostics()->error(DiagnosticStage::Parsing, start,
                                 "Expected top-level function or constant");
            return {};
        }
        auto statement = parse_statement();
        if (!statement || has_error())
            return {};
        if (mode == ParseMode::Core) {
            auto *variable = dynamic_cast<VariableDeclaration *>(statement.get());
            if (!dynamic_cast<FunctionDefinition *>(statement.get()) &&
                !(variable && variable->is_const)) {
                diagnostics()->error(DiagnosticStage::Parsing, start,
                                     "Only functions and constants are allowed at file scope");
                return {};
            }
        }
        program.push_back(std::move(statement));
    }
    if (has_error())
        return {};
    return program;
}

ParseResult GloinParser::parse_checked_program() {
    auto program = parse_program();
    return {std::move(program), !has_error()};
}

[[noreturn]] void GloinParser::fail(const std::string &message) {
    diagnostics()->error(DiagnosticStage::Parsing, current_token.span, message);
    throw ParseFailure{};
}
// Public node entries convert an internal parse failure to a null result and stamp spans.
std::unique_ptr<Expression> GloinParser::parse_expression(int min_binding_power) {
    return parse_node<Expression>(current_token.span,
                                  [&] { return parse_expression_impl(min_binding_power); });
}

std::unique_ptr<Expression> GloinParser::parse_prefix() {
    return parse_node<Expression>(current_token.span, [&] { return parse_prefix_impl(); });
}

std::unique_ptr<Expression> GloinParser::parse_infix(std::unique_ptr<Expression> left) {
    return parse_node<Expression>(left ? left->span : current_token.span,
                                  [&] { return parse_infix_impl(std::move(left)); });
}

std::unique_ptr<Statement> GloinParser::parse_def_statement() {
    return parse_node<Statement>(current_token.span, [&] { return parse_def_statement_impl(); });
}

std::unique_ptr<Statement> GloinParser::parse_statement() {
    return parse_node<Statement>(current_token.span, [&] { return parse_statement_impl(); });
}

std::unique_ptr<Identifier> GloinParser::parse_type() {
    return parse_node<Identifier>(current_token.span, [&] { return parse_type_impl(); });
}

std::unique_ptr<VariableDeclaration> GloinParser::parse_variable_declaration() {
    return parse_node<VariableDeclaration>(current_token.span,
                                           [&] { return parse_variable_declaration_impl(); });
}

std::unique_ptr<FunctionDefinition> GloinParser::parse_function_definition(bool is_spawnable,
                                                                           bool is_deferred) {
    return parse_node<FunctionDefinition>(current_token.span, [&] {
        return parse_function_definition_impl(is_spawnable, is_deferred);
    });
}

std::unique_ptr<ReturnStatement> GloinParser::parse_return_statement() {
    return parse_node<ReturnStatement>(current_token.span,
                                       [&] { return parse_return_statement_impl(); });
}

std::unique_ptr<BlockStatement> GloinParser::parse_block_statement() {
    return parse_node<BlockStatement>(current_token.span,
                                      [&] { return parse_block_statement_impl(); });
}

std::unique_ptr<IfStatement> GloinParser::parse_if_statement() {
    return parse_node<IfStatement>(current_token.span, [&] { return parse_if_statement_impl(); });
}

std::unique_ptr<WhileStatement> GloinParser::parse_while_statement() {
    return parse_node<WhileStatement>(current_token.span,
                                      [&] { return parse_while_statement_impl(); });
}

std::unique_ptr<DeferStatement> GloinParser::parse_defer_statement() {
    return parse_node<DeferStatement>(current_token.span,
                                      [&] { return parse_defer_statement_impl(); });
}

std::unique_ptr<ExpressionStatement> GloinParser::parse_expression_statement() {
    return parse_node<ExpressionStatement>(current_token.span,
                                           [&] { return parse_expression_statement_impl(); });
}

std::unique_ptr<Statement> GloinParser::parse_struct_definition(bool is_packed) {
    return parse_node<Statement>(current_token.span,
                                 [&] { return parse_struct_definition_impl(is_packed); });
}

std::unique_ptr<ImportStatement> GloinParser::parse_import_statement() {
    return parse_node<ImportStatement>(current_token.span,
                                       [&] { return parse_import_statement_impl(); });
}

std::unique_ptr<UnlessStatement> GloinParser::parse_unless_statement() {
    return parse_node<UnlessStatement>(current_token.span,
                                       [&] { return parse_unless_statement_impl(); });
}

std::unique_ptr<ForStatement> GloinParser::parse_for_statement() {
    return parse_node<ForStatement>(current_token.span, [&] { return parse_for_statement_impl(); });
}
