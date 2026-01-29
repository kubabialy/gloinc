#include "parser.h"

#include <iostream>
GloinParser::GloinParser(Lexer l) : lexer(l) {
    // Fill the 'next_token' slot
    next_token = lexer.next_token();
    // Move 'next' to 'current', and refill 'next'
    advance_token();
}

void GloinParser::advance_token() {
    current_token = next_token;
    next_token = lexer.next_token();

    // Skip both comments and newlines
    while (current_token.type == GLOIN_TOKEN_NEWLINE || current_token.type == GLOIN_TOKEN_COMMENT) {
        current_token = next_token;
        next_token = lexer.next_token();
    }
}

/**
 * `get_binding_power` maps a token (operator/punctuator) to a numeric binding power—basically “how
 * tightly does this operator stick to its operands”.
 *
 * Higher number ⇒ higher precedence (binds tighter) ⇒ gets grouped earlier in an expression.
 *
 * This kind of table is typically used by a Pratt
 * parser / precedence-climbing parser to decide whether to keep parsing an infix/postfix operator
 * or to stop and return control to the caller. How to read the table The returned numbers define
 * relative precedence:
 *
 * || → 10 (loosest among the listed ones)
 * && → 20
 * == / != → 30
 * < / > → 40
 * + / - → 50
 * * / / → 60
 * ( → 70 (treated as “call operator” after an expression: f(...))
 * . and [ → 80 (very tight: member access a.b and indexing a[i])
 *
 * default: return 0; means “this token doesn’t act like a (supported) operator in expressions”, so
 * it has the lowest binding power and won’t continue an infix/postfix parse.
 *
 * Why (, ., [ are in the precedence table?
 *
 * Those are not binary operators like +, but in many expression grammars they
 * behave like postfix operators that apply to the expression on their left:
 * f(…) is a function call on f
 * a.b is member access on a
 * a[…] is indexing on a
 *
 * @param type GloinTokenType
 * @return int
 */
int GloinParser::get_binding_power(const GloinTokenType type) {
    switch (type) {
    case GLOIN_TOKEN_OR:
        return 10;
    case GLOIN_TOKEN_AND:
        return 20;
    case GLOIN_TOKEN_EQ:
        return 30;
    case GLOIN_TOKEN_NE:
        return 30;
    case GLOIN_TOKEN_LT:
        return 40;
    case GLOIN_TOKEN_GT:
        return 40;
    case GLOIN_TOKEN_PLUS:
        return 50;
    case GLOIN_TOKEN_MINUS:
        return 50;
    case GLOIN_TOKEN_MULTIPLY:
        return 60;
    case GLOIN_TOKEN_DIVIDE:
        return 60;
    case GLOIN_TOKEN_LPAREN:
        return 70; // Function Call
    case GLOIN_TOKEN_DOT:
        return 80; // Member access
    case GLOIN_TOKEN_LBRACKET:
        return 80; // Index access
    case GLOIN_TOKEN_ASSIGN:
        return 5; // LOWEST precedence (right associative usually)
    default:
        return 0; // LOWEST
    }
}

/**
 * Parses an expression using Pratt Parsing (Top-Down Operator Precedence).
 * 
 * @param min_binding_power The minimum binding power required to continue parsing the expression.
 * @return A unique_ptr to the parsed Expression AST node.
 */
std::unique_ptr<Expression> GloinParser::parse_expression(int min_binding_power) {
    auto left = parse_prefix();

    while (next_token.type != GLOIN_TOKEN_EOF &&
           get_binding_power(next_token.type) > min_binding_power) {
        // Move the operator into 'current_token'
        advance_token();

        // This handles: binary ops (+ *), calls (foo()), indexing (foo[])
        left = parse_infix(std::move(left));
    }

    return left;
}

/**
 * Parses a prefix expression (e.g., identifiers, literals, unary operators like -x or !x).
 * This is the starting point for any expression.
 * 
 * @return A unique_ptr to the parsed Expression AST node.
 */
