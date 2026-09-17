#include "codegen.h"

bool CodeGen::has_open_block() {
    auto *block = builder.getBlock();
    if (!block)
        return false;
    if (!block->empty() && block->back().hasTrait<mlir::OpTrait::IsTerminator>())
        fail("Cannot emit into a terminated block");
    return true;
}

bool CodeGen::branch_if_open(mlir::Block *destination) {
    if (!has_open_block())
        return false;
    builder.create<mlir::cf::BranchOp>(location(), destination);
    builder.clearInsertionPoint();
    return true;
}

mlir::Value CodeGen::gen_condition(const Expression *condition, std::string_view construct) {
    DiagnosticScope source(current_span, condition ? condition->span : SourceSpan{});
    auto value = gen_expression(condition);
    if (!value.getType().isInteger(1))
        fail(std::string(construct) + " condition must be bool");
    return value;
}

void CodeGen::gen_if(const IfStatement *statement) {
    // Expressions may introduce their own blocks. Emit the condition's branch
    // from its final continuation, never from the block where evaluation began.
    auto condition = gen_condition(statement->condition.get(), "If");
    auto *region = builder.getBlock()->getParent();
    auto *then_block = new mlir::Block();
    auto *merge = new mlir::Block();
    auto *else_block = statement->alternative ? new mlir::Block() : merge;
    region->push_back(then_block);
    if (statement->alternative)
        region->push_back(else_block);
    region->push_back(merge);
    builder.create<mlir::cf::CondBranchOp>(location(), condition, then_block, mlir::ValueRange{},
                                           else_block, mlir::ValueRange{});

    builder.setInsertionPointToStart(then_block);
    gen_statement(statement->consequence.get());
    bool then_reaches = branch_if_open(merge);
    // Without an else, the false edge goes directly to the merge.
    bool else_reaches = true;
    if (statement->alternative) {
        builder.setInsertionPointToStart(else_block);
        gen_statement(statement->alternative.get());
        else_reaches = branch_if_open(merge);
    }
    if (then_reaches || else_reaches) {
        builder.setInsertionPointToStart(merge);
    } else {
        merge->erase();
        builder.clearInsertionPoint();
    }
}

void CodeGen::gen_while(const WhileStatement *statement) {
    auto *region = builder.getBlock()->getParent();
    auto *condition_block = new mlir::Block();
    auto *body_block = new mlir::Block();
    auto *exit_block = new mlir::Block();
    region->push_back(condition_block);
    region->push_back(body_block);
    region->push_back(exit_block);
    branch_if_open(condition_block);

    builder.setInsertionPointToStart(condition_block);
    auto condition = gen_condition(statement->condition.get(), "While");
    builder.create<mlir::cf::CondBranchOp>(location(), condition, body_block, mlir::ValueRange{},
                                           exit_block, mlir::ValueRange{});
    builder.setInsertionPointToStart(body_block);
    gen_statement(statement->body.get());
    // A returning body has no backedge. A live nested continuation repeats the
    // entire condition, including its calls, guards, and short-circuit branches.
    branch_if_open(condition_block);
    builder.setInsertionPointToStart(exit_block);
}
