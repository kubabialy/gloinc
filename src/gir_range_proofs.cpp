#include "gir_range_proofs.h"
#include "gir_float_reductions.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/Mem2Reg.h"
#include <algorithm>
#include <limits>

namespace {
// Use MLIR's SSA construction, including joins and loop backedges. Only exact
// loads/stores participate: taking an address, passing it to a call, or deriving
// an alias blocks promotion. Consequently facts about an SSA snapshot can never
// accidentally become facts about a later load through a mutable alias.
struct PromoteAllocation : mlir::PromotableAllocationOpInterface::ExternalModel<
                               PromoteAllocation, gloin::StackAllocOp> {
    llvm::SmallVector<mlir::MemorySlot> getPromotableSlots(mlir::Operation *op) const {
        auto allocation = mlir::cast<gloin::StackAllocOp>(op);
        auto type = allocation.getElementType();
        if (!mlir::isa<mlir::LLVM::LLVMPointerType>(allocation.getAddress().getType()) ||
            (!mlir::isa<mlir::IntegerType>(type) &&
             !mlir::isa<gloin::GloinSliceType>(allocation.getSourceType())))
            return {};
        return {{allocation.getAddress(), type}};
    }
    mlir::Value getDefaultValue(mlir::Operation *op, const mlir::MemorySlot &slot,
                                mlir::OpBuilder &builder) const {
        // Checked source requires definite initialization. For arbitrary IR,
        // preserve the undefined contents of an uninitialized stack slot.
        return builder.create<mlir::LLVM::UndefOp>(op->getLoc(), slot.elemType);
    }
    void handleBlockArgument(mlir::Operation *, const mlir::MemorySlot &,
                              mlir::BlockArgument, mlir::OpBuilder &) const {}
    std::optional<mlir::PromotableAllocationOpInterface> handlePromotionComplete(
        mlir::Operation *op, const mlir::MemorySlot &, mlir::Value initial,
        mlir::OpBuilder &) const {
        op->erase();
        if (initial.use_empty()) initial.getDefiningOp()->erase();
        return std::nullopt;
    }
};

template <typename Op, bool IsLoad>
struct PromoteMemory : mlir::PromotableMemOpInterface::ExternalModel<
                           PromoteMemory<Op, IsLoad>, Op> {
    bool loadsFrom(mlir::Operation *op, const mlir::MemorySlot &slot) const {
        return IsLoad && mlir::cast<Op>(op).getAddress() == slot.ptr;
    }
    bool storesTo(mlir::Operation *op, const mlir::MemorySlot &slot) const {
        return !IsLoad && mlir::cast<Op>(op).getAddress() == slot.ptr;
    }
    mlir::Value getStored(mlir::Operation *op, const mlir::MemorySlot &,
                          mlir::OpBuilder &, mlir::Value, const mlir::DataLayout &) const {
        if constexpr (!IsLoad) return mlir::cast<Op>(op).getValue();
        return {};
    }
    bool canUsesBeRemoved(mlir::Operation *op, const mlir::MemorySlot &slot,
                          const llvm::SmallPtrSetImpl<mlir::OpOperand *> &uses,
                          llvm::SmallVectorImpl<mlir::OpOperand *> &,
                          const mlir::DataLayout &) const {
        auto memory = mlir::cast<Op>(op);
        return uses.size() == 1 && memory.getAddress() == slot.ptr &&
               memory.getValue().getType() == slot.elemType &&
               &memory.getAddressMutable() == *uses.begin();
    }
    mlir::DeletionKind removeBlockingUses(
        mlir::Operation *op, const mlir::MemorySlot &,
        const llvm::SmallPtrSetImpl<mlir::OpOperand *> &, mlir::OpBuilder &,
        mlir::Value reaching, const mlir::DataLayout &) const {
        if constexpr (IsLoad) mlir::cast<Op>(op).getValue().replaceAllUsesWith(reaching);
        return mlir::DeletionKind::Delete;
    }
};

using Wide = unsigned __int128;

std::optional<uint64_t> unsigned_constant(mlir::Value value) {
    mlir::IntegerAttr attr;
    if (auto op = value.getDefiningOp<gloin::ConstantOp>())
        attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValueAttr());
    if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>())
        attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValue());
    if (!attr || attr.getValue().getBitWidth() > 64) return std::nullopt;
    return attr.getValue().getZExtValue();
}

