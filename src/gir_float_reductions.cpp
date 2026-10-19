#include "gir_float_reductions.h"
#include "gir_range_proofs.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>

namespace {
mlir::Attribute constant(mlir::Value value) {
    if (auto op = value.getDefiningOp<gloin::ConstantOp>()) return op.getValueAttr();
    if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>()) return op.getValue();
    return {};
}
bool integer_is(mlir::Value value, uint64_t expected) {
    auto attr = mlir::dyn_cast_or_null<mlir::IntegerAttr>(constant(value));
    return attr && attr.getValue() == expected;
}
bool zero_float(mlir::Value value) {
    auto attr = mlir::dyn_cast_or_null<mlir::FloatAttr>(constant(value));
    return attr && attr.getValue().isZero();
}

// Our native targets use 64-bit addresses. Do not infer valid storage from a
// type whose nested extent could wrap allocation/address calculations. This
// bound is deliberately stricter than unsigned pointer-size representability.
std::optional<uint64_t> storage_bytes(mlir::Type type, unsigned depth = 0) {
    if (type.isF32()) return 4;
    if (type.isF64()) return 8;
    auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(type);
    if (!array || depth > 8) return std::nullopt;
    auto element = storage_bytes(array.getElementType(), depth + 1);
    if (!element || (*element && array.getNumElements() > uint64_t(INT64_MAX) / *element))
        return std::nullopt;
    return array.getNumElements() * *element;
}

// A descriptor length says nothing about the underlying allocation. Recover
// storage from a real local allocation, require a dominating whole initializer,
// and audit every derived address/descriptor use. Unknown uses fail closed.
// This also rules out hidden deallocation, volatile/atomic reads and aliases
// passed to calls. Two accepted inputs may freely overlap: neither is written.
class LocalStorage {
    mlir::DominanceInfo &dominance;
    mlir::Operation *entry;
    llvm::DenseMap<mlir::Value, bool> roots;

    bool confined(mlir::Value root) {
        llvm::SmallVector<mlir::Value> work{root};
        llvm::DenseSet<mlir::Value> seen;
        while (!work.empty()) {
            auto value = work.pop_back_val();
            if (!seen.insert(value).second) continue;
            if (seen.size() > 256) return false;
            for (auto &use : value.getUses()) {
                auto *op = use.getOwner();
                if (auto store = mlir::dyn_cast<gloin::StoreOp>(op)) {
                    if (store.getAddress() != value || store.getValue() == value) return false;
                } else if (auto load = mlir::dyn_cast<gloin::LoadOp>(op)) {
                    if (load.getAddress() != value) return false;
                } else if (auto fill = mlir::dyn_cast<gloin::ArrayFillOp>(op)) {
                    if (fill.getAddress() != value || fill.getElement() == value) return false;
                } else if (auto element = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op)) {
                    if (element.getBase() != value) return false;
                    work.push_back(element.getAddress());
                } else if (auto slice = mlir::dyn_cast<gloin::SliceFromArrayOp>(op)) {
                    if (slice.getBase() != value) return false;
                    work.push_back(slice.getValue());
                } else if (auto slice = mlir::dyn_cast<gloin::SliceSubrangeOp>(op)) {
                    if (slice.getBase() != value) return false;
                    work.push_back(slice.getValue());
                } else if (auto element = mlir::dyn_cast<gloin::SliceElementAddressOp>(op)) {
                    if (element.getBase() != value) return false;
                    work.push_back(element.getAddress());
                } else if (!mlir::isa<gloin::SliceLengthOp>(op)) {
                    return false;
                }
            }
        }
        return true;
    }

