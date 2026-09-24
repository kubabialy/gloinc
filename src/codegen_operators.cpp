#include "codegen.h"
#include "operators.h"

void CodeGen::require_runtime(mlir::Value condition) {
    auto *region = builder.getBlock()->getParent();
    auto *success = new mlir::Block();
    auto *failure = new mlir::Block();
    region->push_back(success);
    region->push_back(failure);
    builder.create<mlir::cf::CondBranchOp>(location(), condition, success, mlir::ValueRange{},
                                           failure, mlir::ValueRange{});
    builder.setInsertionPointToStart(failure);
    builder.create<mlir::LLVM::Trap>(location());
    builder.create<mlir::LLVM::UnreachableOp>(location());
    builder.setInsertionPointToStart(success);
}

mlir::Value CodeGen::checked_integer_arithmetic(std::string_view op, mlir::Value left,
                                                mlir::Value right, CoreType type) {
    const auto &info = core_type_info(type);
    if (op == "/" || op == "%") {
        auto zero = emit_constant({type, llvm::APInt(info.bits, 0)});
        auto nonzero = builder.create<mlir::arith::CmpIOp>(
            location(), mlir::arith::CmpIPredicate::ne, right, zero);
        require_runtime(nonzero);
        if (info.is_signed) {
            auto minimum = emit_constant({type, llvm::APInt::getSignedMinValue(info.bits)});
            auto minus_one = emit_constant({type, llvm::APInt::getAllOnes(info.bits)});
            auto not_minimum = builder.create<mlir::arith::CmpIOp>(
                location(), mlir::arith::CmpIPredicate::ne, left, minimum);
            auto not_minus_one = builder.create<mlir::arith::CmpIOp>(
                location(), mlir::arith::CmpIPredicate::ne, right, minus_one);
            require_runtime(
                builder.create<mlir::arith::OrIOp>(location(), not_minimum, not_minus_one));
            if (op == "/")
                return builder.create<mlir::arith::DivSIOp>(location(), left, right);
            return builder.create<mlir::arith::RemSIOp>(location(), left, right);
        }
        if (op == "/")
            return builder.create<mlir::arith::DivUIOp>(location(), left, right);
        return builder.create<mlir::arith::RemUIOp>(location(), left, right);
    }

    // Double width holds every mathematical sum/product of the supported inputs.
    // Truncation followed by extension detects both signed overflow and unsigned
    // overflow/underflow, without performing poison-producing narrow arithmetic.
    auto wide_type = builder.getIntegerType(info.bits * 2);
    auto extend = [&](mlir::Value value) -> mlir::Value {
        if (info.is_signed)
            return builder.create<mlir::arith::ExtSIOp>(location(), wide_type, value);
        return builder.create<mlir::arith::ExtUIOp>(location(), wide_type, value);
    };
    auto wide_left = extend(left);
    auto wide_right = extend(right);
    mlir::Value wide_result;
    if (op == "+")
        wide_result = builder.create<mlir::arith::AddIOp>(location(), wide_left, wide_right);
    else if (op == "-")
        wide_result = builder.create<mlir::arith::SubIOp>(location(), wide_left, wide_right);
    else if (op == "*")
        wide_result = builder.create<mlir::arith::MulIOp>(location(), wide_left, wide_right);
    else
        fail("Missing checked integer operator");
    auto result = builder.create<mlir::arith::TruncIOp>(location(), left.getType(), wide_result);
    auto restored = extend(result);
    require_runtime(builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::eq,
                                                        wide_result, restored));
    return result;
}

mlir::Value CodeGen::gen_checked_unary(const PrefixExpression *expression) {
    auto type = checked_data->types.at(expression->right.get()).builtin();
    if (!unary_operator_type(expression->op, type))
        fail("Invalid checked unary operator");
    auto operand = gen_expression(expression->right.get());
    if (expression->op == "!")
        return builder.create<mlir::arith::XOrIOp>(location(), operand,
                                                   emit_constant({CoreType::Bool, true}));
    if (core_type_info(type).is_integer)
        return checked_integer_arithmetic(
            "-", emit_constant({type, llvm::APInt(core_type_info(type).bits, 0)}), operand, type);
    return builder.create<mlir::arith::NegFOp>(location(), operand);
}

