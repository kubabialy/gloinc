#include "sema.h"
#include <cmath>
#include <limits>
#include <type_traits>

void Sema::check_constant(const VariableDeclaration *declaration) {
    DiagnosticScope location(current_span, declaration->span);
    if (!recording) {
        log_error("Constants require checked semantic evaluation");
        return;
    }
    if (!declaration->name || !declaration->type || !declaration->initializer) {
        log_error("Constant declaration requires a name, explicit type, and initializer");
        return;
    }
    if (declaration->is_mutable) {
        log_error("A constant cannot be mutable");
        return;
    }
    auto declared_type = resolve_annotation(declaration->type.get());
    if (!declared_type)
        return;
    auto errors_before = diagnostics_->all().size();
    bool previous = checking_constant;
    checking_constant = true;
    auto initializer_type = check_expression(declaration->initializer.get());
    checking_constant = previous;
    if (!initializer_type || diagnostics_->all().size() != errors_before)
        return;
    if (!declared_type->equals(*initializer_type)) {
        log_error("Type mismatch in constant declaration. Expected " + declared_type->to_string() +
                  " but got " + initializer_type->to_string());
        return;
    }
    auto value = evaluate_constant(declaration->initializer.get());
    if (!value)
        return;
    Symbol symbol;
    symbol.name = declaration->name->value;
    symbol.type = declared_type;
    if (define_symbol(declaration->name.get(), symbol, SymbolKind::Constant))
        recording->constants.emplace(recording->bindings.at(declaration->name.get()), *value);
}

std::shared_ptr<Type> Sema::check_constant_expression(const Expression *expression) {
    if (const auto *integer = dynamic_cast<const IntegerLiteral *>(expression)) {
        if (integer->value < std::numeric_limits<int32_t>::min() ||
            integer->value > std::numeric_limits<int32_t>::max()) {
            log_error("Integer constant is out of range for i32 (contextual literals: SPEC-013)");
            return nullptr;
        }
        return get_builtin_type("i32");
    }
    if (const auto *floating = dynamic_cast<const FloatLiteral *>(expression)) {
        if (!std::isfinite(floating->value) ||
            std::abs(floating->value) > std::numeric_limits<float>::max()) {
            log_error("Floating constant is out of range for f32");
            return nullptr;
        }
        return get_builtin_type("f32");
    }
    if (dynamic_cast<const BooleanLiteral *>(expression))
        return get_builtin_type("bool");
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression)) {
        auto *symbol = current_scope->resolve(identifier->value);
        if (!symbol) {
            log_error("Undefined constant '" + identifier->value + "'");
            return nullptr;
        }
        if (symbol->kind != SymbolKind::Constant) {
            log_error("Constant expression cannot reference runtime binding '" + identifier->value +
                      "'");
            return nullptr;
        }
        recording->bindings[identifier] = symbol->id;
        return symbol->type;
    }
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
        auto operand = check_expression(prefix->right.get());
        if (!operand)
            return nullptr;
        auto name = operand->to_string();
        if ((prefix->op == "!" && name == "bool") ||
            (prefix->op == "-" && (name == "i32" || name == "f32")))
            return operand;
        log_error("Invalid unary operator in constant expression");
        return nullptr;
    }
    if (const auto *binary = dynamic_cast<const InfixExpression *>(expression)) {
        auto left = check_expression(binary->left.get());
        auto right = check_expression(binary->right.get());
        if (!left || !right)
            return nullptr;
        if (!left->equals(*right)) {
            log_error("Type mismatch in constant expression");
            return nullptr;
        }
        const auto &op = binary->op;
        auto name = left->to_string();
        bool boolean = name == "bool";
        bool numeric = name == "i32" || name == "f32";
        if (op == "==" || op == "!=" || (boolean && (op == "&&" || op == "||")) ||
            (numeric && (op == "<" || op == ">" || op == "<=" || op == ">=")))
            return get_builtin_type("bool");
        if ((numeric && (op == "+" || op == "-" || op == "*" || op == "/")) ||
            (name == "i32" && op == "%"))
            return left;
        log_error("Invalid binary operator in constant expression");
        return nullptr;
    }
    log_error("Expression is not permitted in a constant initializer");
    return nullptr;
}

