#include "sema.h"

std::shared_ptr<Type> Sema::check_integer_conversion(const CallExpression *call, bool &handled) {
    const auto *name = dynamic_cast<const Identifier *>(call->function.get());
    auto target = name ? resolve_core_type(name->value) : std::nullopt;
    if (!target && name)
        if (auto type = current_scope->resolve_type(name->value))
            target = resolve_core_type(type->to_string());
    if (!target)
        return nullptr;
    handled = true;
    const auto &destination = core_type_info(*target);
    if (!destination.is_integer || call->arguments.size() != 1) {
        log_error("Integer conversion requires an integer type and exactly one argument");
        return nullptr;
    }
    if (checking_constant) {
        log_error(
            "Integer constructors return result<T>; use a typed literal in a constant initializer");
        return nullptr;
    }
    // Conversion checks the source value, not a literal coerced to the destination.
    // Unanchored literal expressions use i64, or u64 when a positive literal
    // needs its full range. Typed operands continue to anchor their own arithmetic.
    auto needs_unsigned = [&](auto &&self, const Expression *expression) -> bool {
        if (const auto *literal = dynamic_cast<const IntegerLiteral *>(expression)) {
            std::string error;
            return !parse_numeric_literal(literal->literal, false, CoreType::I64, false, error);
        }
        if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
            if (prefix->op == "-" && dynamic_cast<const IntegerLiteral *>(prefix->right.get()))
                return false;
            return self(self, prefix->right.get());
        }
        if (const auto *binary = dynamic_cast<const InfixExpression *>(expression))
            return self(self, binary->left.get()) || self(self, binary->right.get());
        return false;
    };
    const auto *input = call->arguments.front().get();
    auto context = numeric_anchor(input);
    if (!context)
        context = needs_unsigned(needs_unsigned, input) ? CoreType::U64 : CoreType::I64;
    auto argument = check_expression(input, context);
    if (!argument)
        return nullptr;
    auto source = resolve_core_type(argument->to_string());
    if (!source || !core_type_info(*source).is_integer) {
        log_error("Integer conversion requires an integer argument");
        return nullptr;
    }
    recording->integer_conversions.insert(call);
    return std::make_shared<ResultType>(get_builtin_type(std::string(destination.name)));
}

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
    if (auto type = expression_type_hint(expression))
        return resolve_core_type(type->to_string());
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
    // A checked typed expression can provide context even when the cheap hint
    // cannot resolve a generic call or an indexed aggregate. Literal trees are
    // pure syntax, so checking the typed side first does not reorder effects.
    auto literal_tree = [](auto &&self, const Expression *value) -> bool {
        if (dynamic_cast<const IntegerLiteral *>(value) || dynamic_cast<const FloatLiteral *>(value))
            return true;
        if (const auto *prefix = dynamic_cast<const PrefixExpression *>(value))
            return prefix->op == "-" && self(self, prefix->right.get());
        if (const auto *binary = dynamic_cast<const InfixExpression *>(value))
            return (binary->op == "+" || binary->op == "-" || binary->op == "*" ||
                    binary->op == "/" || binary->op == "%") &&
                   self(self, binary->left.get()) && self(self, binary->right.get());
        return false;
    };
    if (!context && literal_tree(literal_tree, expression->left.get()) &&
        !literal_tree(literal_tree, expression->right.get())) {
        auto right = check_expression(expression->right.get());
        auto type = right ? resolve_core_type(right->to_string()) : std::nullopt;
        auto left = check_expression(expression->left.get(), type);
        return {left, right};
    }
    auto left = check_expression(expression->left.get(), context);
    if (!context && left)
        context = resolve_core_type(left->to_string());
    auto right = check_expression(expression->right.get(), context);
    return {left, right};
}
