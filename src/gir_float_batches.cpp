#include "gir_float_batches.h"
#include "gir_float_checks.h"
#include "gir_private_float_storage.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"

namespace {

bool floating(mlir::Type type) { return type.isF32() || type.isF64(); }
bool scalar(mlir::Type type) { return floating(type) || mlir::isa<mlir::IntegerType>(type); }
mlir::Type element_type(mlir::Operation *op) {
    if (auto array = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op))
        return mlir::cast<mlir::LLVM::LLVMArrayType>(array.getArrayType()).getElementType();
    return mlir::cast<gloin::SliceElementAddressOp>(op).getElementType();
}
bool integer_is(mlir::Value value, uint64_t expected) {
    mlir::Attribute attr;
    if (auto op = value.getDefiningOp<gloin::ConstantOp>()) attr = op.getValueAttr();
    if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>()) attr = op.getValue();
    auto number = mlir::dyn_cast_or_null<mlir::IntegerAttr>(attr);
    return number && number.getValue() == expected;
}

bool supported_layout(mlir::func::FuncOp function) {
    bool restores = false;
    function.walk([&](mlir::LLVM::StackRestoreOp) { restores = true; });
    if (restores) return false;
    auto module = function->getParentOfType<mlir::ModuleOp>();
    auto attr = module->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
    auto layout = llvm::DataLayout::parse(attr ? attr.getValue() : "");
    if (!layout) { llvm::consumeError(layout.takeError()); return false; }
    llvm::LLVMContext context;
    return layout->getPointerSizeInBits() == 64 && layout->getIndexSizeInBits(0) == 64 &&
           layout->getTypeAllocSize(llvm::Type::getFloatTy(context)) == 4 &&
           layout->getTypeAllocSize(llvm::Type::getDoubleTy(context)) == 8;
}

// A bounded, single-entry loop with one latch and no internal cycles or exits.
// Only its induction variable is loop-carried. Branch diamonds within an
// iteration are allowed; unrelated SSA live-outs are deliberately rejected.
struct Batch {
    mlir::Block *header = nullptr, *latch = nullptr, *after = nullptr;
    mlir::cf::BranchOp entry, back;
    mlir::cf::CondBranchOp conditional_entry;
    mlir::cf::CondBranchOp test;
    gloin::CheckedIntegerCompareOp compare;
    gloin::CheckedIntegerBinaryOp increment;
    gloin::SliceLengthOp length;
    mlir::Operation *bound_constant = nullptr;
    mlir::Value index, bound;
    llvm::SmallVector<mlir::Block *> order;
    llvm::SmallVector<mlir::Operation *> accesses;
    llvm::SetVector<mlir::Value> external_scalars;

    mlir::Operation *entry_point() {
        return entry ? entry.getOperation() : conditional_entry.getOperation();
    }