public:
    LocalStorage(mlir::DominanceInfo &dominance, mlir::Operation *entry)
        : dominance(dominance), entry(entry) {}

    bool pointer(mlir::Value value, mlir::Type type, unsigned depth = 0) {
        if (depth > 8 || !dominance.dominates(value, entry)) return false;
        if (auto element = value.getDefiningOp<gloin::ArrayElementAddressOp>()) {
            auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(element.getArrayType());
            return array.getElementType() == type &&
                   pointer(element.getBase(), array, depth + 1);
        }
        auto allocation = value.getDefiningOp<gloin::StackAllocOp>();
        if (!allocation || allocation.getElementType() != type ||
            !mlir::isa<mlir::LLVM::LLVMArrayType>(type) || !storage_bytes(type)) return false;
        auto address = mlir::dyn_cast<mlir::LLVM::LLVMPointerType>(value.getType());
        if (!address || address.getAddressSpace() != 0) return false;
        if (auto found = roots.find(value); found != roots.end()) return found->second;
        bool initialized = false;
        for (auto *user : value.getUsers()) {
            if (!dominance.dominates(user, entry)) continue;
            if (auto store = mlir::dyn_cast<gloin::StoreOp>(user))
                initialized |= store.getAddress() == value && store.getValue().getType() == type;
            if (auto fill = mlir::dyn_cast<gloin::ArrayFillOp>(user))
                initialized |= fill.getAddress() == value && fill.getLayoutType() == type;
        }
        return roots[value] = initialized && confined(value);
    }

    bool slice(mlir::Value value, mlir::Type element, unsigned depth = 0) {
        if (depth > 8 || !dominance.dominates(value, entry)) return false;
        if (auto sub = value.getDefiningOp<gloin::SliceSubrangeOp>())
            return sub.getElementType() == element && slice(sub.getBase(), element, depth + 1);
        auto from = value.getDefiningOp<gloin::SliceFromArrayOp>();
        if (!from) return false;
        auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(from.getArrayType());
        return array.getElementType() == element && pointer(from.getBase(), array);
    }
};

bool private_accumulator(gloin::LoadOp load, gloin::StoreOp update,
                         mlir::Operation *entry, mlir::DominanceInfo &dominance) {
    auto allocation = load.getAddress().getDefiningOp<gloin::StackAllocOp>();
    if (!allocation || allocation.getElementType() != load.getValue().getType() ||
        update.getAddress() != load.getAddress() || !dominance.dominates(allocation.getOperation(), entry))
        return false;
    bool initialized = false;
    for (auto *user : allocation.getAddress().getUsers()) {
        if (mlir::isa<gloin::LoadOp>(user)) continue;
        auto store = mlir::dyn_cast<gloin::StoreOp>(user);
        if (!store || store.getAddress() != allocation.getAddress()) return false;
        if (store == update) continue;
        // Every other write is zero; a dominating one initializes the first
        // iteration. The loop body's whitelist admits only its single update.
        if (!zero_float(store.getValue())) return false;
        initialized |= dominance.dominates(store, entry);
    }
    return initialized;
}
} // namespace

