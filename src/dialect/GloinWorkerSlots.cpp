#include "GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include <map>
#include <set>

using namespace mlir;
using namespace gloin;

LogicalResult WorkerSlotsOp::verify() {
    if (!isa<GloinSpawnType>(getSlots().getType().getElement()) ||
        !isa<func::FuncOp>((*this)->getParentOp()) || getSlots().getType().getLength() > 1048576)
        return emitOpError("requires local fixed storage of thread handles");
    for (auto *use : getSlots().getUsers())
        if (!isa<WorkerPutOp, WorkerTakeOp, WorkerSlotsEndOp>(use) ||
            use->getParentRegion() != (*this)->getParentRegion())
            return use->emitOpError("cannot copy or escape thread handle array storage");
    return success();
}

LogicalResult WorkerSlotsEndOp::verify() {
    if (!getSlots().getDefiningOp<WorkerSlotsOp>())
        return emitOpError("requires private worker slots");
    return success();
}

LogicalResult WorkerPutOp::verify() {
    auto success = getHandle().getDefiningOp<ResultValueOp>();
    if (!getSlots().getDefiningOp<WorkerSlotsOp>() || !success ||
        !success.getOutcome().getDefiningOp<SpawnOp>() ||
        getHandle().getType() != getSlots().getType().getElement())
        return emitOpError("requires a fresh launch success of the slot's handle type");
    return mlir::success();
}

LogicalResult WorkerTakeOp::verify() {
    if (!getSlots().getDefiningOp<WorkerSlotsOp>() ||
        getHandle().getType() != getSlots().getType().getElement() || !getHandle().hasOneUse() ||
        !isa_and_nonnull<JoinOp>((*this)->getNextNode()) ||
        *getHandle().getUsers().begin() != (*this)->getNextNode())
        return emitOpError("must transfer a slot directly to exactly one join");
    return success();
}

namespace {
struct SlotState {
    bool active = false;
    // The transfer site gives diagnostics a witness when branches converge.
    std::map<uint64_t, Operation *> live;
    std::map<Operation *, uint64_t> counters;
    auto operator<=>(const SlotState &) const = default;
};

// Track only unaliased integer locals. A load samples its value at the point
// of execution. A later store must never retroactively change that SSA value.
class BlockIntegers {
    llvm::DenseMap<Value, llvm::APInt> values;
    SlotState &state;
    const llvm::DenseSet<Operation *> &locals;
    uint64_t length;

    std::optional<llvm::APInt> evaluate(Operation &op) const {
        if (auto load = dyn_cast<LoadOp>(op)) {
            auto found = state.counters.find(load.getAddress().getDefiningOp());
            if (found != state.counters.end())
                return llvm::APInt(cast<IntegerType>(load.getValue().getType()).getWidth(),
                                   found->second);
        } else if (isa<ToLayoutOp, FromLayoutOp>(op) &&
                   op.getOperand(0).getType() == op.getResult(0).getType()) {
            return get(op.getOperand(0));
        } else if (isa<arith::ExtSIOp, arith::ExtUIOp>(op)) {
            if (auto input = get(op.getOperand(0))) {
                auto width = cast<IntegerType>(op.getResult(0).getType()).getWidth();
                return isa<arith::ExtSIOp>(op) ? input->sext(width) : input->zext(width);
            }
        } else if (isa<CheckedIntegerBinaryOp, CheckedIntegerCompareOp>(op)) {
            auto lhs = get(op.getOperand(0));
            auto rhs = get(op.getOperand(1));
            if (!lhs || !rhs || lhs->getBitWidth() != rhs->getBitWidth())
                return std::nullopt;
            if (auto add = dyn_cast<CheckedIntegerBinaryOp>(op); add && add.getKind() == "+")
                return *lhs + *rhs; // Only returning paths matter; overflow traps.
            if (auto cmp = dyn_cast<CheckedIntegerCompareOp>(op)) {
                auto kind = cmp.getKind();
                if (kind == "<")
                    return llvm::APInt(1, cmp.getIsSigned() ? lhs->slt(*rhs) : lhs->ult(*rhs));
                if (kind == "==")
                    return llvm::APInt(1, *lhs == *rhs);
            }
        }
        return std::nullopt;
    }

  public:
    BlockIntegers(SlotState &state, const llvm::DenseSet<Operation *> &locals, uint64_t length)
        : state(state), locals(locals), length(length) {}

    std::optional<llvm::APInt> get(Value value) const {
        if (auto constant = value.getDefiningOp<ConstantOp>()) {
            if (auto integer = dyn_cast<IntegerAttr>(constant.getValue());
                integer && value.getType().isSignlessInteger() &&
                integer.getType() == value.getType())
                return integer.getValue();
        }
        auto found = values.find(value);
        if (found != values.end())
            return found->second;
        return std::nullopt;
    }

