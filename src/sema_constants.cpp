#include "sema.h"

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
    auto initializer_type = check_expression(declaration->initializer.get(),
                                             resolve_core_type(declared_type->to_string()));
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
        auto operand = check_expression(prefix->right.get(), expected_type);
        if (!operand)
            return nullptr;
        auto core = resolve_core_type(operand->to_string()).value();
        const auto &info = core_type_info(core);
        if ((prefix->op == "!" && core == CoreType::Bool) || (prefix->op == "-" && info.is_signed))
            return operand;
        log_error("Invalid unary operator in constant expression");
        return nullptr;
    }
    if (const auto *binary = dynamic_cast<const InfixExpression *>(expression)) {
        auto [left, right] = check_binary_operands(binary);
        if (!left || !right)
            return nullptr;
        if (!left->equals(*right)) {
            log_error("Type mismatch in constant expression");
            return nullptr;
        }
        const auto &op = binary->op;
        auto core = resolve_core_type(left->to_string()).value();
        const auto &info = core_type_info(core);
        bool boolean = core == CoreType::Bool;
        bool numeric = info.is_integer || core == CoreType::F32 || core == CoreType::F64;
        if (op == "==" || op == "!=" || (boolean && (op == "&&" || op == "||")) ||
            (numeric && (op == "<" || op == ">" || op == "<=" || op == ">=")))
            return get_builtin_type("bool");
        if ((numeric && (op == "+" || op == "-" || op == "*" || op == "/")) ||
            (info.is_integer && op == "%"))
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
    if (auto found = recording->literals.find(expression); found != recording->literals.end())
        return found->second;
    if (const auto *boolean = dynamic_cast<const BooleanLiteral *>(expression))
        return ConstantValue{type, boolean->value};
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression))
        return recording->constants.at(recording->bindings.at(identifier));
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression)) {
        auto operand = evaluate_constant(prefix->right.get());
        if (!operand)
            return std::nullopt;
        if (prefix->op == "!")
            return ConstantValue{type, !std::get<bool>(operand->value)};
        if (auto *floating = std::get_if<llvm::APFloat>(&operand->value)) {
            floating->changeSign();
            return operand;
        }
        auto value = std::get<llvm::APInt>(operand->value);
        if (value.isMinSignedValue()) {
            log_error("Integer overflow in constant expression");
            return std::nullopt;
        }
        return ConstantValue{type, -value};
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
    } else if (core_type_info(left->type).is_integer) {
        auto a = std::get<llvm::APInt>(left->value), b = std::get<llvm::APInt>(right->value);
        bool signed_type = core_type_info(left->type).is_signed;
        bool less = signed_type ? a.slt(b) : a.ult(b);
        bool greater = signed_type ? a.sgt(b) : a.ugt(b);
        if (op == "==")
            return ConstantValue{type, a == b};
        if (op == "!=")
            return ConstantValue{type, a != b};
        if (op == "<")
            return ConstantValue{type, less};
        if (op == ">")
            return ConstantValue{type, greater};
        if (op == "<=")
            return ConstantValue{type, !greater};
        if (op == ">=")
            return ConstantValue{type, !less};
        if ((op == "/" || op == "%") && b.isZero()) {
            log_error("Division or remainder by zero in constant expression");
            return std::nullopt;
        }
        bool overflow = false;
        llvm::APInt result(a.getBitWidth(), 0);
        if (op == "+")
            result = signed_type ? a.sadd_ov(b, overflow) : a.uadd_ov(b, overflow);
        else if (op == "-")
            result = signed_type ? a.ssub_ov(b, overflow) : a.usub_ov(b, overflow);
        else if (op == "*")
            result = signed_type ? a.smul_ov(b, overflow) : a.umul_ov(b, overflow);
        else if (op == "/")
            result = signed_type ? a.sdiv_ov(b, overflow) : a.udiv(b);
        else if (op == "%") {
            overflow = signed_type && a.isMinSignedValue() && b.isAllOnes();
            if (!overflow)
                result = signed_type ? a.srem(b) : a.urem(b);
        } else {
            log_error("Unsupported constant operation");
            return std::nullopt;
        }
        if (overflow) {
            log_error("Integer overflow in constant expression");
            return std::nullopt;
        }
        return ConstantValue{type, std::move(result)};
    } else {
        auto a = std::get<llvm::APFloat>(left->value), b = std::get<llvm::APFloat>(right->value);
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
        if (op == "/" && b.isZero()) {
            log_error("Division or remainder by zero in constant expression");
            return std::nullopt;
        }
        if (op == "+")
            a.add(b, llvm::APFloat::rmNearestTiesToEven);
        else if (op == "-")
            a.subtract(b, llvm::APFloat::rmNearestTiesToEven);
        else if (op == "*")
            a.multiply(b, llvm::APFloat::rmNearestTiesToEven);
        else if (op == "/")
            a.divide(b, llvm::APFloat::rmNearestTiesToEven);
        else {
            log_error("Unsupported constant operation");
            return std::nullopt;
        }
        if (!a.isFinite()) {
            log_error("Non-finite result in constant expression");
            return std::nullopt;
        }
        return ConstantValue{type, std::move(a)};
    }
    log_error("Unsupported constant operation");
    return std::nullopt;
}