// Recognize complete groups: i=0; i+S <= n; i+=S, with zero-based secondary
// counters j+=P. For accesses j+offset, 0<=offset<P, groups<=length/P proves
// the entire group fits without overflowing the entry calculation. Keep the
// original loop on failure, including its stores, calls and trap ordering.
void version_strided_slice_loops(mlir::func::FuncOp function, GloinRangeProofs &proofs) {
    mlir::DominanceInfo dominance(function);
    llvm::SmallVector<mlir::Block *> headers;
    for (auto &block : function.getBody()) headers.push_back(&block);
    for (auto *header : headers) {
        if (proofs.versioned_reductions.contains(header)) continue;
        auto branch = mlir::dyn_cast<mlir::cf::CondBranchOp>(header->getTerminator());
        if (!branch || header->getOperations().size() > 8) continue;
        auto compare = branch.getCondition().getDefiningOp<gloin::CheckedIntegerCompareOp>();
        if (!compare || compare->getBlock() != header || compare.getIsSigned() ||
            compare.getKind() != "<=") continue;
        auto ahead = compare.getLhs().getDefiningOp<gloin::CheckedIntegerBinaryOp>();
        if (!ahead || ahead->getBlock() != header || ahead.getIsSigned() ||
            ahead.getKind() != "+") continue;
        auto primary = mlir::dyn_cast<mlir::BlockArgument>(ahead.getLhs());
        auto stride = unsigned_constant(ahead.getRhs());
        if (!primary || primary.getOwner() != header || !primary.getType().isInteger(64) ||
            !stride || *stride == 0) continue;
        auto *body = branch.getTrueDest();
        auto *after = branch.getFalseDest();
        if (body == header || after == header || after == body ||
            body->getSinglePredecessor() != header || after->getSinglePredecessor() != header ||
            body->getNumArguments() != 0 || body->getOperations().size() > 128) continue;
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
        if (!incoming || incoming.getDest() != header) continue;

        auto bound = compare.getRhs();
        auto bound_length = bound.getDefiningOp<gloin::SliceLengthOp>();
        if (!dominance.dominates(bound, incoming) &&
            !(bound_length && bound_length->getBlock() == header &&
              dominance.dominates(bound_length.getBase(), incoming))) continue;
        bool supported = true;
        for (auto &op : *header) {
            if (&op != branch && &op != compare && &op != ahead &&
                &op != bound_length && !mlir::isa<gloin::ConstantOp, mlir::arith::ConstantOp>(op))
                supported = false;
        }
        // Body results cannot be defined on the empty-loop path. Header SSA
        // arguments may escape to the tail; they are joined explicitly below.
        for (auto *block : {header, body}) {
            for (auto &op : *block) {
                if (op.getNumRegions() || mlir::isa<gloin::StackAllocOp>(op)) supported = false;
                for (auto value : op.getResults())
                    for (auto *user : value.getUsers())
                        if (user->getBlock() != header && user->getBlock() != body) supported = false;
            }
        }
        if (!supported) continue;
        struct Counter {
            mlir::BlockArgument value;
            uint64_t step;
            gloin::CheckedIntegerBinaryOp increment;
        };
        llvm::SmallVector<Counter> counters;
        for (auto arg : header->getArguments()) {
            if (!arg.getType().isInteger(64) ||
                unsigned_constant(incoming.getDestOperands()[arg.getArgNumber()]) != 0) continue;
            auto increment = back.getDestOperands()[arg.getArgNumber()]
                                 .getDefiningOp<gloin::CheckedIntegerBinaryOp>();
            if (!increment || (increment->getBlock() != body && increment != ahead) ||
                increment.getKind() != "+" || increment.getIsSigned() ||
                increment.getLhs() != arg) continue;
            auto step = unsigned_constant(increment.getRhs());
            if (step && *step > 0) counters.push_back({arg, *step, increment});
        }
        auto primary_counter = llvm::find_if(counters, [&](const Counter &counter) {
            return counter.value == primary && counter.step == *stride;
        });
        if (primary_counter == counters.end()) continue;

        struct Access {
            gloin::SliceElementAddressOp op;
            Counter *counter;
            llvm::SmallVector<mlir::Operation *> offsets;
        };
        llvm::SmallVector<Access> accesses;
        llvm::SmallVector<Access *> capacities;
        for (auto access : body->getOps<gloin::SliceElementAddressOp>()) {
            if (proofs.bounds.contains(access) ||
                !dominance.dominates(access.getBase(), incoming)) continue;
            mlir::Value index = access.getIndex();
            Wide offset = 0;
            llvm::SmallVector<mlir::Operation *> offsets;
            for (unsigned depth = 0; depth < 4; ++depth) {
                auto add = index.getDefiningOp<gloin::CheckedIntegerBinaryOp>();
                if (!add || (add->getBlock() != header && add->getBlock() != body) ||
                    add.getIsSigned() || add.getKind() != "+") break;
                auto amount = unsigned_constant(add.getRhs());
                if (!amount) break;
                offset += *amount;
                offsets.push_back(add);
                index = add.getLhs();
            }
            auto counter = llvm::find_if(counters, [&](const Counter &candidate) {
                return candidate.value == index && offset < candidate.step;
            });
            if (counter != counters.end()) accesses.push_back({access, &*counter, offsets});
        }
        // Collect after building the vector, so pointers remain stable.
        for (auto &access : accesses)
            if (llvm::none_of(capacities, [&](Access *prior) {
                    return prior->op.getBase() == access.op.getBase() &&
                           prior->counter->step == access.counter->step;
                })) capacities.push_back(&access);
        if (accesses.empty() || capacities.size() > 4) continue;

        llvm::SmallVector<mlir::Value> live;
        for (auto arg : header->getArguments()) {
            bool escapes = false;
            for (auto *user : arg.getUsers()) {
                if (user->getBlock() == header || user->getBlock() == body) continue;
                escapes = true;
                if (!dominance.dominates(after, user->getBlock())) supported = false;
            }
            if (escapes) live.push_back(arg);
        }
        if (!supported) continue;
        for (auto value : live) {
            auto joined = after->addArgument(value.getType(), value.getLoc());
            value.replaceUsesWithIf(joined, [&](mlir::OpOperand &use) {
                return use.getOwner()->getBlock() != header && use.getOwner()->getBlock() != body;
            });
        }
        branch.getFalseDestOperandsMutable().append(live);
        mlir::IRMapping mapping;
        for (auto *original : {header, body}) {
            auto *copy = new mlir::Block;
            function.getBody().getBlocks().insert(header->getIterator(), copy);
            mapping.map(original, copy);
            for (auto arg : original->getArguments())
                mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
        }
        mlir::OpBuilder builder(function.getContext());
        for (auto *original : {header, body}) {
            builder.setInsertionPointToEnd(mapping.lookup(original));
            for (auto &op : *original) {
                auto *copy = builder.clone(op, mapping);
                if (proofs.arithmetic.contains(&op)) proofs.arithmetic.insert(copy);
                if (proofs.bounds.contains(&op)) proofs.bounds.insert(copy);
            }
        }
        proofs.arithmetic.insert(mapping.lookup(ahead.getOperation()));
        proofs.arithmetic.insert(mapping.lookup(primary_counter->increment.getOperation()));
        for (auto &access : accesses) {
            proofs.bounds.insert(mapping.lookup(access.op.getOperation()));
            proofs.arithmetic.insert(mapping.lookup(access.counter->increment.getOperation()));
            for (auto *offset : access.offsets) proofs.arithmetic.insert(mapping.lookup(offset));
        }
        builder.setInsertionPoint(incoming);
        auto loc = incoming.getLoc();
        const auto constant = [&](uint64_t number) -> mlir::Value {
            return builder.create<mlir::arith::ConstantIntOp>(loc, number, 64);
        };
        if (!dominance.dominates(bound, incoming))
            bound = builder.create<gloin::SliceLengthOp>(loc, builder.getI64Type(),
                bound_length.getBase(), bound_length.getSourceType(), bound_length.getLayoutType());
        auto groups = builder.create<mlir::arith::DivUIOp>(loc, bound, constant(*stride));
        // The original header evaluates i+S once more after the last group.
        // Exclude the overflowing final test, even if every access would fit.
        mlir::Value fits = builder.create<mlir::arith::CmpIOp>(loc,
            mlir::arith::CmpIPredicate::ule, groups, constant((UINT64_MAX - *stride) / *stride));
        for (auto *access : capacities) {
            auto length = builder.create<gloin::SliceLengthOp>(loc, builder.getI64Type(),
                access->op.getBase(), access->op.getSourceType(), access->op.getLayoutType());
            auto available = builder.create<mlir::arith::DivUIOp>(loc, length, constant(access->counter->step));
            auto enough = builder.create<mlir::arith::CmpIOp>(loc,
                mlir::arith::CmpIPredicate::ule, groups, available);
            fits = builder.create<mlir::arith::AndIOp>(loc, fits, enough);
        }
        builder.create<mlir::cf::CondBranchOp>(loc, fits, mapping.lookup(header),
            incoming.getDestOperands(), header, incoming.getDestOperands());
        incoming.erase();
        dominance.invalidate();
    }
}

