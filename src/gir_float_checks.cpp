#include "gir_float_checks.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"

void combine_gloin_float_checks(mlir::ModuleOp module) {
    mlir::IRRewriter rewriter(module.getContext());
    module.walk([&](gloin::CheckedFloatBinaryOp sum) {
        if (sum.getKind() != "+") return;
        auto product = mlir::dyn_cast_or_null<gloin::CheckedFloatBinaryOp>(sum->getPrevNode());
        if (!product || product.getKind() != "*" || !product.getResult().hasOneUse() ||
            *product.getResult().getUsers().begin() != sum.getOperation())
            return;
        const auto type = product.getResult().getType();
        if ((!type.isF32() && !type.isF64()) || type != sum.getResult().getType()) return;

        // With ordinary IEEE operations, a non-finite product necessarily makes
        // the following sum non-finite, regardless of the other addend. The sum's
        // mandatory finite check therefore detects both failures. Keep separate
        // rounding: this is an ordinary multiply, with no fast-math or contraction.
        //
        // Exact adjacency and a single use ensure that no memory access, effect,
        // other failure, branch, or escaping product is crossed. Both addends are
        // already available in verified SSA. Never extend this to a merely
        // read-only intervening operation: reads can fault. No loop/lifetime or
        // alias assumptions are made here.
        //
        // Gloin currently reports both finite failures as the same fatal trap.
        // If failure diagnostics become operation-specific, this rule must also
        // retain which source operation failed. The arithmetic locations remain
        // attached to their respective operations.
        rewriter.setInsertionPoint(product);
        // The public lowering boundary also accepts mixed GIR/LLVM input. If the
        // other addend is poison, the original product could still trap before
        // consuming it. Freeze that addend so poison cannot hide this failure.
        // Concrete floating values, including NaNs and infinities, are unchanged.
        const unsigned other = sum.getLhs() == product.getResult() ? 1 : 0;
        auto frozen = rewriter.create<mlir::LLVM::FreezeOp>(
            sum.getLoc(), sum->getOperand(other));
        rewriter.modifyOpInPlace(sum, [&] { sum->setOperand(other, frozen.getResult()); });
        rewriter.replaceOpWithNewOp<mlir::arith::MulFOp>(
            product, product.getLhs(), product.getRhs());
        // Keep the checked addition even if its result has no users.
    });
}