std::optional<ConstantValue> Sema::evaluate_constant(const Expression *expression) {
    DiagnosticScope location(current_span, expression->span);
    auto type = recording->types.at(expression);
    if (const auto *integer = dynamic_cast<const IntegerLiteral *>(expression))
        return ConstantValue{type, integer->value};
    if (const auto *floating = dynamic_cast<const FloatLiteral *>(expression))
        return ConstantValue{type, static_cast<double>(static_cast<float>(floating->value))};
    if (const auto *boolean = dynamic_cast<const BooleanLiteral *>(expression))
        return ConstantValue{type, boolean->value};
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression))
        return recording->constants.at(recording->bindings.at(identifier));

    auto integer_result = [&](int64_t value) -> std::optional<ConstantValue> {
        if (value < std::numeric_limits<int32_t>::min() ||
            value > std::numeric_limits<int32_t>::max()) {
            log_error("Integer overflow in constant expression");
            return std::nullopt;
        }
        return ConstantValue{type, value};
    };
    auto float_result = [&](double value) -> std::optional<ConstantValue> {
        float rounded = static_cast<float>(value);
        if (!std::isfinite(rounded)) {
            log_error("Non-finite result in constant expression");
            return std::nullopt;
        }
        return ConstantValue{type, static_cast<double>(rounded)};
    };
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
        auto operand = evaluate_constant(prefix->right.get());
        if (!operand)
            return std::nullopt;
        if (prefix->op == "!")
            return ConstantValue{type, !std::get<bool>(operand->value)};
        if (type == CoreType::F32)
            return float_result(-std::get<double>(operand->value));
        return integer_result(-std::get<int64_t>(operand->value));
    }
    const auto *binary = dynamic_cast<const InfixExpression *>(expression);
    if (!binary) {
        log_error("Missing constant evaluation rule");
        return std::nullopt;
    }
    auto left = evaluate_constant(binary->left.get());
    if (!left)
        return std::nullopt;
    const auto &op = binary->op;
    if (op == "&&" && !std::get<bool>(left->value))
        return ConstantValue{type, false};
    if (op == "||" && std::get<bool>(left->value))
        return ConstantValue{type, true};
    auto right = evaluate_constant(binary->right.get());
    if (!right)
        return std::nullopt;
    if (left->type == CoreType::Bool) {
        bool a = std::get<bool>(left->value), b = std::get<bool>(right->value);
        if (op == "==")
            return ConstantValue{type, a == b};
        if (op == "!=")
            return ConstantValue{type, a != b};
        if (op == "&&")
            return ConstantValue{type, a && b};
        if (op == "||")
            return ConstantValue{type, a || b};
    } else {
        // All supported integer operands are i32. Widening to i64 makes every
        // intermediate arithmetic operation safe before the explicit range check.
        auto numeric = [&](auto a, auto b) -> std::optional<ConstantValue> {
            if (op == "==")
                return ConstantValue{type, a == b};
            if (op == "!=")
                return ConstantValue{type, a != b};
            if (op == "<")
                return ConstantValue{type, a < b};
            if (op == ">")
                return ConstantValue{type, a > b};
            if (op == "<=")
                return ConstantValue{type, a <= b};
            if (op == ">=")
                return ConstantValue{type, a >= b};
            if ((op == "/" || op == "%") && b == 0) {
                log_error("Division or remainder by zero in constant expression");
                return std::nullopt;
            }
            if constexpr (std::is_integral_v<decltype(a)>) {
                if (op == "+")
                    return integer_result(a + b);
                if (op == "-")
                    return integer_result(a - b);
                if (op == "*")
                    return integer_result(a * b);
                if (op == "/")
                    return integer_result(a / b);
                if (op == "%") {
                    // The quotient must be representable even when the remainder is zero.
                    if (!integer_result(a / b))
                        return std::nullopt;
                    return integer_result(a % b);
                }
            } else {
                if (op == "+")
                    return float_result(a + b);
                if (op == "-")
                    return float_result(a - b);
                if (op == "*")
                    return float_result(a * b);
                if (op == "/")
                    return float_result(a / b);
            }
            return std::nullopt;
        };
        auto result =
            left->type == CoreType::F32
                ? numeric(static_cast<float>(std::get<double>(left->value)),
                          static_cast<float>(std::get<double>(right->value)))
                : numeric(std::get<int64_t>(left->value), std::get<int64_t>(right->value));
        if (result || diagnostics_->has_errors())
            return result;
    }
    log_error("Unsupported constant operation");
    return std::nullopt;
}