std::unique_ptr<Expression> GloinParser::parse_prefix() {
    // Look at 'current_token' (it was advanced before calling this)
    switch (current_token.type) {
    case GLOIN_TOKEN_NUMBER: {
        int64_t val = 0;
        try {
            val = std::stoll(std::string(current_token.literal));
        } catch (...) {
            // Fallback or error handling
        }
        return std::make_unique<IntegerLiteral>(val, std::string(current_token.literal));
    }
    case GLOIN_TOKEN_FLOAT: {
        double val = 0.0;
        try {
            val = std::stod(std::string(current_token.literal));
        } catch (...) {
            // Fallback
        }
        return std::make_unique<FloatLiteral>(val, std::string(current_token.literal));
    }
    case GLOIN_TOKEN_TRUE:
        return std::make_unique<BooleanLiteral>(true);
    case GLOIN_TOKEN_FALSE:
        return std::make_unique<BooleanLiteral>(false);
    case GLOIN_TOKEN_STRING:
        return std::make_unique<StringLiteral>(std::string(current_token.literal));
    case GLOIN_TOKEN_IDENTIFIER:
        return std::make_unique<Identifier>(std::string(current_token.literal));

    case GLOIN_TOKEN_MINUS:
    case GLOIN_TOKEN_NOT: {
        // Unary Operator
        // Recurse with HIGH precedence (PREFIX usually binds very tight)
        std::string op(current_token.literal);
        int prefix_bp = 90; // PREFIX binding power
        advance_token();
        auto right = parse_expression(prefix_bp);
        return std::make_unique<PrefixExpression>(op, std::move(right));
    }

    case GLOIN_TOKEN_LPAREN: {
        // Grouping: ( expression )
        advance_token();
        auto expr = parse_expression(0);
        if (next_token.type == GLOIN_TOKEN_RPAREN) {
            advance_token(); // Eat ')'
        } else {
            // Ideally report error here
             std::cerr << "Expected ')' after expression\n";
        }
        return expr;
    }

    default:
        std::cerr << "Unexpected token in prefix position: " << current_token.literal << "\n";
        return nullptr;
    }
}

/**
 * Parses an infix expression (e.g., binary operations, function calls, member access).
 * It takes the left-hand side of the expression as input and builds a larger tree.
 * 
 * @param left The left-hand side expression that has already been parsed.
 * @return A unique_ptr to the new Expression node containing the infix operation.
 */
std::unique_ptr<Expression> GloinParser::parse_infix(std::unique_ptr<Expression> left) {
    GloinTokenType op_type = current_token.type;
    int precedence = get_binding_power(op_type);

    switch (op_type) {
    case GLOIN_TOKEN_PLUS:
    case GLOIN_TOKEN_MINUS:
    case GLOIN_TOKEN_MULTIPLY:
    case GLOIN_TOKEN_DIVIDE:
    case GLOIN_TOKEN_EQ:
    case GLOIN_TOKEN_NE:
    case GLOIN_TOKEN_LT:
    case GLOIN_TOKEN_GT:
    case GLOIN_TOKEN_AND:
    case GLOIN_TOKEN_OR: {
        // Binary Operator
        std::string op_str(current_token.literal);
        advance_token();
        // Recurse!
        auto right = parse_expression(precedence);
        return std::make_unique<InfixExpression>(std::move(left), op_str, std::move(right));
    }

    case GLOIN_TOKEN_LPAREN: {
        // Function Call: ident ( args )
        // 'left' is the identifier (the function name)
        // Note: We do NOT advance_token() here because parse_expression_list 
        // expects 'next_token' to be the start of the first argument.
        // current_token is '('.
        
        // We need to parse arguments until ')'
        std::vector<std::unique_ptr<Expression>> args;
        if (next_token.type != GLOIN_TOKEN_RPAREN) {
             args = parse_expression_list(GLOIN_TOKEN_RPAREN);
        }
        
        if (next_token.type == GLOIN_TOKEN_RPAREN) {
            advance_token(); // Eat ')'
        }
        
        return std::make_unique<CallExpression>(std::move(left), std::move(args));
    }
    
    case GLOIN_TOKEN_LBRACKET: {
        // Indexing: left [ index ]
        // current_token is '['
        advance_token(); // Eat '['
        auto index = parse_expression(0);
        
        if (next_token.type == GLOIN_TOKEN_RBRACKET) {
            advance_token(); // Eat ']'
        } else {
            std::cerr << "Expected ']' after index\n";
        }
        
        return std::make_unique<IndexExpression>(std::move(left), std::move(index));
    }

    case GLOIN_TOKEN_DOT: {
        // Member Access: left . member
        // current_token is '.'
        advance_token(); // Eat '.', current_token is now the Identifier
        
        if (current_token.type == GLOIN_TOKEN_IDENTIFIER) {
             auto member = std::make_unique<Identifier>(std::string(current_token.literal));
             return std::make_unique<MemberAccessExpression>(std::move(left), std::move(member));
        } else {
             std::cerr << "Expected identifier after '.'\n";
             return left; 
        }
    }

    case GLOIN_TOKEN_ASSIGN: {
        advance_token();
        auto right = parse_expression(precedence - 1);
        return std::make_unique<AssignmentExpression>(std::move(left), std::move(right));
    }

    default:
        return left;
    }
}

