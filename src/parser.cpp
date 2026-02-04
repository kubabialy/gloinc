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
    case GLOIN_TOKEN_LBRACKET: {
        // Array Literal: [expr, expr, ...]
        advance_token(); // Eat '['
        std::vector<std::unique_ptr<Expression>> elements;
        
        while (current_token.type != GLOIN_TOKEN_RBRACKET && current_token.type != GLOIN_TOKEN_EOF) {
            auto elem = parse_expression(0);
            if (!elem) break;
            elements.push_back(std::move(elem));
            
            if (current_token.type == GLOIN_TOKEN_COMMA) {
                advance_token();
            } else if (current_token.type != GLOIN_TOKEN_RBRACKET) {
                 std::cerr << "Expected ',' or ']' in array literal\n";
                 // recover?
            }
        }
        
        if (current_token.type == GLOIN_TOKEN_RBRACKET) {
            advance_token();
        }
        
        return std::make_unique<ArrayLiteral>(std::move(elements));
    }
    case GLOIN_TOKEN_IDENTIFIER: {
        auto ident = std::make_unique<Identifier>(std::string(current_token.literal));
        // Check for Struct Initialization: Type { field: val, ... }
        // BUT wait, parse_infix handles function calls. 
        // Struct init looks like: Point { ... }
        // If next token is '{', it COULD be struct init.
        // But what if it's not?
        // In Gloin, identifiers are prefix.
        // If we consume Identifier, then Infix parser sees '{'?
        // No, '{' is usually not infix operator unless we define it.
        
        // Actually, let's handle it here if possible or in parse_infix?
        // If we handle here, we need to peek.
        if (next_token.type == GLOIN_TOKEN_LBRACE) {
             // Struct Literal! 
             // Consume identifier (already done, stored in ident)
             // But parse_prefix returns just the identifier.
             // We can return the Identifier, and let parse_infix handle the '{'?
             // But '{' is not a standard infix op. 
             // So we must handle it here or add '{' to parse_infix.
             
             // Let's consume the '{' here and parse fields.
             // Wait, this modifies the parse_prefix flow significantly if we consume more tokens.
             // Let's try to handle it.
             advance_token(); // Eat '{' (current becomes '{')
             advance_token(); // Move to first field name or '}'
             
             std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
             while (current_token.type != GLOIN_TOKEN_RBRACE && current_token.type != GLOIN_TOKEN_EOF) {
                 if (current_token.type != GLOIN_TOKEN_IDENTIFIER) {
                     // Error or maybe Empty? 
                     break; 
                 }
                 std::string fieldName = std::string(current_token.literal);
                 advance_token(); // Eat field name
                 
                 if (current_token.type != GLOIN_TOKEN_COLON) {
                      std::cerr << "Expected ':' in struct init\n";
                 }
                 advance_token(); // Eat ':'
                 
                 auto val = parse_expression(0);
                 fields.push_back({fieldName, std::move(val)});
                 
                 if (next_token.type == GLOIN_TOKEN_COMMA) {
                     advance_token(); // Eat last token of expr
                     advance_token(); // Eat ','
                 } else if (next_token.type == GLOIN_TOKEN_RBRACE) {
                     advance_token(); // Eat last token
                     // Loop will terminate
                 }
             }
             
             if (current_token.type == GLOIN_TOKEN_RBRACE) {
                 // Good
             } else if (next_token.type == GLOIN_TOKEN_RBRACE) {
                 advance_token();
             }
             
             // Helper to create StructLiteral node (need to define it in AST first if not exists)
             // For now, let's assume we can treat it as a special Call or similar, 
             // OR we need to add StructLiteral to AST.
             // Checking AST.h...
             return std::make_unique<StructLiteral>(std::move(ident), std::move(fields));
        }
        return ident;
    }

    case GLOIN_TOKEN_SPAWN: // Fallthrough
    case GLOIN_TOKEN_RUN: {
        // run expression (usually a function call)
        advance_token();
        // Parse the expression with lower precedence to allow function calls (precedence 70)
        // Using 60 (Multiplication level) ensures spawn binds tighter than + (50) but looser than call (70)
        auto right = parse_expression(60); 
        return std::make_unique<SpawnExpression>(std::move(right));
    }
    case GLOIN_TOKEN_AWAIT: {
        advance_token();
        // Await has similar precedence to spawn, or maybe tighter?
        // await spawn x -> await (spawn x)
        // spawn await x -> spawn (await x)
        // Let's stick with 60 for now.
        auto right = parse_expression(60);
        return std::make_unique<AwaitExpression>(std::move(right));
    }

    case GLOIN_TOKEN_MINUS:
    case GLOIN_TOKEN_NOT:
    case GLOIN_TOKEN_AMPERSAND:
    case GLOIN_TOKEN_MULTIPLY: {
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
/**
 * Parses a statement that starts with 'def'.
 * Can be a VariableDeclaration or a FunctionDefinition.
 */
std::unique_ptr<Statement> GloinParser::parse_def_statement() {
    // We are at 'def'. Peek ahead to distinguish.
    // def name : type ... -> Variable
    // def mut name : ... -> Variable
    // def spawnable/deferred name( ... -> Function
    // def name( ... -> Function

    // Note: advance_token() overwrites current_token with next_token.
    // We can't peek 2 tokens ahead easily with just next_token.
    // However, VariableDecl starts with 'mut' or 'name :'.
    // Function starts with 'spawnable', 'deferred' or 'name('.
    
    // Let's consume 'def'.
    advance_token();
    
    bool is_spawnable = false;
    bool is_deferred = false;
    bool is_variable = false;
    
    // Check keywords
    if (current_token.type == GLOIN_TOKEN_STRUCT || current_token.type == GLOIN_TOKEN_PACKED) {
         // def struct ... or def packed struct ...
         // If packed, consume it
         bool is_packed = false;
         if (current_token.type == GLOIN_TOKEN_PACKED) {
             is_packed = true;
             advance_token();
             if (current_token.type != GLOIN_TOKEN_STRUCT) {
                  std::cerr << "Expected 'struct' after 'packed'\n";
                  return nullptr;
             }
         }
         // current_token is STRUCT
         return parse_struct_definition(is_packed);
    }

    if (current_token.type == GLOIN_TOKEN_SPAWNABLE) {
        is_spawnable = true;
        advance_token();
    } else if (current_token.type == GLOIN_TOKEN_DEFERRED) { // Assuming DEFERRED token exists or will be added
        is_deferred = true;
        advance_token();
    } else if (current_token.type == GLOIN_TOKEN_MUT) {
        is_variable = true;
        // Don't advance here, parse_variable_declaration expects to handle mut (or we need to adjust)
        // Actually parse_variable_declaration expects 'def' to be consumed.
        // But it checks for 'mut'.
        // If we consumed 'def', current_token is 'mut'.
    }
    
    if (is_variable) {
        // We need to implement a variant of parse_variable_declaration that takes already parsed flags?
        // Or we can just reuse parse_variable_declaration but we already consumed 'def'.
        // My previous parse_variable_declaration consumed 'def' at start.
        // But here we consumed 'def'.
        // And we might be at 'mut'.
        // Let's refactor parse_variable_declaration to NOT consume 'def' first?
        // Or handle it here.
        
        // Let's simplify: parse_variable_declaration expects current_token to be the one AFTER 'def'.
        // Wait, look at existing code:
        // parse_variable_declaration() { advance_token(); ... }
        // It eats 'def'.
        
        // So we can't easily dispatch inside parse_statement without peeking.
        // But we only have 1 peek.
        
        // If we are here, we called parse_def_statement instead of parse_variable_declaration from parse_statement.
        // So 'def' is consumed. current_token is next thing.
    }
    
    // If we saw 'spawnable' or 'deferred', it MUST be a function (variables aren't spawnable).
    if (is_spawnable || is_deferred) {
        return parse_function_definition(is_spawnable, is_deferred);
    }
    
    // Now we are at 'name' or 'mut'.
    if (current_token.type == GLOIN_TOKEN_MUT) {
        // Must be variable
        return parse_variable_declaration(); // But we need to handle that we already ate 'def'.
    }
    
    // At 'name'. Next token tells us.
    // name : -> Variable
    // name ( -> Function
    
    if (current_token.type == GLOIN_TOKEN_IDENTIFIER) {
        if (next_token.type == GLOIN_TOKEN_LPAREN) {
            return parse_function_definition(false, false);
        } else {
            return parse_variable_declaration();
        }
    }
    
    std::cerr << "Unexpected token after def\n";
    return nullptr;
}

std::unique_ptr<Statement> GloinParser::parse_statement() {
    switch (current_token.type) {
        case GLOIN_TOKEN_DEF:
            return parse_def_statement(); // Changed from parse_variable_declaration
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
std::unique_ptr<Identifier> GloinParser::parse_type() {
    std::string type_str;
    
    // Handle pointers and references (*, **, &, &&, etc.)
    while (current_token.type == GLOIN_TOKEN_MULTIPLY || current_token.type == GLOIN_TOKEN_AMPERSAND) {
        type_str += std::string(current_token.literal);
        advance_token();
    }
    
    // Base type
    if (current_token.type == GLOIN_TOKEN_IDENTIFIER || 
        (current_token.type >= GLOIN_TOKEN_BOOL && current_token.type <= GLOIN_TOKEN_LE_U128) ||
        current_token.type == GLOIN_TOKEN_STRING || 
        current_token.type == GLOIN_TOKEN_VOID ||
        current_token.type == GLOIN_TOKEN_CUSTOM_WIDTH_INT ||
        current_token.type == GLOIN_TOKEN_BIT ||
        current_token.type == GLOIN_TOKEN_DEFERRED) { // Added DEFERRED
        
        type_str += std::string(current_token.literal);
        advance_token();
        
        // Handle Generics <T, U, ...>
        if (current_token.type == GLOIN_TOKEN_LT) {
            type_str += "<";
            advance_token();
            
            while (true) {
                auto sub_type = parse_type();
                if (!sub_type) return nullptr;
                type_str += sub_type->value;
                
                if (current_token.type == GLOIN_TOKEN_COMMA) {
                    type_str += ", "; // Normalize space?
                    advance_token();
                } else if (current_token.type == GLOIN_TOKEN_GT) {
                    type_str += ">";
                    advance_token();
                    break;
                } else {
                    std::cerr << "Expected ',' or '>' in generic type\n";
                    return nullptr;
                }
            }
        }
    } 
    else if (current_token.type == GLOIN_TOKEN_LBRACKET) {
        // Array Type: [Type; N]
        advance_token();
        auto sub_type = parse_type();
        if (!sub_type) return nullptr;
        
        if (current_token.type != GLOIN_TOKEN_SEMICOLON) {
             std::cerr << "Expected ';' in array type\n";
             return nullptr;
        }
        advance_token();
        
        if (current_token.type != GLOIN_TOKEN_NUMBER) {
             std::cerr << "Expected array size number\n";
             return nullptr;
        }
        std::string size = std::string(current_token.literal);
        advance_token();
        
        if (current_token.type != GLOIN_TOKEN_RBRACKET) {
             std::cerr << "Expected ']' after array size\n";
             return nullptr;
        }
        advance_token();
        
        type_str = "[" + sub_type->value + "; " + size + "]";
    }
    else {
        std::cerr << "Expected type identifier, got: " << current_token.literal << "\n";
        return nullptr;
    }
    
    return std::make_unique<Identifier>(type_str);
}

std::unique_ptr<VariableDeclaration> GloinParser::parse_variable_declaration() {
    // Note: 'def' is already consumed by parse_def_statement. 
    // current_token is what followed 'def' (or followed 'spawnable' etc which shouldn't happen for vars).
    
    // But wait, parse_def_statement logic:
    // If 'mut', it calls this. current_token is 'mut'.
    // If 'name', it calls this. current_token is 'name'.
    
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
        type = parse_type();
    }
    
    std::unique_ptr<Expression> initializer = nullptr;
    if (current_token.type == GLOIN_TOKEN_ASSIGN) {
        advance_token();
        initializer = parse_expression(0);
    }
    
    if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    } else {
        // parse_expression was called
        advance_token();
        if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
            advance_token();
        }
    }
    
    return std::make_unique<VariableDeclaration>(is_mutable, std::move(name), std::move(type), std::move(initializer));
}

