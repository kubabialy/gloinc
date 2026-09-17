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

void CodeGen::gen_branch(const Expression *expression, const BlockStatement *body,
                         const Statement *alternative, bool invert) {
    // Expressions may introduce their own blocks. Emit the condition's branch
    // from its final continuation, never from the block where evaluation began.
    auto condition = gen_condition(expression, invert ? "Unless" : "If");
    auto *region = builder.getBlock()->getParent();
    auto *then_block = new mlir::Block();
    auto *merge = new mlir::Block();
    auto *else_block = alternative ? new mlir::Block() : merge;
    region->push_back(then_block);
    if (alternative)
        region->push_back(else_block);
    region->push_back(merge);
    builder.create<mlir::cf::CondBranchOp>(location(), condition, invert ? else_block : then_block,
                                           mlir::ValueRange{}, invert ? then_block : else_block,
                                           mlir::ValueRange{});

    builder.setInsertionPointToStart(then_block);
    gen_statement(body);
    bool then_reaches = branch_if_open(merge);
    // Without an else, the skipped-body edge goes directly to the merge.
    bool else_reaches = true;
    if (alternative) {
        builder.setInsertionPointToStart(else_block);
        gen_statement(alternative);
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

void CodeGen::gen_for(const ForStatement *statement) {
    enter_scope();
    if (statement->init) {
        if (!dynamic_cast<const VariableDeclaration *>(statement->init.get()) &&
            !dynamic_cast<const ExpressionStatement *>(statement->init.get()))
            fail("For initializer must be a binding or expression statement");
        gen_statement(statement->init.get());
    }
    auto *region = builder.getBlock()->getParent();
    auto *condition_block = new mlir::Block();
    auto *body_block = new mlir::Block();
    auto *exit_block = new mlir::Block();
    region->push_back(condition_block);
    region->push_back(body_block);
    region->push_back(exit_block);
    branch_if_open(condition_block);
    builder.setInsertionPointToStart(condition_block);
    auto condition = statement->condition ? gen_condition(statement->condition.get(), "For")
                                          : emit_constant({CoreType::Bool, true});
    builder.create<mlir::cf::CondBranchOp>(location(), condition, body_block, mlir::ValueRange{},
                                           exit_block, mlir::ValueRange{});
    builder.setInsertionPointToStart(body_block);
    gen_statement(statement->body.get());
    if (has_open_block()) {
        if (statement->increment)
            gen_expression(statement->increment.get(), true);
        branch_if_open(condition_block);
    }
    builder.setInsertionPointToStart(exit_block);
    leave_scope();
}
