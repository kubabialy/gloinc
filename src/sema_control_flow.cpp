#include "sema.h"

std::optional<std::pair<SymbolId, Sema::ResultProof>>
Sema::result_condition(const Expression *condition) {
    bool negated = false;
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(condition);
        prefix && prefix->op == "!") {
        negated = true;
        condition = prefix->right.get();
    }
    const auto *member = dynamic_cast<const MemberAccessExpression *>(condition);
    const auto *field = member ? dynamic_cast<const Identifier *>(member->member.get()) : nullptr;
    const auto *base = member ? dynamic_cast<const Identifier *>(member->left.get()) : nullptr;
    if (!field || field->value != "erroneous" || !base)
        return std::nullopt;
    auto binding = recording->bindings.find(base);
    if (binding == recording->bindings.end() || !result_proof.contains(binding->second))
        return std::nullopt;
    return std::make_pair(binding->second,
                          negated ? ResultProof::Success : ResultProof::Error);
}

void Sema::merge_result_flow(const std::unordered_map<SymbolId, ResultProof> &left,
                             const std::unordered_map<SymbolId, bool> &left_handled,
                             bool left_reaches,
                             const std::unordered_map<SymbolId, ResultProof> &right,
                             const std::unordered_map<SymbolId, bool> &right_handled,
                             bool right_reaches) {
    for (auto &[id, proof] : result_proof) {
        auto lhs = left.contains(id) ? left.at(id) : ResultProof::Unknown;
        auto rhs = right.contains(id) ? right.at(id) : ResultProof::Unknown;
        proof = !left_reaches ? rhs : !right_reaches ? lhs
                                                  : lhs == rhs ? lhs : ResultProof::Unknown;
        bool lhs_handled = left_handled.contains(id) && left_handled.at(id);
        bool rhs_handled = right_handled.contains(id) && right_handled.at(id);
        result_handled[id] = !left_reaches ? rhs_handled
                              : !right_reaches ? lhs_handled
                                               : lhs_handled && rhs_handled;
    }
}

void Sema::check_conditional(const Expression *condition, const BlockStatement *body,
                             const Statement *alternative, std::string_view construct) {
    auto type = check_expression(condition);
    if (type && !type->equals(*get_builtin_type("bool"))) {
        DiagnosticScope location(current_span, condition->span);
        log_error(std::string(construct) + " condition must be bool");
    }
    auto before = initialization;
    bool before_reaches = falls_through;
    auto before_proof = result_proof;
    auto before_handled = result_handled;
    auto refinement = recording ? result_condition(condition) : std::nullopt;
    if (refinement && construct == "Unless")
        refinement->second = refinement->second == ResultProof::Error
                                 ? ResultProof::Success : ResultProof::Error;
    if (refinement) {
        result_handled[refinement->first] = true;
        before_handled[refinement->first] = true;
        result_proof[refinement->first] = refinement->second;
    }
    check_statement(body);
    auto body_state = initialization;
    bool body_reaches = falls_through;
    auto body_proof = result_proof;
    auto body_handled = result_handled;
    initialization = before;
    falls_through = before_reaches;
    result_proof = before_proof;
    result_handled = before_handled;
    if (refinement)
        result_proof[refinement->first] =
            refinement->second == ResultProof::Error ? ResultProof::Success
                                                     : ResultProof::Error;
    if (alternative)
        check_statement(alternative);
    auto other_proof = result_proof;
    auto other_handled = result_handled;
    bool other_reaches = falls_through;
    merge_initialization(before, body_state, body_reaches, initialization, falls_through);
    result_proof = before_proof;
    result_handled = before_handled;
    merge_result_flow(body_proof, body_handled, body_reaches,
                      other_proof, other_handled, other_reaches);
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
    auto before_proof = result_proof;
    auto before_handled = result_handled;
    auto refinement = recording && statement->condition
                          ? result_condition(statement->condition.get())
                          : std::nullopt;
    if (refinement) {
        result_handled[refinement->first] = true;
        before_handled[refinement->first] = true;
        result_proof[refinement->first] = refinement->second;
    }
    ++loop_depth;
    check_statement(statement->body.get());
    bool body_reaches = falls_through;
    // Validate the update even if every body path returns, without borrowing
    // initialization that exists only on returning paths or body-local names.
    if (!body_reaches)
        initialization = before;
    if (statement->increment) {
        auto type = check_expression(statement->increment.get(), std::nullopt, true);
        if (recording && dynamic_cast<ResultType *>(type.get()))
            log_error("A result cannot be discarded in a for increment");
    }
    --loop_depth;
    merge_initialization(before, before, before_reaches, initialization, body_reaches);
    auto body_proof = result_proof;
    auto body_handled = result_handled;
    result_proof = before_proof;
    result_handled = before_handled;
    merge_result_flow(before_proof, before_handled, before_reaches,
                      body_proof, body_handled, body_reaches);
    for (auto &[id, proof] : result_proof)
        proof = ResultProof::Unknown;
    leave_scope();
}