    void step(Operation &op) {
        if (isa<StackAllocOp>(op))
            state.counters.erase(&op);
        if (auto store = dyn_cast<StoreOp>(op)) {
            auto *local = store.getAddress().getDefiningOp();
            if (locals.contains(local)) {
                auto number = get(store.getValue());
                if (number && number->getLimitedValue() <= length)
                    state.counters[local] = number->getZExtValue();
                else
                    state.counters.erase(local);
            }
        } else if (op.getNumResults() == 1 && op.getResult(0).getType().isSignlessInteger()) {
            if (auto number = evaluate(op))
                values.try_emplace(op.getResult(0), *number);
        }
    }
};

bool isPrivateCounter(StackAllocOp alloc) {
    if (!alloc.getSourceType().isSignlessInteger() ||
        alloc.getSourceType() != alloc.getElementType())
        return false;
    return llvm::all_of(alloc.getAddress().getUsers(), [&](Operation *use) {
        if (use->getParentRegion() != alloc->getParentRegion())
            return false;
        if (auto load = dyn_cast<LoadOp>(use))
            return load.getAddress() == alloc.getAddress() &&
                   load.getValue().getType() == alloc.getSourceType();
        if (auto store = dyn_cast<StoreOp>(use))
            return store.getAddress() == alloc.getAddress() &&
                   store.getValue().getType() == alloc.getSourceType();
        return isa<ToLayoutOp>(use) && use->use_empty();
    });
}

LogicalResult outstanding(Operation *exit, const SlotState &state) {
    if (state.live.empty())
        return success();
    auto [index, put] = *state.live.begin();
    auto error = exit->emitOpError("thread slot ")
                 << index << " must be joined before leaving its scope";
    error.attachNote(put->getLoc()) << "handle transferred here";
    return failure();
}
} // namespace

LogicalResult gloin::verifyWorkerSlots(WorkerSlotsOp slots) {
    if (slots.getSlots().use_empty())
        return success();
    llvm::DenseSet<Operation *> locals;
    auto &region = *slots->getParentRegion();
    for (auto &block : region)
        for (auto alloc : block.getOps<StackAllocOp>())
            if (isPrivateCounter(alloc))
                locals.insert(alloc);
    using Edge = std::pair<Block *, SlotState>;
    std::set<Edge> visited;
    SmallVector<Edge, 4> work{{&slots->getParentRegion()->front(), {}}};
    size_t proofCost = 0;
    while (!work.empty()) {
        auto [block, state] = work.pop_back_val();
        if (!visited.emplace(block, state).second)
            continue;
        proofCost += 1 + state.counters.size() + state.live.size();
        if (proofCost > 262144)
            return slots.emitOpError("thread slot ownership exceeds the static proof budget");
        BlockIntegers integers(state, locals, slots.getSlots().getType().getLength());
        for (auto &op : *block) {
            integers.step(op);
            if (&op == slots ||
                (isa<WorkerSlotsEndOp>(op) && op.getOperand(0) == slots.getSlots())) {
                if (failed(outstanding(&op, state)))
                    return failure();
                state.active = &op == slots;
                continue;
            }
            if (!isa<WorkerPutOp, WorkerTakeOp>(op) || op.getOperand(0) != slots.getSlots())
                continue;
            if (!state.active)
                return op.emitOpError("thread slot used outside its owning scope");
            auto number = integers.get(op.getOperand(1));
            if (!number || number->getLimitedValue() >= slots.getSlots().getType().getLength())
                return op.emitOpError("thread slot index must be provably in bounds");
            auto index = number->getZExtValue();
            if (isa<WorkerPutOp>(op)) {
                auto [previous, empty] = state.live.emplace(index, &op);
                if (!empty) {
                    auto error = op.emitOpError("cannot overwrite live thread slot ") << index;
                    error.attachNote(previous->second->getLoc()) << "previous transfer here";
                    return failure();
                }
            } else if (!state.live.erase(index)) {
                return op.emitOpError("thread slot must be live on every path to join: ") << index;
            }
        }
        auto *end = block->getTerminator();
        if (!end->getNumSuccessors() && !isa<LLVM::UnreachableOp>(end) &&
            failed(outstanding(end, state)))
            return failure();
        if (auto branch = dyn_cast<cf::CondBranchOp>(end)) {
            if (auto truth = integers.get(branch.getCondition())) {
                work.emplace_back(truth->isZero() ? branch.getFalseDest() : branch.getTrueDest(),
                                  state);
                continue;
            }
        }
        for (auto *next : end->getSuccessors())
            work.emplace_back(next, state);
    }
    return success();
}