// Version only small, straight-line counted loops. A failed entry predicate
// selects the original loop, rather than trapping early: calls, stores and
// arithmetic failures must still occur in their original order. The fast copy
// keeps every operation except the bounds guards proven by its entry predicate.
// This runs after promotion, so descriptors and the induction variable are SSA
// snapshots; address-escaped counters and reloaded descriptors cannot match.
void version_slice_loops(mlir::func::FuncOp function, GloinRangeProofs &proofs) {
    mlir::DominanceInfo dominance(function);
    llvm::SmallVector<mlir::Block *> headers;
    for (auto &block : function.getBody()) headers.push_back(&block);
    auto is_constant = [](mlir::Value value, uint64_t expected) {
        mlir::IntegerAttr attr;
        if (auto op = value.getDefiningOp<gloin::ConstantOp>())
            attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValueAttr());
        if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>())
            attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValue());
        return attr && attr.getValue() == expected;
    };
    for (auto *header : headers) {
        if (proofs.versioned_reductions.contains(header)) continue;
        auto branch = mlir::dyn_cast<mlir::cf::CondBranchOp>(header->getTerminator());
        if (!branch || header->getOperations().size() != 2) continue;
        auto compare = branch.getCondition().getDefiningOp<gloin::CheckedIntegerCompareOp>();
        if (!compare || compare->getBlock() != header || compare.getKind() != "<") continue;
        auto index = mlir::dyn_cast<mlir::BlockArgument>(compare.getLhs());
        if (!index || index.getOwner() != header || !index.getType().isInteger(64)) continue;
        auto *body = branch.getTrueDest();
        if (body == header || body == branch.getFalseDest() ||
            body->getSinglePredecessor() != header || body->getNumArguments() != 0 ||
            body->getOperations().size() > 64) continue;
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
        if (!incoming || incoming.getDest() != header ||
            !is_constant(incoming.getDestOperands()[index.getArgNumber()], 0) ||
            !dominance.dominates(compare.getRhs(), incoming)) continue;
        auto increment = back.getDestOperands()[index.getArgNumber()]
                             .getDefiningOp<gloin::CheckedIntegerBinaryOp>();
        if (!increment || increment->getBlock() != body || increment.getKind() != "+" ||
            increment.getIsSigned() != compare.getIsSigned() || increment.getLhs() != index ||
            !is_constant(increment.getRhs(), 1)) continue;

        // Values used after the loop would need an additional SSA join. Keep
        // that broader transformation out of this deliberately bounded pass.
        bool supported = true;
        auto local_uses = [&](mlir::Value value) {
            for (auto *user : value.getUsers())
                if (user->getBlock() != header && user->getBlock() != body) supported = false;
        };
        for (auto *block : {header, body}) {
            for (auto arg : block->getArguments()) local_uses(arg);
            for (auto &op : *block) {
                if (op.getNumRegions() || mlir::isa<gloin::StackAllocOp>(op)) supported = false;
                for (auto result : op.getResults()) local_uses(result);
            }
        }
        llvm::SmallVector<gloin::SliceElementAddressOp> accesses;
        llvm::SmallVector<gloin::SliceElementAddressOp> descriptors;
        for (auto access : body->getOps<gloin::SliceElementAddressOp>()) {
            if (access.getIndex() != index || proofs.bounds.contains(access) ||
                !dominance.dominates(access.getBase(), incoming)) continue;
            accesses.push_back(access);
            if (llvm::none_of(descriptors, [&](auto prior) { return prior.getBase() == access.getBase(); }))
                descriptors.push_back(access);
        }
        if (!supported || accesses.empty() || descriptors.size() > 4) continue;

        mlir::IRMapping mapping;
        for (auto *original : {header, body}) {
            auto *copy = new mlir::Block;
            function.getBody().getBlocks().insert(header->getIterator(), copy);
            mapping.map(original, copy);
            for (auto arg : original->getArguments())
                mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
        }
        mlir::OpBuilder builder(function.getContext());
        for (auto *original : {header, body}) {
            builder.setInsertionPointToEnd(mapping.lookup(original));
            for (auto &op : *original) {
                auto *copy = builder.clone(op, mapping);
                // The original dominating facts also hold in its fast copy.
                // Keep earlier range proofs rather than reintroducing guards.
                if (proofs.arithmetic.contains(&op)) proofs.arithmetic.insert(copy);
                if (proofs.bounds.contains(&op)) proofs.bounds.insert(copy);
            }
        }
        for (auto access : accesses) proofs.bounds.insert(mapping.lookup(access.getOperation()));
        // On a reached iteration 0 <= i < n; adding one cannot overflow even
        // when n is the signed/unsigned maximum. No fast-math is involved.
        proofs.arithmetic.insert(mapping.lookup(increment.getOperation()));
        builder.setInsertionPoint(incoming);
        mlir::Value fits;
        for (auto descriptor : descriptors) {
            auto length = builder.create<gloin::SliceLengthOp>(
                incoming.getLoc(), builder.getI64Type(), descriptor.getBase(),
                descriptor.getSourceType(), descriptor.getLayoutType());
            auto enough = builder.create<mlir::arith::CmpIOp>(
                incoming.getLoc(), mlir::arith::CmpIPredicate::ule, compare.getRhs(), length);
            if (fits) fits = builder.create<mlir::arith::AndIOp>(incoming.getLoc(), fits, enough);
            else fits = enough;
        }
        builder.create<mlir::cf::CondBranchOp>(incoming.getLoc(), fits,
            mapping.lookup(header), incoming.getDestOperands(), header, incoming.getDestOperands());
        incoming.erase();
        // Recompute after each successful clone; stale dominance cannot prove
        // availability for a later loop.
        dominance.invalidate();
    }
}

