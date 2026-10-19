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

std::shared_ptr<Type> Sema::check_selection(const Node *node, const Expression *subject,
                                            const std::vector<const CasePattern *> &patterns,
                                            bool matching) {
    DiagnosticScope location(current_span, node->span);
    if (!recording) {
        log_error("switch and match require checked semantic analysis");
        return nullptr;
    }
    auto type = check_expression(subject);
    if (!type)
        return nullptr;
    auto *enumeration = dynamic_cast<EnumType *>(type.get());
    auto *result = dynamic_cast<ResultType *>(type.get());
    auto core = resolve_core_type(type->to_string());
    bool boolean = core && *core == CoreType::Bool;
    bool integer = core && core_type_info(*core).is_integer;
    bool string = core && *core == CoreType::String;
    if (!enumeration && !boolean && !integer && !string && !(matching && result)) {
        log_error(matching ? "Unsupported match subject type" : "Unsupported switch subject type");
        return nullptr;
    }
    if (matching && patterns.size() < 2)
        log_error("match requires at least two arms");
    uint64_t domain = enumeration ? enumeration->variants.size() : (boolean || result) ? 2 : 0;
    recording->selection_domains[node] = domain;
    std::unordered_set<std::string> seen;
    bool fallback = false;
    for (const auto *pattern : patterns) {
        DiagnosticScope arm_location(current_span, pattern->span);
        if (fallback)
            log_error("Unreachable arm: default must be last");
        if (pattern->kind == CaseKind::Default) {
            if (fallback)
                log_error("Duplicate default arm");
            if (domain && seen.size() == domain)
                log_error("Unreachable default: all outcomes are covered");
            fallback = true;
            continue;
        }
        std::optional<ConstantValue> value;
        if (result) {
            if (pattern->kind != CaseKind::Success && pattern->kind != CaseKind::Error) {
                log_error("A result match requires value(...) or error(...) patterns");
                continue;
            }
            bool success = pattern->kind == CaseKind::Success;
            bool empty = success && dynamic_cast<VoidType *>(result->value.get());
            if (empty && pattern->binding)
                log_error("result<void> success uses value() without a binding");
            if (!empty && !pattern->binding)
                log_error("Result pattern requires a payload binding");
            value = ConstantValue{CoreType::Bool, !success};
        } else if (pattern->kind != CaseKind::Value) {
            log_error("Result pattern requires a result subject");
            continue;
        } else if (enumeration) {
            auto *member = dynamic_cast<const MemberAccessExpression *>(pattern->value.get());
            auto label_type = check_expression(pattern->value.get());
            if (!label_type || !label_type->equals(*type) || !member ||
                !recording->enum_variants.contains(member)) {
                log_error("Enum case must name a variant of the subject enum");
                continue;
            }
            value =
                ConstantValue{CoreType::U32, llvm::APInt(32, recording->enum_variants.at(member))};
        } else {
            auto errors_before = diagnostics_->all().size();
            bool previous = checking_constant;
            checking_constant = true;
            auto label_type = check_expression(pattern->value.get(), core);
            checking_constant = previous;
            if (!label_type || diagnostics_->all().size() != errors_before)
                continue;
            if (!label_type->equals(*type)) {
                log_error("Case type must match the subject type");
                continue;
            }
            value = evaluate_constant(pattern->value.get());
        }
        if (!value)
            continue;
        std::string key;
        if (auto *text = std::get_if<std::string>(&value->value))
            key = *text;
        else if (auto *flag = std::get_if<bool>(&value->value))
            key = *flag ? "1" : "0";
        else
            key = std::to_string(std::get<llvm::APInt>(value->value).getZExtValue());
        if (!seen.insert(key).second)
            log_error("Duplicate case value");
        recording->case_values.emplace(pattern, *value);
    }
    bool exhaustive = fallback || (domain && seen.size() == domain);
    if (exhaustive)
        recording->exhaustive_selections.insert(node);
    if (matching && !exhaustive)
        log_error(domain ? "Non-exhaustive match: missing outcome"
                         : "Integer and string match require default");
    return type;
}