std::unique_ptr<FunctionDefinition> GloinParser::parse_function_definition(bool is_spawnable, bool is_deferred) {
    // std::cerr << "DEBUG: parsing function definition " << current_token.literal << "\n";
    auto name = std::make_unique<Identifier>(std::string(current_token.literal));
    advance_token(); // Eat name
    
    if (current_token.type != GLOIN_TOKEN_LPAREN) {
        std::cerr << "Expected '(' in function definition, got " << current_token.literal << "\n";
        return nullptr;
    }
    advance_token(); // Eat '('
    
    std::vector<Parameter> params;
    while (current_token.type != GLOIN_TOKEN_RPAREN && current_token.type != GLOIN_TOKEN_EOF) {
        // ... (existing param logic)
        if (current_token.type == GLOIN_TOKEN_SELF) {
             auto param_name = std::make_unique<Identifier>("self");
             advance_token();
             
             std::unique_ptr<Identifier> param_type = nullptr;
             if (current_token.type == GLOIN_TOKEN_COLON) {
                 advance_token();
                 param_type = parse_type();
             } else {
                 param_type = std::make_unique<Identifier>("*Self");
             }
             
             params.emplace_back(std::move(param_name), std::move(param_type));
             
        } else if (current_token.type == GLOIN_TOKEN_IDENTIFIER) {
            auto param_name = std::make_unique<Identifier>(std::string(current_token.literal));
            advance_token();
            if (current_token.type != GLOIN_TOKEN_COLON) {
                 std::cerr << "Expected ':' in parameter\n";
            }
            advance_token(); // Eat ':'
            
            auto param_type = parse_type();
            params.emplace_back(std::move(param_name), std::move(param_type));
        } else if (current_token.type == GLOIN_TOKEN_COMMA) {
            advance_token();
        } else {
             std::cerr << "Unexpected token in parameter list: " << current_token.literal << "\n";
             advance_token();
        }
    }
    // std::cerr << "DEBUG: ended params at " << current_token.literal << "\n";
    advance_token(); // Eat ')'
    // std::cerr << "DEBUG: after ')' token is " << current_token.literal << " type=" << current_token.type << "\n";
    
    std::unique_ptr<Identifier> return_type = nullptr;
    if (current_token.type == GLOIN_TOKEN_ARROW) {
        advance_token(); // Eat '->'
        return_type = parse_type();
    } else {
        return_type = std::make_unique<Identifier>("void");
    }
    
    auto body = parse_block_statement();
    
    auto func_def = std::make_unique<FunctionDefinition>(std::move(name), std::move(params), std::move(return_type), std::move(body));
    func_def->is_spawnable = is_spawnable;
    func_def->is_deferred = is_deferred;
    return func_def;
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
    if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    } else {
        value = parse_expression(0);
        advance_token();
        if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
            advance_token();
        }
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
        } else {
            advance_token();
        }
    }

    if (current_token.type == GLOIN_TOKEN_RBRACE) {
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
    if (current_token.type == GLOIN_TOKEN_ELSE) {
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
    
    if (!expr) {
        return nullptr;
    }
    
    advance_token();
    if (current_token.type == GLOIN_TOKEN_SEMICOLON) {
        advance_token();
    }
    
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

std::unique_ptr<Statement> GloinParser::parse_struct_definition(bool is_packed) {
    // Current token is 'struct'
    advance_token();
    
    std::unique_ptr<Identifier> backing_type = nullptr;
    if (is_packed && current_token.type == GLOIN_TOKEN_LPAREN) {
        advance_token(); // Eat '('
        if (current_token.type == GLOIN_TOKEN_IDENTIFIER || 
           (current_token.type >= GLOIN_TOKEN_BOOL && current_token.type <= GLOIN_TOKEN_LE_U128)) {
            backing_type = std::make_unique<Identifier>(std::string(current_token.literal));
            advance_token();
        } else {
             std::cerr << "Expected type in packed struct backing definition\n";
        }
        
        if (current_token.type == GLOIN_TOKEN_RPAREN) {
            advance_token(); // Eat ')'
        } else {
            std::cerr << "Expected ')' after packed struct backing type\n";
        }
    }
    
    if (current_token.type != GLOIN_TOKEN_IDENTIFIER) {
        std::cerr << "Expected identifier after 'struct'\n";
        return nullptr;
    }
    auto name = std::make_unique<Identifier>(std::string(current_token.literal));
    advance_token();
    
    if (current_token.type != GLOIN_TOKEN_LBRACE) {
        std::cerr << "Expected '{' after struct name\n";
        return nullptr;
    }
    advance_token(); // Eat '{'
    
    std::vector<StructField> fields;
    std::vector<std::unique_ptr<FunctionDefinition>> methods;
    
    while (current_token.type != GLOIN_TOKEN_RBRACE && current_token.type != GLOIN_TOKEN_EOF) {
        bool is_pub_method = false;
        
        // Check for 'pub' on method
        if (current_token.type == GLOIN_TOKEN_PUB) {
            is_pub_method = true;
            advance_token();
        }
        
        if (current_token.type == GLOIN_TOKEN_DEF) {
            advance_token(); // Eat 'def'
            
            if (is_pub_method) {
                // Must be method: pub def ...
                // Parse method
                auto method = parse_function_definition();
                if (method) methods.push_back(std::move(method));
            } else {
                // Could be field (def pub name, def name) or method (def name(...))
                
                bool is_pub_field = false;
                if (current_token.type == GLOIN_TOKEN_PUB) {
                    is_pub_field = true;
                    advance_token();
                }
                
                if (current_token.type != GLOIN_TOKEN_IDENTIFIER) {
                     std::cerr << "Expected identifier in struct member\n";
                     advance_token(); // recover
                     continue;
                }
                
                // Peek to distinguish field vs method
                if (next_token.type == GLOIN_TOKEN_LPAREN) {
                    // Method: def name(...)
                    // But wait, if we are here, we are not 'pub def'. Private method.
                    // But we already consumed 'def'. parse_function_definition expects 'def' consumed? 
                    // No, parse_function_definition starts at NAME.
                    // "auto name = ...; advance_token();"
                    // So if we are at NAME, parse_function_definition works.
                    
                    auto method = parse_function_definition();
                    if (method) methods.push_back(std::move(method));
                    
                } else if (next_token.type == GLOIN_TOKEN_COLON || next_token.type == GLOIN_TOKEN_COMMA || next_token.type == GLOIN_TOKEN_RBRACE || next_token.type == GLOIN_TOKEN_BIT) {
                    // Field: def name : type
                    // Or minimal: def name
                    
                    auto field_name = std::make_unique<Identifier>(std::string(current_token.literal));
                    advance_token(); // Eat name
                    
                    std::unique_ptr<Identifier> field_type = nullptr;
                    if (current_token.type == GLOIN_TOKEN_COLON) {
                        advance_token();
                        field_type = parse_type();
                    }

                    // Handling 'at' keyword if present (packed structs)
                    // If we just parsed type (e.g. u5, bit), check for 'at'
                    int offset = -1;
                    if (current_token.type == GLOIN_TOKEN_KEYWORD_AT) {
                        advance_token();
                        if (current_token.type == GLOIN_TOKEN_NUMBER) {
                             offset = std::stoi(std::string(current_token.literal));
                             advance_token();
                        } else {
                             std::cerr << "Expected offset number after 'at'\n";
                        }
                    }
                    
                    fields.emplace_back(is_pub_field, std::move(field_name), std::move(field_type), offset);
                    
                    if (current_token.type == GLOIN_TOKEN_COMMA) {
                        advance_token();
                    }
                } else {
                     std::cerr << "Unexpected token in struct member: " << current_token.literal << "\n";
                     advance_token();
                }
            }
        } else {
            // Unexpected token
             std::cerr << "Expected 'def' or 'pub' in struct body\n";
             advance_token();
        }
    }
    
    advance_token(); // Eat '}'
    
    return std::make_unique<StructDefinition>(std::move(name), std::move(fields), std::move(methods), is_packed, std::move(backing_type));
}

void GloinParser::parse() {
    // For now, this is just a driver loop to test parsing.
    // In a real compiler, this would parse top-level declarations (def, import, etc.)
    // But since the request is to focus on the Pratt Parser (expressions), we will just 
    // try to parse an expression if we see one.
    
    if (current_token.type != GLOIN_TOKEN_EOF) {
        auto expr = parse_expression(0);
        if (expr) {

        }
    }
}
std::vector<std::unique_ptr<Statement>> GloinParser::parse_program() {
    std::vector<std::unique_ptr<Statement>> program;
    while (current_token.type != GLOIN_TOKEN_EOF) {
        auto stmt = parse_statement();
        if (stmt) {
            program.push_back(std::move(stmt));
        } else if (current_token.type != GLOIN_TOKEN_EOF) {
            advance_token();
        }
    }
    return program;
}
