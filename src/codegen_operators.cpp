#include "codegen.h"
#include "operators.h"

void CodeGen::require_runtime(mlir::Value condition) {
    if (checked_data) {
        builder.create<gloin::AssertOp>(location(), condition);
        return;
    }
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
    return builder.create<gloin::CheckedIntegerBinaryOp>(
        location(), left.getType(), left, right, builder.getStringAttr(op),
        builder.getBoolAttr(info.is_signed));
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
    const auto &left_type = checked_data->types.at(expression->left.get());
    if (left_type.structure && checked_data->enum_types.contains(*left_type.structure)) {
        auto left = gen_expression(expression->left.get());
        auto right = gen_expression(expression->right.get());
        auto source = source_type(left_type);
        left = as_source_value(left, source);
        right = as_source_value(right, source);
        return builder.create<gloin::EnumCompareOp>(
            location(), builder.getI1Type(), left, right,
            builder.getBoolAttr(expression->op == "!="),
            mlir::TypeAttr::get(source));
    }
    if (checked_data->types.at(expression->left.get()).is_pointer()) {
        auto left = gen_expression(expression->left.get());
        auto right = gen_expression(expression->right.get());
        auto pointer = source_type(checked_data->types.at(expression->left.get()));
        if (expression->op == "+") {
            auto element = checked_data->types.at(expression->left.get()).pointee();
            auto offset = builder.create<gloin::PointerOffsetOp>(
                location(), pointer, as_source_value(left, pointer), right,
                mlir::TypeAttr::get(lower_type(element)),
                mlir::TypeAttr::get(pointer));
            return builder.create<gloin::ToLayoutOp>(
                location(), lower_type(checked_data->types.at(expression)), offset);
        }
        return builder.create<gloin::PointerCompareOp>(
            location(), builder.getI1Type(), as_source_value(left, pointer),
            as_source_value(right, pointer),
            builder.getBoolAttr(expression->op == "!="),
            mlir::TypeAttr::get(pointer));
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
        return builder.create<gloin::CheckedIntegerCompareOp>(
            location(), builder.getI1Type(), left, right,
            builder.getStringAttr(op), builder.getBoolAttr(info.is_signed));
    }
    if (!arithmetic)
        return builder.create<gloin::CheckedFloatCompareOp>(
            location(), builder.getI1Type(), left, right, builder.getStringAttr(op));
    return builder.create<gloin::CheckedFloatBinaryOp>(
        location(), left.getType(), left, right, builder.getStringAttr(op));
}