void version_gloin_float_reductions(mlir::func::FuncOp function, GloinRangeProofs &proofs) {
    if (function.getBody().getBlocks().size() < 3) return;
    // Mixed LLVM input can end stack allocation lifetimes without using any
    // of their addresses. Address confinement alone cannot rule this out.
    bool restores_stack = false;
    function.walk([&](mlir::LLVM::StackRestoreOp) { restores_stack = true; });
    if (restores_stack) return;
    // Honor explicit mixed-IR layouts as well as our native 64-bit defaults.
    // The byte-extent proof below requires ordinary f32/f64 allocation strides.
    auto module = function->getParentOfType<mlir::ModuleOp>();
    auto attribute = module->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
    auto layout = llvm::DataLayout::parse(attribute ? attribute.getValue() : "");
    if (!layout) { llvm::consumeError(layout.takeError()); return; }
    llvm::LLVMContext llvm_context;
    if (layout->getPointerSizeInBits() != 64 || layout->getIndexSizeInBits(0) != 64 ||
        layout->getTypeAllocSize(llvm::Type::getFloatTy(llvm_context)) != 4 ||
        layout->getTypeAllocSize(llvm::Type::getDoubleTy(llvm_context)) != 8) return;
    mlir::DominanceInfo dominance(function);
    llvm::SmallVector<mlir::Block *> headers;
    for (auto &block : function.getBody()) headers.push_back(&block);
    for (auto *header : headers) {
        if (header->getNumArguments() != 1 || header->getOperations().size() > 3) continue;
        auto branch = mlir::dyn_cast<mlir::cf::CondBranchOp>(header->getTerminator());
        if (!branch || branch.getTrueDestOperands().size() || branch.getFalseDestOperands().size()) continue;
        auto compare = branch.getCondition().getDefiningOp<gloin::CheckedIntegerCompareOp>();
        auto index = header->getArgument(0);
        if (!compare || compare->getBlock() != header || compare.getKind() != "<" ||
            compare.getLhs() != index || !index.getType().isInteger(64)) continue;
        auto *body = branch.getTrueDest();
        auto *after = branch.getFalseDest();
        if (body == header || after == header || after == body ||
            body->getSinglePredecessor() != header || body->getNumArguments() ||
            body->getOperations().size() > 48) continue;
        auto back = mlir::dyn_cast<mlir::cf::BranchOp>(body->getTerminator());
        if (!back || back.getDest() != header) continue;
        mlir::Block *entry = nullptr;
        unsigned predecessors = 0;
        for (auto *pred : header->getPredecessors()) {
            ++predecessors;
            if (pred != body) entry = pred;
        }
        if (predecessors != 2 || !entry || entry == header) continue;
        auto incoming = mlir::dyn_cast<mlir::cf::BranchOp>(entry->getTerminator());
        if (!incoming || incoming.getDest() != header || !integer_is(incoming.getDestOperands()[0], 0)) continue;
        auto increment = back.getDestOperands()[0].getDefiningOp<gloin::CheckedIntegerBinaryOp>();
        if (!increment || increment->getBlock() != body || increment.getKind() != "+" ||
            increment.getIsSigned() != compare.getIsSigned() || increment.getLhs() != index ||
            !integer_is(increment.getRhs(), 1)) continue;
        auto bound = compare.getRhs();
        auto length = bound.getDefiningOp<gloin::SliceLengthOp>();
        if (!dominance.dominates(bound, incoming) &&
            !(length && length->getBlock() == header && dominance.dominates(length.getBase(), incoming))) continue;
        bool supported = true;
        for (auto &op : *header)
            if (&op != branch && &op != compare && &op != length) supported = false;
        // No counter/body SSA value may escape: the accumulator is a proven
        // private slot whose final load remains after the mandatory check.
        for (auto *user : index.getUsers())
            if (user->getBlock() != header && user->getBlock() != body) supported = false;
        for (auto *block : {header, body})
            for (auto &op : *block)
                for (auto value : op.getResults())
                    for (auto *user : value.getUsers())
                        if (user->getBlock() != header && user->getBlock() != body) supported = false;
        if (!supported) continue;

        gloin::CheckedFloatBinaryOp product, sum;
        for (auto op : body->getOps<gloin::CheckedFloatBinaryOp>()) {
            if (op.getKind() == "*" && !product) product = op;
            else if (op.getKind() == "+" && !sum) sum = op;
            else supported = false;
        }
        if (!supported || !product || !sum || !product.getResult().hasOneUse() ||
            !sum.getResult().hasOneUse()) continue;
        const auto type = sum.getResult().getType();
        if ((!type.isF32() && !type.isF64()) || product.getResult().getType() != type) continue;
        auto accumulator = (sum.getLhs() == product.getResult() ? sum.getRhs() :
                            sum.getRhs() == product.getResult() ? sum.getLhs() : mlir::Value{});
        auto load = accumulator ? accumulator.getDefiningOp<gloin::LoadOp>() : gloin::LoadOp{};
        auto update = mlir::dyn_cast<gloin::StoreOp>(*sum.getResult().getUsers().begin());
        if (!load || load->getBlock() != body || !load.getValue().hasOneUse() || !update ||
            update.getValue() != sum.getResult() || !private_accumulator(load, update, incoming, dominance)) continue;

        LocalStorage storage(dominance, incoming);
        llvm::SmallPtrSet<mlir::Operation *, 16> allowed{product, sum, load, update, increment, back};
        llvm::SmallVector<mlir::Operation *> accesses;
        for (auto operand : {product.getLhs(), product.getRhs()}) {
            auto input = operand.getDefiningOp<gloin::LoadOp>();
            if (!input || input == load || input->getBlock() != body) { supported = false; break; }
            for (auto *user : input.getValue().getUsers())
                if (user != product) supported = false;
            allowed.insert(input);
            if (auto access = input.getAddress().getDefiningOp<gloin::ArrayElementAddressOp>()) {
                auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(access.getArrayType());
                if (access->getBlock() != body || access.getIndex() != index ||
                    array.getElementType() != type || !storage.pointer(access.getBase(), array)) supported = false;
                accesses.push_back(access);
                allowed.insert(access);
            } else if (auto access = input.getAddress().getDefiningOp<gloin::SliceElementAddressOp>()) {
                if (access->getBlock() != body || access.getIndex() != index ||
                    access.getElementType() != type || !storage.slice(access.getBase(), type)) supported = false;
                accesses.push_back(access);
                allowed.insert(access);
            } else supported = false;
        }
        // Read-only is insufficient. Only the proven reads, the recurrence,
        // private update and non-overflowing counter can execute after a failure.
        for (auto &op : *body)
            if (!allowed.contains(&op) && !mlir::isa<gloin::ConstantOp, mlir::arith::ConstantOp>(op)) supported = false;
        if (!supported) continue;

        mlir::IRMapping mapping;
        for (auto *original : {header, body}) {
            auto *copy = new mlir::Block;
            function.getBody().getBlocks().insert(header->getIterator(), copy);
            mapping.map(original, copy);
            for (auto arg : original->getArguments()) mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
        }
        mlir::IRRewriter rewriter(function.getContext());
        for (auto *original : {header, body}) {
            rewriter.setInsertionPointToEnd(mapping.lookup(original));
            for (auto &op : *original) {
                auto *copy = rewriter.clone(op, mapping);
                if (proofs.arithmetic.contains(&op)) proofs.arithmetic.insert(copy);
                if (proofs.bounds.contains(&op)) proofs.bounds.insert(copy);
            }
        }
        for (auto *access : accesses) proofs.bounds.insert(mapping.lookup(access));
        proofs.arithmetic.insert(mapping.lookup(increment.getOperation()));
        auto fast_product = mlir::cast<gloin::CheckedFloatBinaryOp>(mapping.lookup(product.getOperation()));
        auto fast_sum = mlir::cast<gloin::CheckedFloatBinaryOp>(mapping.lookup(sum.getOperation()));
        rewriter.setInsertionPoint(fast_product);
        // A later poison/undef input must not erase an earlier arithmetic trap.
        // Freezing each loaded operand preserves concrete data; it does not
        // justify the read. Storage validity was established independently.
        auto lhs = rewriter.create<mlir::LLVM::FreezeOp>(product.getLoc(), fast_product.getLhs());
        auto rhs = rewriter.create<mlir::LLVM::FreezeOp>(product.getLoc(), fast_product.getRhs());
        rewriter.replaceOpWithNewOp<mlir::arith::MulFOp>(fast_product, lhs, rhs);
        rewriter.setInsertionPoint(fast_sum);
        rewriter.replaceOpWithNewOp<mlir::arith::AddFOp>(fast_sum, fast_sum.getLhs(), fast_sum.getRhs());

        auto *checked_exit = new mlir::Block;
        function.getBody().getBlocks().insert(after->getIterator(), checked_exit);
        rewriter.setInsertionPointToEnd(checked_exit);
        auto final_value = rewriter.create<gloin::LoadOp>(sum.getLoc(), type, load.getAddress(),
                                                       load.getSourceType(), load.getLayoutType());
        rewriter.create<gloin::RequireFiniteOp>(sum.getLoc(), final_value);
        rewriter.create<mlir::cf::BranchOp>(sum.getLoc(), after);
        auto fast_branch = mlir::cast<mlir::cf::CondBranchOp>(mapping.lookup(header)->getTerminator());
        fast_branch->setSuccessor(checked_exit, 1);

        rewriter.setInsertionPoint(incoming);
        if (length && length->getBlock() == header)
            bound = rewriter.create<gloin::SliceLengthOp>(length.getLoc(), length.getLength().getType(),
                length.getBase(), length.getSourceType(), length.getLayoutType());
        mlir::Value fits;
        for (auto *op : accesses) {
            mlir::Value capacity;
            if (auto array = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op))
                capacity = rewriter.create<mlir::arith::ConstantIntOp>(incoming.getLoc(),
                    mlir::cast<mlir::LLVM::LLVMArrayType>(array.getArrayType()).getNumElements(), 64);
            else {
                auto slice = mlir::cast<gloin::SliceElementAddressOp>(op);
                capacity = rewriter.create<gloin::SliceLengthOp>(incoming.getLoc(), rewriter.getI64Type(),
                    slice.getBase(), slice.getSourceType(), slice.getLayoutType());
            }
            auto enough = rewriter.create<mlir::arith::CmpIOp>(incoming.getLoc(),
                mlir::arith::CmpIPredicate::ule, bound, capacity);
            if (fits) fits = rewriter.create<mlir::arith::AndIOp>(incoming.getLoc(), fits, enough);
            else fits = enough;
        }
        rewriter.create<mlir::cf::CondBranchOp>(incoming.getLoc(), fits, mapping.lookup(header),
            incoming.getDestOperands(), header, incoming.getDestOperands());
        incoming.erase();
        proofs.versioned_reductions.insert(header);
        proofs.versioned_reductions.insert(mapping.lookup(header));
        dominance.invalidate();
    }
}