mlir::Value CodeGen::gen_short_circuit(const InfixExpression *expression) {
    auto left = gen_expression(expression->left.get());
    auto *region = builder.getBlock()->getParent();
    auto *right_block = new mlir::Block();
    auto *merge = new mlir::Block();
    region->push_back(right_block);
    region->push_back(merge);
    auto result = merge->addArgument(builder.getI1Type(), location());
    if (expression->op == "&&")
        builder.create<mlir::cf::CondBranchOp>(location(), left, right_block, mlir::ValueRange{},
                                               merge, mlir::ValueRange{left});
    else
        builder.create<mlir::cf::CondBranchOp>(location(), left, merge, mlir::ValueRange{left},
                                               right_block, mlir::ValueRange{});
    builder.setInsertionPointToStart(right_block);
    auto right = gen_expression(expression->right.get());
    builder.create<mlir::cf::BranchOp>(location(), merge, mlir::ValueRange{right});
    builder.setInsertionPointToStart(merge);
    return result;
}

mlir::Value CodeGen::gen_checked_binary(const InfixExpression *expression) {
    if (checked_data->types.at(expression->left.get()).is_pointer()) {
        auto left = gen_expression(expression->left.get());
        auto right = gen_expression(expression->right.get());
        return builder.create<mlir::LLVM::ICmpOp>(
            location(),
            expression->op == "==" ? mlir::LLVM::ICmpPredicate::eq : mlir::LLVM::ICmpPredicate::ne,
            left, right);
    }
    auto type = checked_data->types.at(expression->left.get()).builtin();
    const auto &op = expression->op;
    if (type != checked_data->types.at(expression->right.get()) || !binary_operator_type(op, type))
        fail("Invalid checked binary operator");
    if (op == "&&" || op == "||")
        return gen_short_circuit(expression);
    auto left = gen_expression(expression->left.get());
    auto right = gen_expression(expression->right.get());
    const auto &info = core_type_info(type);
    bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
    if (info.is_integer || type == CoreType::Bool) {
        if (arithmetic)
            return checked_integer_arithmetic(op, left, right, type);
        using Predicate = mlir::arith::CmpIPredicate;
        auto predicate = Predicate::eq;
        if (op == "!=")
            predicate = Predicate::ne;
        else if (op == "<")
            predicate = info.is_signed ? Predicate::slt : Predicate::ult;
        else if (op == "<=")
            predicate = info.is_signed ? Predicate::sle : Predicate::ule;
        else if (op == ">")
            predicate = info.is_signed ? Predicate::sgt : Predicate::ugt;
        else if (op == ">=")
            predicate = info.is_signed ? Predicate::sge : Predicate::uge;
        return builder.create<mlir::arith::CmpIOp>(location(), predicate, left, right);
    }
    using Predicate = mlir::arith::CmpFPredicate;
    if (!arithmetic) {
        auto predicate = Predicate::OEQ;
        if (op == "!=")
            predicate = Predicate::UNE;
        else if (op == "<")
            predicate = Predicate::OLT;
        else if (op == "<=")
            predicate = Predicate::OLE;
        else if (op == ">")
            predicate = Predicate::OGT;
        else if (op == ">=")
            predicate = Predicate::OGE;
        return builder.create<mlir::arith::CmpFOp>(location(), predicate, left, right);
    }
    mlir::Value result;
    if (op == "+")
        result = builder.create<mlir::arith::AddFOp>(location(), left, right);
    else if (op == "-")
        result = builder.create<mlir::arith::SubFOp>(location(), left, right);
    else if (op == "*")
        result = builder.create<mlir::arith::MulFOp>(location(), left, right);
    else if (op == "/") {
        auto zero = emit_constant({type, llvm::APFloat::getZero(float_semantics(type))});
        require_runtime(
            builder.create<mlir::arith::CmpFOp>(location(), Predicate::ONE, right, zero));
        result = builder.create<mlir::arith::DivFOp>(location(), left, right);
    } else {
        fail("Missing checked floating operator");
    }
    auto largest = llvm::APFloat::getLargest(float_semantics(type));
    auto upper = emit_constant({type, largest});
    largest.changeSign();
    auto lower = emit_constant({type, largest});
    auto above_lower =
        builder.create<mlir::arith::CmpFOp>(location(), Predicate::OGE, result, lower);
    auto below_upper =
        builder.create<mlir::arith::CmpFOp>(location(), Predicate::OLE, result, upper);
    require_runtime(builder.create<mlir::arith::AndIOp>(location(), above_lower, below_upper));
    return result;
}
