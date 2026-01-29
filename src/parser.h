//
// Created by Kuba Bialy on 29/01/2026.
//

#ifndef GLOINC_PARSER_H
#define GLOINC_PARSER_H
#include "lexer.h"
#include "AST.h"

#include <memory>

enum Precedence {
    LOWEST = 1,
    EQUALS,
    LESSGREATER,
    SUM,
    PRODUCT,
    PREFIX,
    CALL
};

class GloinParser {
  public:
    void parse();
    explicit GloinParser(Lexer l);
    std::unique_ptr<Expression> parse_expression(int min_biding_power);

    // Statement parsing
    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<VariableDeclaration> parse_variable_declaration();
    std::unique_ptr<ReturnStatement> parse_return_statement();
    std::unique_ptr<BlockStatement> parse_block_statement();
    std::unique_ptr<IfStatement> parse_if_statement();
    std::unique_ptr<WhileStatement> parse_while_statement();
    std::unique_ptr<DeferStatement> parse_defer_statement();
    std::unique_ptr<ExpressionStatement> parse_expression_statement();

  private:
    Lexer lexer;

    GloinToken current_token;
    GloinToken next_token; // peeked, not consumed

    void advance_token();

    static int get_binding_power(GloinTokenType type);
    // std::unique_ptr<Expression> parse_expression(int min_biding_power); // Moved to public
    std::unique_ptr<Expression> parse_prefix();
    std::unique_ptr<Expression> parse_infix(std::unique_ptr<Expression> left);
    std::vector<std::unique_ptr<Expression>> parse_expression_list(GloinTokenType end_token); // Helper for arguments


};

#endif // GLOINC_PARSER_H