/**
 * Helper to parse a comma-separated list of expressions until an end token is met.
 * Used for function arguments, array literals, etc.
 * 
 * @param end_token The token that terminates the list (e.g. RPAREN)
 * @return vector of expression pointers
 */
std::vector<std::unique_ptr<Expression>> GloinParser::parse_expression_list(GloinTokenType end_token) {
    std::vector<std::unique_ptr<Expression>> list;
    
    if (next_token.type == end_token) {
        return list;
    }
    
    // Parse first expression
    advance_token(); // Move to start of expression
    list.push_back(parse_expression(0));
    
    // Parse subsequent expressions if comma exists
    while (next_token.type == GLOIN_TOKEN_COMMA) {
        advance_token(); // Eat comma
        advance_token(); // Move to next expr
        list.push_back(parse_expression(0));
    }
    
    return list;
}

/**
 * Parses a single statement.
 * Dispatches to specific statement parsers based on the current token.
 * 
 * @return A unique_ptr to the parsed Statement AST node.
 */
std::unique_ptr<Statement> GloinParser::parse_statement() {
    switch (current_token.type) {
        case GLOIN_TOKEN_DEF:
            return parse_variable_declaration();
        case GLOIN_TOKEN_RETURN:
            return parse_return_statement();
        case GLOIN_TOKEN_IF:
            return parse_if_statement();
        case GLOIN_TOKEN_WHILE:
            return parse_while_statement();
        case GLOIN_TOKEN_DEFER:
            return parse_defer_statement();
        case GLOIN_TOKEN_LBRACE:
            return parse_block_statement();
        default:
            return parse_expression_statement();
    }
}

/**
 * Parses a variable declaration.
 * Syntax: def [mut] name: type = initializer;
 * 
 * @return A unique_ptr to the VariableDeclaration AST node.
 */
std::unique_ptr<VariableDeclaration> GloinParser::parse_variable_declaration() {
    // Current token is 'def'
    advance_token();
    
    bool is_mutable = false;
    if (current_token.type == GLOIN_TOKEN_MUT) {
        is_mutable = true;
        advance_token();
    }
    
    if (current_token.type != GLOIN_TOKEN_IDENTIFIER) {
        std::cerr << "Expected identifier in variable declaration\n";
        return nullptr;
    }
    auto name = std::make_unique<Identifier>(std::string(current_token.literal));
    advance_token();
    
    std::unique_ptr<Identifier> type = nullptr;
    if (current_token.type == GLOIN_TOKEN_COLON) {
        advance_token();
        // Parse type (simplified: just an identifier or basic type token for now)
        // In reality, types can be complex (pointers, arrays, etc.)
        // For now let's assume simple type identifier
        if (current_token.type == GLOIN_TOKEN_IDENTIFIER || 
            (current_token.type >= GLOIN_TOKEN_BOOL && current_token.type <= GLOIN_TOKEN_LE_U128) ||
            current_token.type == GLOIN_TOKEN_STRING || current_token.type == GLOIN_TOKEN_VOID) {
             type = std::make_unique<Identifier>(std::string(current_token.literal));
             advance_token();
        } else if (current_token.type == GLOIN_TOKEN_MULTIPLY || current_token.type == GLOIN_TOKEN_AMPERSAND) {
             // Handle pointer/ref types simply by consuming token and next identifier
             std::string type_str(current_token.literal);
             advance_token();
             if (current_token.type == GLOIN_TOKEN_IDENTIFIER || 
                (current_token.type >= GLOIN_TOKEN_BOOL && current_token.type <= GLOIN_TOKEN_LE_U128)) {
                 type_str += std::string(current_token.literal);
                 type = std::make_unique<Identifier>(type_str);
                 advance_token();
             }
        }
    }
    
    std::unique_ptr<Expression> initializer = nullptr;
    if (current_token.type == GLOIN_TOKEN_ASSIGN) {
        advance_token();
        initializer = parse_expression(0);
    }
    
    if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
        // Optional semicolon consumption if not consumed by parse_expression (it doesn't consume it)
        // But wait, parse_expression stops when precedence is low. Semicolon has 0 precedence.
        // So we should be at semicolon.
    } else {
        // If next is semicolon, eat it. 
        // Note: parse_expression consumes tokens.
        // If we are here, current_token is the last token of expression?
        // No, parse_expression loops until `next_token` is low binding power.
        // So `current_token` is the last token of expression. `next_token` is semicolon.
    }
    
    if (next_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    }
    
    return std::make_unique<VariableDeclaration>(is_mutable, std::move(name), std::move(type), std::move(initializer));
}