struct Range { uint64_t low, high; };
struct Fact { mlir::Value lhs, rhs; llvm::StringRef kind; };
struct Affine { mlir::Value base; __int128 offset; };

class FunctionProofs {
    mlir::func::FuncOp function;
    mlir::DominanceInfo dominance;
    llvm::SmallVector<mlir::cf::CondBranchOp> branches;
    llvm::DenseMap<mlir::Value, mlir::Value> lengths;
    llvm::SmallVector<Fact> facts;
    llvm::DenseMap<mlir::Value, Range> cache;

    mlir::Value canonical(mlir::Value value) const {
        auto found = lengths.find(value);
        return found == lengths.end() ? value : found->second;
    }
    static uint64_t maximum(mlir::Value value) {
        auto type = mlir::dyn_cast<mlir::IntegerType>(value.getType());
        if (!type || type.getWidth() >= 64) return UINT64_MAX;
        return (uint64_t{1} << type.getWidth()) - 1;
    }
    static std::optional<uint64_t> constant(mlir::Value value) {
        mlir::IntegerAttr attr;
        if (auto op = value.getDefiningOp<gloin::ConstantOp>())
            attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValueAttr());
        if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>())
            attr = mlir::dyn_cast<mlir::IntegerAttr>(op.getValue());
        if (attr && attr.getValue().getBitWidth() <= 64)
            return attr.getValue().getZExtValue();
        return std::nullopt;
    }
    Affine affine(mlir::Value value, unsigned depth = 0) const {
        if (depth < 8) {
            if (auto op = value.getDefiningOp<gloin::CheckedIntegerBinaryOp>();
                op && !op.getIsSigned()) {
                if (auto rhs = constant(op.getRhs()); rhs &&
                    (op.getKind() == "+" || op.getKind() == "-")) {
                    auto result = affine(op.getLhs(), depth + 1);
                    result.offset += op.getKind() == "+" ? __int128(*rhs) : -__int128(*rhs);
                    return result;
                }
            }
        }
        return {canonical(value), 0};
    }
    void condition(mlir::Value value, bool truth) {
        if (auto op = value.getDefiningOp<mlir::arith::XOrIOp>()) {
            if (constant(op.getRhs()) == 1) { condition(op.getLhs(), !truth); return; }
        }
        auto compare = value.getDefiningOp<gloin::CheckedIntegerCompareOp>();
        if (!compare || compare.getIsSigned()) return;
        auto kind = compare.getKind();
        if (!truth) {
            if (kind == "<") kind = ">=";
            else if (kind == "<=") kind = ">";
            else if (kind == ">") kind = "<=";
            else if (kind == ">=") kind = "<";
            else if (kind == "==") kind = "!=";
            else kind = "==";
        }
        auto lhs = canonical(compare.getLhs()), rhs = canonical(compare.getRhs());
        if (kind == ">" || kind == ">=") {
            std::swap(lhs, rhs);
            kind = kind == ">" ? "<" : "<=";
        }
        facts.push_back({lhs, rhs, kind});
    }
    Range range(mlir::Value value, unsigned depth = 0) {
        value = canonical(value);
        if (auto found = cache.find(value); found != cache.end()) return found->second;
        Range result{0, maximum(value)};
        if (depth >= 16) return result;
        // An in-progress query returns the full range. In particular loop phis
        // do not acquire speculative inductive facts from their own backedge.
        cache[value] = result;
        if (auto literal = constant(value)) result = {*literal, *literal};
        else if (auto op = value.getDefiningOp<mlir::arith::ExtUIOp>())
            result = range(op.getIn(), depth + 1);
        else if (auto op = value.getDefiningOp<gloin::SliceLengthOp>()) {
            if (auto slice = op.getBase().getDefiningOp<gloin::SliceFromArrayOp>()) {
                auto start = constant(slice.getStart()), end = constant(slice.getEnd());
                if (start && end && *start <= *end) result = {*end - *start, *end - *start};
            }
        } else if (auto op = value.getDefiningOp<gloin::CheckedIntegerBinaryOp>();
                   op && !op.getIsSigned()) {
            auto a = range(op.getLhs(), depth + 1), b = range(op.getRhs(), depth + 1);
            const auto limit = maximum(value);
            if (op.getKind() == "+" && Wide(a.high) + b.high <= limit)
                result = {a.low + b.low, a.high + b.high};
            else if (op.getKind() == "-" && a.low >= b.high)
                result = {a.low - b.high, a.high - b.low};
            else if (op.getKind() == "*" && Wide(a.high) * b.high <= limit)
                result = {a.low * b.low, a.high * b.high};
            else if (op.getKind() == "/" && b.low > 0)
                result = {a.low / b.high, a.high / b.low};
            else if (op.getKind() == "%" && b.low > 0)
                result = {0, std::min(a.high, b.high - 1)};
        }
        for (const auto &fact : facts) {
            auto a = affine(fact.lhs), b = affine(fact.rhs);
            if (fact.kind == "<" || fact.kind == "<=") {
                const __int128 strict = fact.kind == "<" ? 1 : 0;
                if (a.base == value) {
                    const auto upper = __int128(range(fact.rhs, depth + 1).high) - a.offset - strict;
                    if (upper >= 0 && upper <= UINT64_MAX)
                        result.high = std::min(result.high, uint64_t(upper));
                }
                if (b.base == value) {
                    const auto lower = __int128(range(fact.lhs, depth + 1).low) - b.offset + strict;
                    if (lower >= 0 && lower <= UINT64_MAX)
                        result.low = std::max(result.low, uint64_t(lower));
                }
            } else if (fact.kind == "==") {
                if (fact.lhs == value) {
                    auto bound = range(fact.rhs, depth + 1);
                    result.low = std::max(result.low, bound.low);
                    result.high = std::min(result.high, bound.high);
                }
                if (fact.rhs == value) {
                    auto bound = range(fact.lhs, depth + 1);
                    result.low = std::max(result.low, bound.low);
                    result.high = std::min(result.high, bound.high);
                }
            } else if (fact.kind == "!=") {
                auto other = fact.lhs == value ? fact.rhs : fact.rhs == value ? fact.lhs : mlir::Value{};
                if (other) if (auto number = constant(other)) {
                    if (*number == result.low && result.low < result.high) ++result.low;
                    if (*number == result.high && result.low < result.high) --result.high;
                }
            }
        }
        if (result.low > result.high) result = {0, maximum(value)};
        cache[value] = result;
        return result;
    }
    bool less(mlir::Value lhs, mlir::Value rhs) {
        if (range(lhs).high < range(rhs).low) return true;
        auto a = affine(lhs), b = affine(rhs);
        if (a.base == b.base && a.offset < b.offset) return true;
        for (const auto &fact : facts) {
            if (fact.kind != "<" && fact.kind != "<=") continue;
            auto x = affine(fact.lhs), y = affine(fact.rhs);
            if (a.base == x.base && b.base == y.base &&
                a.offset - b.offset < x.offset - y.offset + (fact.kind == "<" ? 1 : 0))
                return true;
        }
        return false;
    }

