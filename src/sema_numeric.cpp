#include "sema.h"

std::shared_ptr<Type> Sema::check_numeric_literal(const Expression *expression) {
    DiagnosticScope location(current_span, expression->span);
    const auto *prefix = dynamic_cast<const PrefixExpression *>(expression);
    const auto *leaf = prefix ? prefix->right.get() : expression;
    const auto *integer = dynamic_cast<const IntegerLiteral *>(leaf);
    const auto *floating = dynamic_cast<const FloatLiteral *>(leaf);
    auto type = integer ? CoreType::I32 : CoreType::F32;
    if (expected_type && *expected_type != CoreType::Bool && *expected_type != CoreType::Void)
        type = *expected_type;
    std::string error;
    auto value = parse_numeric_literal(integer ? integer->literal : floating->literal,
                                       floating != nullptr, type, prefix != nullptr, error);
    if (!value) {
        log_error(error);
        return nullptr;
    }
    if (recording)
        recording->literals.insert_or_assign(expression, std::move(*value));
    return get_builtin_type(std::string(core_type_info(type).name));
}

std::optional<CoreType> Sema::numeric_anchor(const Expression *expression) {
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression)) {
        if (auto *symbol = current_scope->resolve(identifier->value))
            return resolve_core_type(symbol->type->to_string());
    }
    if (const auto *call = dynamic_cast<const CallExpression *>(expression)) {
        if (const auto *identifier = dynamic_cast<const Identifier *>(call->function.get())) {
            if (auto *symbol = current_scope->resolve(identifier->value)) {
                if (const auto *function = dynamic_cast<const FunctionType *>(symbol->type.get()))
                    return resolve_core_type(function->return_type->to_string());
            }
        }
    }
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
        if (prefix->op == "-")
            return numeric_anchor(prefix->right.get());
    }
    if (const auto *binary = dynamic_cast<const InfixExpression *>(expression)) {
        if (binary->op == "+" || binary->op == "-" || binary->op == "*" || binary->op == "/" ||
            binary->op == "%") {
            auto left = numeric_anchor(binary->left.get());
            return left ? left : numeric_anchor(binary->right.get());
        }
    }
    return std::nullopt;
}

std::pair<std::shared_ptr<Type>, std::shared_ptr<Type>>
Sema::check_binary_operands(const InfixExpression *expression) {
    bool arithmetic = expression->op == "+" || expression->op == "-" || expression->op == "*" ||
                      expression->op == "/" || expression->op == "%";
    auto context = numeric_anchor(expression->left.get());
    if (!context)
        context = numeric_anchor(expression->right.get());
    if (!context && arithmetic)
        context = expected_type;
    auto left = check_expression(expression->left.get(), context);
    auto right = check_expression(expression->right.get(), context);
    return {left, right};
}
