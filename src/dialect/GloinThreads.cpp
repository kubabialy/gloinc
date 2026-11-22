#include "GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;

LogicalResult gloin::SpawnOp::verify() {
    auto module = (*this)->getParentOfType<ModuleOp>();
    auto callee = module ? module.lookupSymbol<func::FuncOp>(getCallee()) : func::FuncOp{};
    if (!callee || callee.isExternal() || !callee->hasAttrOfType<UnitAttr>("gloin.spawnable"))
        return emitOpError("requires a defined spawnable function");
    auto signature = callee.getFunctionType();
    if (!llvm::equal(signature.getInputs(), getOperandTypes()) || signature.getNumResults() > 1)
        return emitOpError(
            "requires the worker's exact argument types and at most one return value");
    Type payload = NoneType::get(getContext());
    if (signature.getNumResults())
        payload = signature.getResult(0);
    if (auto result = dyn_cast<GloinResultType>(payload))
        payload = result.getValueType();
    auto handle = dyn_cast<GloinSpawnType>(getHandle().getType().getValueType());
    if (!handle || handle.getValueType() != payload)
        return emitOpError("launch result must own one handle with the worker's completion type");
    if (!isa_and_nonnull<func::FuncOp>((*this)->getParentOp()) || !(*this)->getNextNode())
        return emitOpError("requires a function control-flow block");
    return success();
}

LogicalResult gloin::JoinOp::verify() {
    auto owner = getHandle().getDefiningOp<ResultValueOp>();
    if ((!owner || !owner.getOutcome().getDefiningOp<SpawnOp>()) &&
        !getHandle().getDefiningOp<WorkerTakeOp>())
        return emitOpError("requires a handle transferred from a worker launch");
    if (!matchesSourceStorage(*this, getResult().getType(), getLayoutType()))
        return emitOpError("requires the checked completion layout");
    if (getResult().getType().getValueType() != getHandle().getType().getValueType())
        return emitOpError("requires one result of the worker's completion type");
    return success();
}

namespace {
bool isTrue(Value value) {
    auto constant = value.getDefiningOp<gloin::ConstantOp>();
    auto boolean = constant ? dyn_cast<BoolAttr>(constant.getValue()) : BoolAttr{};
    return boolean && boolean.getValue();
}

enum class Owner { Failed, Unextracted, Live, Joined };

// Retain one witness for each reachable ownership state so a diagnostic can
// identify both uses even when they occur in different basic blocks.
struct Path {
    Operation *next;
    Owner owner;
    Operation *previous;
};

LogicalResult ownershipError(Operation *op, StringRef message, Operation *previous) {
    auto diagnostic = op->emitOpError(message);
    if (auto loc = dyn_cast<FileLineColLoc>(previous->getLoc()))
        diagnostic << " (previous ownership operation at " << loc.getFilename().getValue() << ":"
                   << loc.getLine() << ":" << loc.getColumn() << ")";
    return failure();
}

LogicalResult checkLaunch(gloin::SpawnOp launch) {
    auto result = launch.getHandle();
    for (auto *use : result.getUsers()) {
        if (use->getParentRegion() != launch->getParentRegion() ||
            !isa<gloin::ResultIsErrorOp, gloin::ResultErrorOp, gloin::ResultValueOp>(use))
            return use->emitOpError("cannot copy or escape a launch result");
        if (auto transfer = dyn_cast<gloin::ResultValueOp>(use))
            for (auto *consumer : transfer.getValue().getUsers())
                if (!isa<gloin::JoinOp, gloin::WorkerPutOp>(consumer) ||
                    consumer->getParentRegion() != launch->getParentRegion())
                    return consumer->emitOpError("cannot copy or escape a thread handle");
    }

    // Follow the two launch outcomes independently. The immutable result tag
    // refines a conditional edge even after its handle has been consumed.
    SmallVector<Path> work{{launch->getNextNode(), Owner::Failed, launch},
                           {launch->getNextNode(), Owner::Unextracted, launch}};
    llvm::DenseSet<std::pair<Operation *, Owner>> seen;
    while (!work.empty()) {
        auto [op, state, previous] = work.pop_back_val();
        if (!seen.insert({op, state}).second)
            continue;
        for (;; op = op->getNextNode()) {
            if (op == launch || op->hasTrait<OpTrait::IsTerminator>()) {
                if (op == launch || !op->getNumSuccessors()) {
                    if (state == Owner::Live || state == Owner::Unextracted)
                        return ownershipError(
                            op, "successful worker launch must be joined on this path", previous);
                    break;
                }
                if (auto branch = dyn_cast<cf::CondBranchOp>(op)) {
                    Value condition = branch.getCondition();
                    bool inverted = false;
                    if (auto negation = condition.getDefiningOp<arith::XOrIOp>();
                        negation && isTrue(negation.getRhs())) {
                        inverted = true;
                        condition = negation.getLhs();
                    }
                    auto tag = condition.getDefiningOp<gloin::ResultIsErrorOp>();
                    if (tag && tag.getValue() == result) {
                        auto *dest = (state == Owner::Failed) != inverted ? branch.getTrueDest()
                                                                          : branch.getFalseDest();
                        work.push_back({&dest->front(), state, previous});
                        break;
                    }
                }
                for (auto *dest : op->getSuccessors())
                    work.push_back({&dest->front(), state, previous});
                break;
            }
            if (auto take = dyn_cast<gloin::ResultValueOp>(op);
                take && take.getOutcome() == result) {
                if (state != Owner::Unextracted)
                    return ownershipError(
                        op, "requires a successful launch with an untransferred handle", previous);
                state = Owner::Live;
                previous = op;
            } else if (isa<gloin::JoinOp, gloin::WorkerPutOp>(op)) {
                Value handle = isa<gloin::JoinOp>(op) ? cast<gloin::JoinOp>(op).getHandle()
                                                       : cast<gloin::WorkerPutOp>(op).getHandle();
                auto take = handle.getDefiningOp<gloin::ResultValueOp>();
                if (take && take.getOutcome() == result) {
                    if (state != Owner::Live)
                        return ownershipError(
                            op, "thread handle has already been joined on this path", previous);
                    state = Owner::Joined;
                    previous = op;
                }
            } else if (auto error = dyn_cast<gloin::ResultErrorOp>(op);
                       error && error.getOutcome() == result && state != Owner::Failed) {
                return error.emitOpError("requires a failed launch on this path");
            }
        }
    }
    return success();
}
} // namespace