void Sema::check_switch(const SwitchStatement *statement) {
    std::vector<const CasePattern *> patterns;
    for (const auto &arm : statement->arms)
        patterns.push_back(&arm.pattern);
    if (!check_selection(statement, statement->subject.get(), patterns, false))
        return;
    auto before = initialization;
    auto before_proof = result_proof;
    auto before_handled = result_handled;
    bool before_reaches = falls_through;
    auto merged = before;
    auto merged_proof = before_proof;
    auto merged_handled = before_handled;
    bool reaches = before_reaches && !recording->exhaustive_selections.contains(statement);
    for (const auto &arm : statement->arms) {
        initialization = before;
        result_proof = before_proof;
        result_handled = before_handled;
        falls_through = before_reaches;
        check_statement(arm.body.get());
        auto arm_proof = result_proof;
        auto arm_handled = result_handled;
        bool arm_reaches = falls_through;
        merge_initialization(before, merged, reaches, initialization, arm_reaches);
        merged = initialization;
        result_proof = before_proof;
        result_handled = before_handled;
        merge_result_flow(merged_proof, merged_handled, reaches, arm_proof, arm_handled,
                          arm_reaches);
        merged_proof = result_proof;
        merged_handled = result_handled;
        reaches = reaches || arm_reaches;
    }
    initialization = merged;
    result_proof = merged_proof;
    result_handled = merged_handled;
    falls_through = reaches;
}

std::shared_ptr<Type> Sema::check_match(const MatchExpression *expression,
                                        std::shared_ptr<Type> expected) {
    DiagnosticScope location(current_span, expression->span);
    std::vector<const CasePattern *> patterns;
    for (const auto &arm : expression->arms)
        patterns.push_back(&arm.pattern);
    auto subject = check_selection(expression, expression->subject.get(), patterns, true);
    if (!subject)
        return nullptr;
    auto *result = dynamic_cast<ResultType *>(subject.get());
    auto *name = dynamic_cast<const Identifier *>(expression->subject.get());
    std::optional<SymbolId> result_id;
    if (result && name && recording->bindings.contains(name)) {
        result_id = recording->bindings.at(name);
        result_handled[*result_id] = true;
    }
    auto before = initialization;
    auto before_proof = result_proof;
    auto before_handled = result_handled;
    bool before_reaches = falls_through;
    auto merged_proof = before_proof;
    auto merged_handled = before_handled;
    bool first = true;
    for (const auto &arm : expression->arms) {
        DiagnosticScope arm_location(current_span, arm.value->span);
        initialization = before;
        result_proof = before_proof;
        result_handled = before_handled;
        falls_through = before_reaches;
        enter_scope();
        bool success = arm.pattern.kind == CaseKind::Success;
        bool failure = arm.pattern.kind == CaseKind::Error;
        if (result && (success || failure)) {
            if (result_id)
                result_proof[*result_id] = success ? ResultProof::Success : ResultProof::Error;
            auto payload = success ? result->value : std::make_shared<ErrorType>();
            if (arm.pattern.binding && !dynamic_cast<VoidType *>(payload.get())) {
                Symbol symbol;
                symbol.name = arm.pattern.binding->value;
                symbol.type = payload;
                if (define_symbol(arm.pattern.binding.get(), symbol, SymbolKind::Variable))
                    initialization[recording->bindings.at(arm.pattern.binding.get())] =
                        Initialization::Initialized;
            }
        }
        auto actual = check_typed_expression(arm.value.get(), expected);
        if (actual && dynamic_cast<VoidType *>(actual.get())) {
            log_error("A match arm must produce a value");
            actual.reset();
        }
        if (!expected)
            expected = actual;
        else if (actual && !expected->equals(*actual))
            log_error("Match arm type mismatch");
        leave_scope();
        auto arm_proof = result_proof;
        auto arm_handled = result_handled;
        result_proof = before_proof;
        result_handled = before_handled;
        merge_result_flow(merged_proof, merged_handled, !first, arm_proof, arm_handled, true);
        merged_proof = result_proof;
        merged_handled = result_handled;
        first = false;
    }
    initialization = before;
    result_proof = merged_proof;
    result_handled = merged_handled;
    falls_through = before_reaches;
    return expected;
}
