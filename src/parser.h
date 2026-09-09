//
// Created by Kuba Bialy on 29/01/2026.
//

#ifndef GLOINC_PARSER_H
#define GLOINC_PARSER_H
#include "AST.h"
#include "lexer.h"

#include <memory>
#include <stdexcept>

enum Precedence { LOWEST = 1, EQUALS, LESSGREATER, SUM, PRODUCT, PREFIX, CALL };

struct ParseResult {
    std::vector<std::unique_ptr<Statement>> program;
    bool success;
};

class GloinParser {
  public:
    void parse();
    std::vector<std::unique_ptr<Statement>> parse_program();
    ParseResult parse_checked_program();
    bool has_error() const { return diagnostics()->has_errors(); }
    std::shared_ptr<Diagnostics> diagnostics() const { return lexer.diagnostics(); }
    explicit GloinParser(Lexer l);
    std::unique_ptr<Expression> parse_expression(int min_biding_power);

    // Statement parsing
    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Statement> parse_def_statement(); // Handles VarDecl and FuncDef
    std::unique_ptr<VariableDeclaration> parse_variable_declaration();
    std::unique_ptr<FunctionDefinition> parse_function_definition(bool is_spawnable = false,
                                                                  bool is_deferred = false);
    std::unique_ptr<ReturnStatement> parse_return_statement();
    std::unique_ptr<BlockStatement> parse_block_statement();
    std::unique_ptr<IfStatement> parse_if_statement();
    std::unique_ptr<WhileStatement> parse_while_statement();
    std::unique_ptr<DeferStatement> parse_defer_statement();
    std::unique_ptr<Statement> parse_struct_definition(bool is_packed = false);
    std::unique_ptr<ExpressionStatement> parse_expression_statement();
    std::unique_ptr<UnlessStatement> parse_unless_statement();
    std::unique_ptr<ForStatement> parse_for_statement();
    std::unique_ptr<ImportStatement> parse_import_statement();

    std::unique_ptr<Identifier> parse_type();

  private:
    Lexer lexer;
    std::vector<std::string> parse_generic_params();

    GloinToken current_token{};
    GloinToken next_token{}; // peeked, not consumed
    size_t consumed_end = 0;
    size_t node_end = 0;
    size_t parse_depth = 0;
    std::unique_ptr<Expression> parse_expression_impl(int min_binding_power);
    std::unique_ptr<Expression> parse_prefix_impl();
    std::unique_ptr<Expression> parse_infix_impl(std::unique_ptr<Expression> left);
    std::unique_ptr<Statement> parse_def_statement_impl();
    std::unique_ptr<Statement> parse_statement_impl();
    std::unique_ptr<Identifier> parse_type_impl();
    std::unique_ptr<VariableDeclaration> parse_variable_declaration_impl();
    std::unique_ptr<FunctionDefinition> parse_function_definition_impl(bool is_spawnable,
                                                                       bool is_deferred);
    std::unique_ptr<ReturnStatement> parse_return_statement_impl();
    std::unique_ptr<BlockStatement> parse_block_statement_impl();
    std::unique_ptr<IfStatement> parse_if_statement_impl();
    std::unique_ptr<WhileStatement> parse_while_statement_impl();
    std::unique_ptr<DeferStatement> parse_defer_statement_impl();
    std::unique_ptr<ExpressionStatement> parse_expression_statement_impl();
    std::unique_ptr<Statement> parse_struct_definition_impl(bool is_packed);
    std::unique_ptr<ImportStatement> parse_import_statement_impl();
    std::unique_ptr<UnlessStatement> parse_unless_statement_impl();
    std::unique_ptr<ForStatement> parse_for_statement_impl();
    struct ParseFailure {};
    [[noreturn]] void fail(const std::string &message);

    template <typename T, typename... Args> std::unique_ptr<T> located_node(Args &&...args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->span = current_token.span;
        node_end = std::max(node_end, node->span.end);
        return node;
    }

    template <typename T, typename F> std::unique_ptr<T> parse_node(SourceSpan start, F parse) {
        if (parse_depth == 0 && has_error())
            return nullptr;
        struct DepthGuard {
            size_t &depth;
            explicit DepthGuard(size_t &depth) : depth(depth) { ++depth; }
            ~DepthGuard() { --depth; }
        } guard(parse_depth);
        try {
            auto node = parse();
            if (!node)
                fail("Expected a syntax node");
            node->span = {start.source, start.begin, std::max({start.end, consumed_end, node_end})};
            node_end = std::max(node_end, node->span.end);
            return node;
        } catch (const ParseFailure &) {
            if (parse_depth > 1)
                throw;
            return nullptr;
        } catch (const std::invalid_argument &) {
            diagnostics()->error(DiagnosticStage::Parsing, current_token.span,
                                 "Invalid numeric value");
            if (parse_depth > 1)
                throw ParseFailure{};
            return nullptr;
        } catch (const std::out_of_range &) {
            diagnostics()->error(DiagnosticStage::Parsing, current_token.span,
                                 "Numeric value out of range");
            if (parse_depth > 1)
                throw ParseFailure{};
            return nullptr;
        }
    }

    void advance_token();

    static int get_binding_power(GloinTokenType type);
    // std::unique_ptr<Expression> parse_expression(int min_biding_power); // Moved to public
    std::unique_ptr<Expression> parse_prefix();
    std::unique_ptr<Expression> parse_infix(std::unique_ptr<Expression> left);
    std::vector<std::unique_ptr<Expression>>
    parse_expression_list(GloinTokenType end_token); // Helper for arguments
};

#endif // GLOINC_PARSER_H
