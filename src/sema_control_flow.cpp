#include "sema.h"

void Sema::check_conditional(const Expression *condition, const BlockStatement *body,
                             const Statement *alternative, std::string_view construct) {
    auto type = check_expression(condition);
    if (type && !type->equals(*get_builtin_type("bool"))) {
        DiagnosticScope location(current_span, condition->span);
        log_error(std::string(construct) + " condition must be bool");
    }
    auto before = initialization;
    bool before_reaches = falls_through;
    check_statement(body);
    auto body_state = initialization;
    bool body_reaches = falls_through;
    initialization = before;
    falls_through = before_reaches;
    if (alternative)
        check_statement(alternative);
    merge_initialization(before, body_state, body_reaches, initialization, falls_through);
}

void Sema::check_for(const ForStatement *statement) {
    enter_scope();
    if (statement->init) {
        if (!dynamic_cast<const VariableDeclaration *>(statement->init.get()) &&
            !dynamic_cast<const ExpressionStatement *>(statement->init.get()))
            log_error("For initializer must be a binding or expression statement");
        else
            check_statement(statement->init.get());
    }
    if (statement->condition) {
        auto type = check_expression(statement->condition.get());
        if (type && !type->equals(*get_builtin_type("bool"))) {
            DiagnosticScope location(current_span, statement->condition->span);
            log_error("For condition must be bool");
        }
    }
    auto before = initialization;
    bool before_reaches = falls_through;
    ++loop_depth;
    check_statement(statement->body.get());
    bool body_reaches = falls_through;
    // Validate the update even if every body path returns, without borrowing
    // initialization that exists only on returning paths or body-local names.
    if (!body_reaches)
        initialization = before;
    if (statement->increment)
        check_expression(statement->increment.get(), std::nullopt, true);
    --loop_depth;
    merge_initialization(before, before, before_reaches, initialization, body_reaches);
    leave_scope();
}
