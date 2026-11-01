#include "gir_float_checks.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace {
bool propagates_nonfinite(gloin::CheckedFloatBinaryOp op) {
    if (!op) return false;
    auto type = op.getResult().getType();
    return (type.isF32() || type.isF64()) &&
           (op.getKind() == "+" || op.getKind() == "-" || op.getKind() == "*");
}

void combine_run(llvm::ArrayRef<gloin::CheckedFloatBinaryOp> run,
                 mlir::IRRewriter &rewriter) {
    if (run.size() < 2) return;
    llvm::SmallPtrSet<mlir::Operation *, 32> members;
    for (auto op : run) members.insert(op);
    llvm::SmallVector<gloin::CheckedFloatBinaryOp> removable;
    for (auto op : run) {
        // A single use in this straight-line run carries every non-finite
        // result to a later checked operation. Following those uses must end
        // at a retained check: SSA is acyclic within a block. Escaping and
        // unused values retain their original mandatory checks.
        if (op.getResult().hasOneUse() &&
            members.contains(*op.getResult().getUsers().begin()))
            removable.push_back(op);
    }
    if (removable.empty()) return;

    // The public lowering API accepts poison/undef in mixed GIR/LLVM input.
    // Freeze ALL external inputs, including those of independent intermediate
    // checks: otherwise a later poison could hide an earlier deferred failure.
    // Internal results cannot be poison once their inputs are concrete. IEEE
    // +, -, * propagate non-finite values even for Inf * 0 or Inf - Inf.
    // Freezing preserves every concrete input, and never licenses memory reads.
    llvm::DenseMap<mlir::Value, mlir::Value> frozen;
    rewriter.setInsertionPoint(run.front());
    for (auto op : run) {
        for (unsigned i = 0; i != 2; ++i) {
            auto input = op->getOperand(i);
            if (members.contains(input.getDefiningOp())) continue;
            auto [it, inserted] = frozen.try_emplace(input);
            if (inserted)
                it->second = rewriter.create<mlir::LLVM::FreezeOp>(op.getLoc(), input);
            rewriter.modifyOpInPlace(op, [&] { op->setOperand(i, it->second); });
        }
    }
    for (auto op : removable) {
        rewriter.setInsertionPoint(op);
        if (op.getKind() == "+")
            rewriter.replaceOpWithNewOp<mlir::arith::AddFOp>(op, op.getLhs(), op.getRhs());
        else if (op.getKind() == "-")
            rewriter.replaceOpWithNewOp<mlir::arith::SubFOp>(op, op.getLhs(), op.getRhs());
        else
            rewriter.replaceOpWithNewOp<mlir::arith::MulFOp>(op, op.getLhs(), op.getRhs());
    }
}
} // namespace

void combine_gloin_float_checks(mlir::ModuleOp module) {
    mlir::IRRewriter rewriter(module.getContext());
    llvm::SetVector<mlir::Block *> blocks;
    module.walk([&](gloin::CheckedFloatBinaryOp op) { blocks.insert(op->getBlock()); });
    for (auto *block : blocks) {
        auto it = block->begin();
        while (it != block->end()) {
            auto first = mlir::dyn_cast<gloin::CheckedFloatBinaryOp>(&*it);
            if (!propagates_nonfinite(first)) { ++it; continue; }
            llvm::SmallVector<gloin::CheckedFloatBinaryOp> run;
            // A fixed-size window bounds temporary storage without a recursive
            // graph search. Every other operation is a barrier, even constants.
            while (it != block->end() && run.size() != 64) {
                auto op = mlir::dyn_cast<gloin::CheckedFloatBinaryOp>(&*it);
                if (!propagates_nonfinite(op) || op.getType() != first.getType()) break;
                run.push_back(op);
                ++it;
            }
            combine_run(run, rewriter);
        }
    }
}