/**
 * Parses a return statement.
 * Syntax: return [expression];
 * 
 * @return A unique_ptr to the ReturnStatement AST node.
 */
std::unique_ptr<ReturnStatement> GloinParser::parse_return_statement() {
    // Current token is 'return'
    advance_token();
    
    std::unique_ptr<Expression> value = nullptr;
    if (current_token.type != GLOIN_TOKEN_SEMICOLON) {
        value = parse_expression(0);
    }
    
    if (next_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    }
    
    return std::make_unique<ReturnStatement>(std::move(value));
}

/**
 * Parses a block statement (a list of statements enclosed in braces).
 * Syntax: { statement* }
 * 
 * @return A unique_ptr to the BlockStatement AST node.
 */
std::unique_ptr<BlockStatement> GloinParser::parse_block_statement() {
    // Current token is '{'
    auto block = std::make_unique<BlockStatement>();
    advance_token();
    
    while (current_token.type != GLOIN_TOKEN_RBRACE && current_token.type != GLOIN_TOKEN_EOF) {
        auto stmt = parse_statement();
        if (stmt) {
            block->statements.push_back(std::move(stmt));
        }
        advance_token();
    }
    
    return block;
}

/**
 * Parses an if statement.
 * Syntax: if condition { consequence } [else { alternative }]
 * 
 * @return A unique_ptr to the IfStatement AST node.
 */
std::unique_ptr<IfStatement> GloinParser::parse_if_statement() {
    // Current token is 'if'
    advance_token();
    
    auto condition = parse_expression(0);
    
    if (next_token.type != GLOIN_TOKEN_LBRACE) {
        std::cerr << "Expected '{' after if condition\n";
        return nullptr;
    }
    advance_token(); // Move to '{'
    
    auto consequence = parse_block_statement();
    
    std::unique_ptr<Statement> alternative = nullptr;
    if (next_token.type == GLOIN_TOKEN_ELSE) {
        advance_token(); // Eat '}' from block
        advance_token(); // Eat 'else'
        
        if (current_token.type == GLOIN_TOKEN_IF) {
            alternative = parse_if_statement();
        } else if (current_token.type == GLOIN_TOKEN_LBRACE) {
            alternative = parse_block_statement();
        } else {
            std::cerr << "Expected '{' or 'if' after else\n";
        }
    }
    
    return std::make_unique<IfStatement>(std::move(condition), std::move(consequence), std::move(alternative));
}

/**
 * Parses a while statement.
 * Syntax: while condition { body }
 * 
 * @return A unique_ptr to the WhileStatement AST node.
 */
std::unique_ptr<WhileStatement> GloinParser::parse_while_statement() {
    // Current token is 'while'
    advance_token();
    
    auto condition = parse_expression(0);
    
    if (next_token.type != GLOIN_TOKEN_LBRACE) {
         std::cerr << "Expected '{' after while condition\n";
         return nullptr;
    }
    advance_token(); // Move to '{'
    
    auto body = parse_block_statement();
    
    return std::make_unique<WhileStatement>(std::move(condition), std::move(body));
}

/**
 * Parses a defer statement.
 * Syntax: defer expression;
 * 
 * @return A unique_ptr to the DeferStatement AST node.
 */
std::unique_ptr<DeferStatement> GloinParser::parse_defer_statement() {
    // Current token is 'defer'
    advance_token();
    
    auto call = parse_expression(0);
    
    if (next_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    }
    
    return std::make_unique<DeferStatement>(std::move(call));
}

/**
 * Parses an expression statement.
 * Syntax: expression;
 * 
 * @return A unique_ptr to the ExpressionStatement AST node.
 */
std::unique_ptr<ExpressionStatement> GloinParser::parse_expression_statement() {
    auto expr = parse_expression(0);
    
    if (next_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    }
    
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

void GloinParser::parse() {
    // For now, this is just a driver loop to test parsing.
    // In a real compiler, this would parse top-level declarations (def, import, etc.)
    // But since the request is to focus on the Pratt Parser (expressions), we will just 
    // try to parse an expression if we see one.
    
    if (current_token.type != GLOIN_TOKEN_EOF) {
        auto expr = parse_expression(0);
        if (expr) {
            std::cout << "Parsed: " << expr->to_string() << "\n";
        }
    }
}