    bool match(mlir::CFGLoop *loop, mlir::DominanceInfo &dominance) {
        if (!loop || !loop->getSubLoops().empty() || loop->getNumBlocks() > 32) return false;
        header = loop->getHeader();
        if (header->getNumArguments() != 1 || header->getOperations().size() > 3) return false;
        test = mlir::dyn_cast<mlir::cf::CondBranchOp>(header->getTerminator());
        if (!test || !test.getTrueDestOperands().empty() || !test.getFalseDestOperands().empty() ||
            !loop->contains(test.getTrueDest()) || loop->contains(test.getFalseDest())) return false;
        after = test.getFalseDest();
        index = header->getArgument(0);
        compare = test.getCondition().getDefiningOp<gloin::CheckedIntegerCompareOp>();
        if (!compare || compare->getBlock() != header || compare.getKind() != "<" ||
            compare.getLhs() != index || !index.getType().isInteger(64)) return false;
        unsigned predecessors = 0;
        for (auto *pred : header->getPredecessors()) {
            ++predecessors;
            if (loop->contains(pred)) latch = pred;
            else {
                entry = mlir::dyn_cast<mlir::cf::BranchOp>(pred->getTerminator());
                conditional_entry = mlir::dyn_cast<mlir::cf::CondBranchOp>(pred->getTerminator());
            }
        }
        if (predecessors != 2 || !latch) return false;
        if (entry) {
            if (entry.getDest() != header || !integer_is(entry.getDestOperands()[0], 0)) return false;
        } else {
            // Earlier slice versioning may already have made a fast/fallback
            // pair. Specialize its true edge only, preserving the existing
            // fallback and avoiding a second specialization of that fallback.
            if (!conditional_entry || conditional_entry.getTrueDest() != header ||
                conditional_entry.getFalseDest() == header ||
                !integer_is(conditional_entry.getTrueDestOperands()[0], 0)) return false;
        }
        back = mlir::dyn_cast<mlir::cf::BranchOp>(latch->getTerminator());
        if (!back || back.getDest() != header) return false;
        increment = back.getDestOperands()[0].getDefiningOp<gloin::CheckedIntegerBinaryOp>();
        if (!increment || increment->getBlock() != latch || increment.getKind() != "+" ||
            increment.getLhs() != index || !integer_is(increment.getRhs(), 1) ||
            increment.getIsSigned() != compare.getIsSigned()) return false;
        bound = compare.getRhs();
        length = bound.getDefiningOp<gloin::SliceLengthOp>();
        if (auto *op = bound.getDefiningOp(); op && op->getBlock() == header &&
            mlir::isa<gloin::ConstantOp, mlir::arith::ConstantOp>(op))
            bound_constant = op;
        if (!dominance.dominates(bound, entry_point()) &&
            !bound_constant &&
            !(length && length->getBlock() == header && dominance.dominates(length.getBase(), entry_point()))) return false;
        for (auto &op : *header)
            if (&op != test && &op != compare && &op != length && &op != bound_constant) return false;

        // Topologically sort after removing the sole latch-to-header edge.
        // This both proves termination after a failed float check and gives a
        // definition-before-use order for cloning values across branch joins.
        llvm::DenseMap<mlir::Block *, unsigned> degrees;
        unsigned operations = 0;
        for (auto *block : loop->getBlocks()) {
            operations += block->getOperations().size();
            if (operations > 256) return false;
            auto *term = block->getTerminator();
            if (!mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(term)) return false;
            degrees[block] = 0;
            for (auto *pred : block->getPredecessors()) {
                if (block == header) continue;
                if (!loop->contains(pred)) return false;
                ++degrees[block];
            }
            for (auto *successor : block->getSuccessors()) {
                if (block == header && successor == after) continue;
                if (!loop->contains(successor) || (successor == header && block != latch)) return false;
            }
            for (auto arg : block->getArguments()) {
                if (!scalar(arg.getType())) return false;
                for (auto *user : arg.getUsers()) if (!loop->contains(user->getBlock())) return false;
            }
            for (auto &op : *block) for (auto value : op.getResults())
                for (auto *user : value.getUsers()) if (!loop->contains(user->getBlock())) return false;
        }
        order.push_back(header);
        for (unsigned i = 0; i != order.size(); ++i)
            for (auto *successor : order[i]->getSuccessors()) {
                if (successor == header || successor == after) continue;
                if (--degrees[successor] == 0) order.push_back(successor);
            }
        if (order.size() != loop->getNumBlocks()) return false;

        gloin_detail::LocalStorage storage(dominance, entry_point());
        llvm::SmallPtrSet<mlir::Operation *, 32> allowed_accesses;
        unsigned checks = 0;
        for (auto *block : order) for (auto &op : *block) {
            if (auto access = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op)) {
                auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(access.getArrayType());
                if (access.getIndex() != index || !floating(array.getElementType()) ||
                    !storage.pointer(access.getBase(), array)) return false;
                accesses.push_back(&op); allowed_accesses.insert(&op);
            } else if (auto access = mlir::dyn_cast<gloin::SliceElementAddressOp>(op)) {
                if (access.getIndex() != index || !floating(access.getElementType()) ||
                    !storage.slice(access.getBase(), access.getElementType())) return false;
                accesses.push_back(&op); allowed_accesses.insert(&op);
            }
        }
        if (accesses.empty()) return false;
        for (auto *block : order) for (auto &op : *block) {
            for (auto input : op.getOperands()) {
                auto *owner = input.getParentBlock();
                if (scalar(input.getType()) && !loop->contains(owner)) {
                    if (!dominance.dominates(input, entry_point())) return false;
                    external_scalars.insert(input);
                }
            }
            if (&op == increment || &op == length || allowed_accesses.contains(&op)) continue;
            if (auto load = mlir::dyn_cast<gloin::LoadOp>(op)) {
                if (!floating(load.getType()) || !allowed_accesses.contains(load.getAddress().getDefiningOp())) return false;
                auto type = element_type(load.getAddress().getDefiningOp());
                if (load.getType() != type || load.getLayoutType() != type) return false;
            } else if (auto store = mlir::dyn_cast<gloin::StoreOp>(op)) {
                if (!floating(store.getValue().getType()) || !allowed_accesses.contains(store.getAddress().getDefiningOp())) return false;
                auto type = element_type(store.getAddress().getDefiningOp());
                if (store.getValue().getType() != type) return false;
            } else if (auto binary = mlir::dyn_cast<gloin::CheckedFloatBinaryOp>(op)) {
                if (!floating(binary.getType()) || (binary.getKind() != "+" && binary.getKind() != "-" && binary.getKind() != "*")) return false;
                ++checks;
            } else if (mlir::isa<mlir::arith::AddFOp, mlir::arith::SubFOp, mlir::arith::MulFOp,
                                 mlir::arith::NegFOp, mlir::arith::CmpFOp>(op)) {
                auto flags = op.getAttrOfType<mlir::arith::FastMathFlagsAttr>("fastmath");
                if (flags && flags.getValue() != mlir::arith::FastMathFlags::none) return false;
                for (auto type : op.getOperandTypes()) if (!floating(type)) return false;
            } else if (mlir::isa<mlir::arith::AndIOp, mlir::arith::OrIOp, mlir::arith::XOrIOp>(op)) {
                // Only boolean logic; do not admit hidden integer flags/UB.
                if (!op.getResult(0).getType().isInteger(1)) return false;
            } else if (auto select = mlir::dyn_cast<mlir::arith::SelectOp>(op)) {
                if (!scalar(select.getType())) return false;
            } else if (auto freeze = mlir::dyn_cast<mlir::LLVM::FreezeOp>(op)) {
                if (!scalar(freeze.getType())) return false;
            } else if (!mlir::isa<gloin::ConstantOp, mlir::arith::ConstantOp,
                                gloin::CheckedFloatCompareOp, gloin::CheckedIntegerCompareOp,
                                mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) return false;
        }
        return checks != 0;
    }

    llvm::SmallPtrSet<mlir::Operation *, 32> covered_checks(
            const llvm::SmallPtrSetImpl<mlir::Operation *> &safe_checks) {
        llvm::SmallPtrSet<mlir::Operation *, 32> removable;
        for (auto *block : order) {
            llvm::DenseSet<mlir::Value> covered;
            // A retained predicate is a mandatory sink in this block. Walk
            // backwards through operations that propagate every non-finite
            // operand to their result. Multiple uses and intervening private
            // memory operations are harmless: batch matching already proves
            // their safety and lack of observable effects before the exit.
            //
            // Keep the proof local to one block; a conditional consumer, phi,
            // select, or reload is not evidence that a failure reaches a sink.
            // Never seed coverage from a range-proven check that emits no flag:
            // its finite fact may itself depend on an earlier checked result.
            for (auto &op : llvm::reverse(*block)) {
                if (op.getNumResults() != 1 || !floating(op.getResult(0).getType())) continue;
                auto result = op.getResult(0);
                if (mlir::isa<gloin::CheckedFloatBinaryOp>(op) && !safe_checks.contains(&op)) {
                    if (covered.contains(result)) removable.insert(&op);
                    else covered.insert(result);
                }
                if (!covered.contains(result)) continue;
                if (mlir::isa<gloin::CheckedFloatBinaryOp, mlir::arith::AddFOp,
                              mlir::arith::SubFOp, mlir::arith::MulFOp,
                              mlir::arith::NegFOp, mlir::LLVM::FreezeOp>(op)) {
                    // Batch matching excludes fast-math and other checked
                    // operators. External inputs and loads are frozen in the
                    // fast copy, and supported internal arithmetic cannot
                    // create poison. Freeze therefore preserves non-finiteness.
                    for (auto operand : op.getOperands()) covered.insert(operand);
                }
            }
        }
        return removable;
    }

    unsigned interleave_short_batch() {
        mlir::Attribute attr;
        if (auto op = bound.getDefiningOp<gloin::ConstantOp>()) attr = op.getValueAttr();
        if (auto op = bound.getDefiningOp<mlir::arith::ConstantOp>()) attr = op.getValue();
        auto number = mlir::dyn_cast_or_null<mlir::IntegerAttr>(attr);
        if (!number) return 0;
        const uint64_t count = number.getValue().getZExtValue();
        // A profitability hint, not a dependence or arithmetic proof. Keep
        // tiny and dynamic loops under LLVM's ordinary cost model, and bound
        // code growth even within this already bounded matcher.
        if ((count != 8 && count < 16) || count > 32) return 0;
        uint64_t operations = 0;
        for (auto *block : order) operations += block->getOperations().size();
        if (count * operations > 4096) return 0;
        // Eight elements need two groups on the measured AVX2 f64 target.
        // Avoid imposing four groups and unnecessarily reducing vector width.
        return count == 8 ? 2 : 4;
    }

    void rewrite(mlir::func::FuncOp function, GloinRangeProofs &proofs,
                 const llvm::SmallPtrSetImpl<mlir::Operation *> &safe_checks) {
        const auto removable_checks = covered_checks(safe_checks);
        const unsigned interleave = interleave_short_batch();
        mlir::IRRewriter rewriter(function.getContext());
        mlir::IRMapping mapping;
        if (!entry) {
            auto *gate = new mlir::Block;
            function.getBody().getBlocks().insert(header->getIterator(), gate);
            rewriter.setInsertionPointToEnd(gate);
            entry = rewriter.create<mlir::cf::BranchOp>(conditional_entry.getLoc(), header,
                                                       conditional_entry.getTrueDestOperands());
            rewriter.setInsertionPoint(conditional_entry);
            rewriter.create<mlir::cf::CondBranchOp>(conditional_entry.getLoc(),
                conditional_entry.getCondition(), gate, mlir::ValueRange{},
                conditional_entry.getFalseDest(), conditional_entry.getFalseDestOperands());
            conditional_entry.erase();
        }
        const auto loc = entry.getLoc();
        rewriter.setInsertionPoint(entry);
        // All new work at entry is non-faulting descriptor/SSA work. A failed
        // unsigned capacity guard selects the unchanged original loop, including
        // negative signed limits, partial progress, and original trap order.
        if (length && length->getBlock() == header)
            bound = rewriter.create<gloin::SliceLengthOp>(loc, bound.getType(), length.getBase(),
                                                        length.getSourceType(), length.getLayoutType());
        if (bound_constant) bound = rewriter.clone(*bound_constant)->getResult(0);
        auto frozen_bound = rewriter.create<mlir::LLVM::FreezeOp>(loc, bound);
        mapping.map(compare.getRhs(), frozen_bound.getResult());
        for (auto value : external_scalars)
            if (!mapping.contains(value)) mapping.map(value, rewriter.create<mlir::LLVM::FreezeOp>(loc, value).getResult());
        mlir::Value fits;
        for (auto *access : accesses) {
            mlir::Value capacity;
            if (auto array = mlir::dyn_cast<gloin::ArrayElementAddressOp>(access))
                capacity = rewriter.create<mlir::arith::ConstantIntOp>(loc,
                    mlir::cast<mlir::LLVM::LLVMArrayType>(array.getArrayType()).getNumElements(), 64);
            else {
                auto slice = mlir::cast<gloin::SliceElementAddressOp>(access);
                capacity = rewriter.create<gloin::SliceLengthOp>(loc, rewriter.getI64Type(),
                    slice.getBase(), slice.getSourceType(), slice.getLayoutType());
            }
            auto enough = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule, frozen_bound, capacity);
            if (fits) fits = rewriter.create<mlir::arith::AndIOp>(loc, fits, enough);
            else fits = enough;
        }
        auto valid_entry = rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 1);
        for (auto *original : order) {
            auto *copy = new mlir::Block;
            function.getBody().getBlocks().insert(header->getIterator(), copy);
            mapping.map(original, copy);
            for (auto arg : original->getArguments()) mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
            copy->addArgument(rewriter.getI1Type(), loc);
        }
        auto *checked_exit = new mlir::Block;
        function.getBody().getBlocks().insert(after->getIterator(), checked_exit);
        checked_exit->addArgument(rewriter.getI1Type(), loc);
        rewriter.setInsertionPointToEnd(checked_exit);
        rewriter.create<gloin::AssertOp>(loc, checked_exit->getArgument(0));
        rewriter.create<mlir::cf::BranchOp>(loc, after);

        for (auto *original : order) {
            auto *copy = mapping.lookup(original);
            rewriter.setInsertionPointToEnd(copy);
            mlir::Value valid = copy->getArguments().back();
            for (auto &op : *original) {
                if ((&op == length && length->getBlock() == header) || &op == bound_constant) continue;
                if (auto binary = mlir::dyn_cast<gloin::CheckedFloatBinaryOp>(op)) {
                    auto lhs = mapping.lookupOrDefault(binary.getLhs()), rhs = mapping.lookupOrDefault(binary.getRhs());
                    mlir::Value value;
                    if (binary.getKind() == "+") value = rewriter.create<mlir::arith::AddFOp>(op.getLoc(), lhs, rhs);
                    else if (binary.getKind() == "-") value = rewriter.create<mlir::arith::SubFOp>(op.getLoc(), lhs, rhs);
                    else value = rewriter.create<mlir::arith::MulFOp>(op.getLoc(), lhs, rhs);
                    mapping.map(binary.getResult(), value);
                    // Scalar profitability kept some provably safe checks in
                    // conditional paths. The batch already defers its effects:
                    // those checks need neither a branch nor a flag update.
                    if (safe_checks.contains(&op) || removable_checks.contains(&op)) continue;
                    auto type = mlir::cast<mlir::FloatType>(value.getType());
                    auto largest = llvm::APFloat::getLargest(type.getFloatSemantics());
                    auto upper = rewriter.create<mlir::arith::ConstantOp>(op.getLoc(), type, rewriter.getFloatAttr(type, largest));
                    largest.changeSign();
                    auto lower = rewriter.create<mlir::arith::ConstantOp>(op.getLoc(), type, rewriter.getFloatAttr(type, largest));
                    auto lo = rewriter.create<mlir::arith::CmpFOp>(op.getLoc(), mlir::arith::CmpFPredicate::OGE, value, lower);
                    auto hi = rewriter.create<mlir::arith::CmpFOp>(op.getLoc(), mlir::arith::CmpFPredicate::OLE, value, upper);
                    auto finite = rewriter.create<mlir::arith::AndIOp>(op.getLoc(), lo, hi);
                    valid = rewriter.create<mlir::arith::AndIOp>(op.getLoc(), valid, finite);
                } else if (auto branch = mlir::dyn_cast<mlir::cf::BranchOp>(op)) {
                    llvm::SmallVector<mlir::Value> args;
                    for (auto value : branch.getDestOperands()) args.push_back(mapping.lookupOrDefault(value));
                    args.push_back(valid);
                    auto cloned_branch = rewriter.create<mlir::cf::BranchOp>(op.getLoc(), mapping.lookup(branch.getDest()), args);
                    if (&op == back && interleave) {
                        // Interleave vector iterations rather than unrolling
                        // scalar IR before LLVM gets a chance to vectorize it.
                        // LLVM still proves dependence safety, chooses a vector
                        // width, and handles scalar tails. No fast-math,
                        // parallel-access, or vectorize-enable claim is added.
                        auto grouping = mlir::LLVM::LoopInterleaveAttr::get(function.getContext(),
                            rewriter.getI32IntegerAttr(interleave));
                        auto annotation = mlir::LLVM::LoopAnnotationAttr::get(function.getContext(), {}, {}, grouping,
                            {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {});
                        cloned_branch->setAttr("loop_annotation", annotation);
                    }
                } else if (auto branch = mlir::dyn_cast<mlir::cf::CondBranchOp>(op)) {
                    llvm::SmallVector<mlir::Value> yes, no;
                    for (auto value : branch.getTrueDestOperands()) yes.push_back(mapping.lookupOrDefault(value));
                    for (auto value : branch.getFalseDestOperands()) no.push_back(mapping.lookupOrDefault(value));
                    yes.push_back(valid); no.push_back(valid);
                    rewriter.create<mlir::cf::CondBranchOp>(op.getLoc(), mapping.lookupOrDefault(branch.getCondition()),
                        mapping.lookup(branch.getTrueDest()), yes,
                        original == header ? checked_exit : mapping.lookup(branch.getFalseDest()), no);
                } else {
                    auto *cloned = rewriter.clone(op, mapping);
                    if (&op == increment || proofs.arithmetic.contains(&op)) proofs.arithmetic.insert(cloned);
                    if (proofs.bounds.contains(&op) || llvm::is_contained(accesses, &op)) proofs.bounds.insert(cloned);
                    if (auto load = mlir::dyn_cast<gloin::LoadOp>(cloned)) {
                        auto frozen = rewriter.create<mlir::LLVM::FreezeOp>(op.getLoc(), load.getValue());
                        mapping.map(op.getResult(0), frozen.getResult());
                    }
                }
            }
        }
        rewriter.setInsertionPoint(entry);
        llvm::SmallVector<mlir::Value> args(entry.getDestOperands());
        args.push_back(valid_entry);
        rewriter.create<mlir::cf::CondBranchOp>(loc, fits, mapping.lookup(header), args, header, entry.getDestOperands());
        entry.erase();
    }
};
} // namespace

