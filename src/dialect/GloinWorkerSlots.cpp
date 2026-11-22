#include "GloinDialect.h"
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
        if (!isa<WorkerPutOp, WorkerTakeOp>(use) ||
            use->getParentRegion() != (*this)->getParentRegion())
            return use->emitOpError("cannot copy or escape thread handle array storage");
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
    // The transfer site gives diagnostics a witness when branches converge.
    std::map<uint64_t, Operation *> live;
    auto operator<=>(const SlotState &) const = default;
};

std::optional<uint64_t> knownIndex(Value value) {
    auto constant = value.getDefiningOp<ConstantOp>();
    auto attr = constant ? dyn_cast<IntegerAttr>(constant.getValue()) : IntegerAttr{};
    if (attr && attr.getValue().getBitWidth() <= 64)
        return attr.getValue().getZExtValue();
    return std::nullopt;
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
    using Edge = std::pair<Block *, SlotState>;
    std::set<Edge> visited;
    SmallVector<Edge, 4> work{{&slots->getParentRegion()->front(), {}}};
    while (!work.empty()) {
        auto [block, state] = work.pop_back_val();
        if (!visited.emplace(block, state).second)
            continue;
        if (visited.size() > 32768)
            return slots.emitOpError("thread slot ownership exceeds the static proof budget");
        for (auto &op : *block) {
            if (&op == slots && failed(outstanding(&op, state)))
                return failure();
            if (!isa<WorkerPutOp, WorkerTakeOp>(op) || op.getOperand(0) != slots.getSlots())
                continue;
            auto index = knownIndex(op.getOperand(1));
            if (!index || *index >= slots.getSlots().getType().getLength())
                return op.emitOpError("thread slot index must be provably in bounds");
            if (isa<WorkerPutOp>(op)) {
                auto [previous, empty] = state.live.emplace(*index, &op);
                if (!empty) {
                    auto error = op.emitOpError("cannot overwrite live thread slot ") << *index;
                    error.attachNote(previous->second->getLoc()) << "previous transfer here";
                    return failure();
                }
            } else if (!state.live.erase(*index)) {
                return op.emitOpError("thread slot must be live on every path to join: ") << *index;
            }
        }
        auto *end = block->getTerminator();
        if (!end->getNumSuccessors() && !isa<LLVM::UnreachableOp>(end) &&
            failed(outstanding(end, state)))
            return failure();
        for (auto *next : end->getSuccessors())
            work.emplace_back(next, state);
    }
    return success();
}