public:
    explicit FunctionProofs(mlir::func::FuncOp function) : function(function), dominance(function) {
        llvm::DenseMap<mlir::Value, mlir::Value> first_length;
        function.walk([&](gloin::SliceLengthOp op) {
            auto [it, inserted] = first_length.try_emplace(op.getBase(), op.getLength());
            lengths[op.getLength()] = it->second;
        });
        function.walk([&](mlir::cf::CondBranchOp op) { branches.push_back(op); });
    }
    void run(GloinRangeProofs &proofs) {
        for (auto &block : function.getBody()) {
            facts.clear();
            cache.clear();
            for (auto branch : branches) {
                for (unsigned edge = 0; edge < 2; ++edge) {
                    auto *destination = branch->getSuccessor(edge);
                    if (destination != branch->getBlock() &&
                        destination->getSinglePredecessor() == branch->getBlock() &&
                        dominance.dominates(branch->getBlock(), destination) &&
                        dominance.dominates(destination, &block) &&
                        branch.getTrueDest() != branch.getFalseDest())
                        condition(branch.getCondition(), edge == 0);
                }
            }
            for (auto &operation : block) {
                if (auto op = mlir::dyn_cast<gloin::CheckedIntegerBinaryOp>(operation);
                    op && !op.getIsSigned()) {
                    auto a = range(op.getLhs()), b = range(op.getRhs());
                    auto kind = op.getKind();
                    const auto limit = maximum(op.getResult());
                    bool safe = (kind == "+" && Wide(a.high) + b.high <= limit) ||
                                (kind == "-" && a.low >= b.high) ||
                                (kind == "*" && Wide(a.high) * b.high <= limit) ||
                                ((kind == "/" || kind == "%") && b.low > 0);
                    if (safe) proofs.arithmetic.insert(op);
                } else if (auto op = mlir::dyn_cast<gloin::ArrayElementAddressOp>(operation)) {
                    auto size = mlir::cast<mlir::LLVM::LLVMArrayType>(op.getArrayType()).getNumElements();
                    if (range(op.getIndex()).high < size) proofs.bounds.insert(op);
                } else if (auto op = mlir::dyn_cast<gloin::SliceElementAddressOp>(operation)) {
                    // Match an existing length of this exact SSA descriptor.
                    // Loads from an aliased descriptor remain distinct values.
                    for (auto [length, first] : lengths) {
                        auto read = length.getDefiningOp<gloin::SliceLengthOp>();
                        if (read.getBase() == op.getBase() && less(op.getIndex(), first)) {
                            proofs.bounds.insert(op);
                            break;
                        }
                    }
                }
            }
        }
    }
};
} // namespace