LogicalResult gloin::verifyThreadOwnership(ModuleOp module) {
    llvm::DenseMap<Type, bool> ownerTypes;
    auto containsHandle = [&](Type type) {
        auto cached = ownerTypes.find(type);
        if (cached != ownerTypes.end())
            return cached->second;
        bool contains =
            type.walk([](GloinSpawnType) { return WalkResult::interrupt(); }).wasInterrupted();
        ownerTypes[type] = contains;
        return contains;
    };
    auto checked = module.walk([&](Operation *op) -> WalkResult {
        if (auto slots = dyn_cast<WorkerSlotsOp>(op))
            return failed(verifyWorkerSlots(slots)) ? WalkResult::interrupt() : WalkResult::advance();
        if (isa<WorkerTakeOp>(op))
            return WalkResult::advance();
        if (auto launch = dyn_cast<SpawnOp>(op))
            return failed(checkLaunch(launch)) ? WalkResult::interrupt() : WalkResult::advance();
        if (auto function = dyn_cast<func::FuncOp>(op);
            function && containsHandle(function.getFunctionType()))
            return function.emitOpError("thread handle types cannot cross a function signature");
        if (auto record = dyn_cast<StructDefinitionOp>(op))
            for (auto field : record.getFieldSourceTypes())
                if (containsHandle(cast<TypeAttr>(field).getValue()))
                    return record.emitOpError("thread handles cannot be stored in record fields");
        for (auto &region : op->getRegions())
            for (auto &block : region)
                if (llvm::any_of(block.getArgumentTypes(), containsHandle))
                    return op->emitOpError("thread handles cannot be forwarded as block arguments");
        for (auto type : op->getResultTypes()) {
            if (!containsHandle(type))
                continue;
            auto take = dyn_cast<ResultValueOp>(op);
            if (!take || !isa<GloinSpawnType>(type) || !take.getOutcome().getDefiningOp<SpawnOp>())
                return op->emitOpError(
                    "thread handles must originate in a successful worker launch");
        }
        return WalkResult::advance();
    });
    return failure(checked.wasInterrupted());
}