void version_gloin_float_batches(mlir::ModuleOp module, GloinRangeProofs &proofs) {
    for (auto function : module.getOps<mlir::func::FuncOp>()) {
        if (function.isExternal() || function.getBody().hasOneBlock()) continue;
        bool checked = false;
        function.walk([&](gloin::CheckedFloatBinaryOp) { checked = true; });
        if (!checked || !supported_layout(function)) continue;
        llvm::SmallVector<mlir::Block *> headers;
        {
            mlir::DominanceInfo dominance(function);
            mlir::CFGLoopInfo loops(dominance.getDomTree(&function.getBody()));
            for (auto &block : function.getBody())
                if (auto *loop = loops.getLoopFor(&block); loop && loop->getHeader() == &block && loop->getSubLoops().empty())
                    headers.push_back(&block);
        }
        llvm::SmallPtrSet<mlir::Operation *, 32> safe_checks;
        bool collected = false;
        for (auto *header : headers) {
            if (proofs.versioned_reductions.contains(header)) continue;
            mlir::DominanceInfo dominance(function);
            mlir::CFGLoopInfo loops(dominance.getDomTree(&function.getBody()));
            Batch batch;
            if (batch.match(loops.getLoopFor(header), dominance)) {
                // Recompute AFTER expression combining: it may have moved the
                // mandatory check that anchored an earlier finite-value fact.
                if (!collected) { find_proven_gloin_float_checks(function, safe_checks); collected = true; }
                batch.rewrite(function, proofs, safe_checks);
            }
        }
    }
}