GloinRangeProofs prove_gloin_ranges(mlir::ModuleOp module) {
    auto *context = module.getContext();
    const auto allocation = mlir::RegisteredOperationName::lookup(
        gloin::StackAllocOp::getOperationName(), context);
    // The public lowering/JIT entry also accepts already lowered LLVM modules.
    // Such contexts need not have loaded or registered the Gloin dialect.
    if (!allocation) return {};
    if (!allocation->hasInterface<mlir::PromotableAllocationOpInterface>()) {
        gloin::StackAllocOp::attachInterface<PromoteAllocation>(*context);
        gloin::LoadOp::attachInterface<PromoteMemory<gloin::LoadOp, true>>(*context);
        gloin::StoreOp::attachInterface<PromoteMemory<gloin::StoreOp, false>>(*context);
    }
    GloinRangeProofs proofs;
    for (auto function : module.getOps<mlir::func::FuncOp>()) {
        if (function.isExternal()) continue;
        llvm::SmallVector<mlir::PromotableAllocationOpInterface> allocations;
        function.walk([&](gloin::StackAllocOp op) {
            allocations.push_back(mlir::cast<mlir::PromotableAllocationOpInterface>(op.getOperation()));
        });
        mlir::OpBuilder builder(context);
        mlir::DominanceInfo dominance(function);
        (void)mlir::tryToPromoteMemorySlots(allocations, builder, mlir::DataLayout(module), dominance);
        FunctionProofs(function).run(proofs);
        version_gloin_float_reductions(function, proofs);
        version_slice_loops(function, proofs);
        version_strided_slice_loops(function, proofs);
    }
    return proofs;
}
