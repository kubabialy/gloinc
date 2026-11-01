#include "gir_float_checks.h"
#include "gir_private_float_storage.h"
#include "dialect/GloinDialect.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include <deque>
#include <optional>

namespace {
bool supported(mlir::Value value) {
    return value.getType().isF32() || value.getType().isF64();
}

mlir::FloatAttr constant(mlir::Value value) {
    if (auto op = value.getDefiningOp<gloin::ConstantOp>())
        return mlir::dyn_cast<mlir::FloatAttr>(op.getValueAttr());
    if (auto op = value.getDefiningOp<mlir::arith::ConstantOp>())
        return mlir::dyn_cast<mlir::FloatAttr>(op.getValue());
    return {};
}

// Bottom is an edge whose value has not yet contributed to the fixed point.
// Unknown includes NaN, infinity, undef and opaque memory/call results. Finite
// intervals include both signed zeros. Arithmetic uses conservative absolute
// magnitudes; branch edges retain signed endpoints through joins.
struct Bound {
    enum Kind { Bottom, Finite, Unknown } kind = Bottom;
    std::optional<llvm::APFloat> magnitude;
    std::optional<llvm::APFloat> lower, upper;

    static Bound unknown() { return {Unknown, std::nullopt, std::nullopt, std::nullopt}; }
    static Bound interval(llvm::APFloat lower, llvm::APFloat upper) {
        if (!lower.isFinite() || !upper.isFinite()) return unknown();
        auto a = lower, b = upper;
        a.clearSign(); b.clearSign();
        return {Finite, a.compare(b) == llvm::APFloat::cmpGreaterThan ? a : b, lower, upper};
    }
    static Bound finite(llvm::APFloat value) {
        if (!value.isFinite()) return unknown();
        value.clearSign();
        auto lower = value; lower.changeSign();
        return interval(lower, value);
    }
    static Bound full(mlir::Type type) {
        return finite(llvm::APFloat::getLargest(mlir::cast<mlir::FloatType>(type).getFloatSemantics()));
    }
    void join(const Bound &other) {
        if (kind == Unknown || other.kind == Bottom) return;
        if (other.kind == Unknown || kind == Bottom) { *this = other; return; }
        auto lo = *lower, hi = *upper;
        if (lo.compare(*other.lower) == llvm::APFloat::cmpGreaterThan) lo = *other.lower;
        if (hi.compare(*other.upper) == llvm::APFloat::cmpLessThan) hi = *other.upper;
        *this = interval(lo, hi);
    }
};

class FloatProofs {
    mlir::func::FuncOp function;
    mutable mlir::DominanceInfo dominance;
    struct Input { mlir::Value value; mlir::Block *block; };
    struct Storage {
        mlir::Type element;
        llvm::SmallVector<Input> writes;
        bool zero = false;
    };
    struct Guard { mlir::cf::CondBranchOp branch; llvm::APFloat constant; std::string kind; bool inverted; };
    llvm::DenseMap<mlir::Value, Storage> storage;
    llvm::DenseMap<mlir::Value, mlir::Value> load_roots;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<Input>> incoming;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<Guard>> guards;
    bool array_proofs = false;
    llvm::DenseMap<mlir::Value, Bound> bounds;
    llvm::DenseMap<mlir::Value, unsigned> growth;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> inputs, dependents;
    llvm::SmallVector<mlir::Value> values;
    // Profitability, not a safety restriction: removing the last effect from
    // a conditional loop path can make LLVM speculate its floating arithmetic
    // into selects, extending the loop-carried dependency chain. Retain these
    // checks until a target-aware cost model can justify that tradeoff. Their
    // result bounds still contribute to the analysis of unconditional work.
    llvm::DenseSet<mlir::Block *> conditional_loop_blocks;

    Bound get(mlir::Value value) const {
        auto it = bounds.find(value);
        return it == bounds.end() ? Bound::unknown() : it->second;
    }

    Bound at(mlir::Value value, mlir::Block *block, mlir::Block *successor = nullptr) const {
        auto result = get(value);
        // Never infer finiteness from unordered false edges or an unstable
        // undef/poison value. Refinement only narrows an existing finite fact.
        if (result.kind != Bound::Finite) return result;
        auto found = guards.find(value);
        if (found == guards.end()) return result;
        auto lo = *result.lower, hi = *result.upper;
        for (auto guard : found->second) {
            auto yes = guard.branch.getTrueDest(), no = guard.branch.getFalseDest();
            if (yes == no) continue;
            const bool yes_dominates = (guard.branch->getBlock() == block && successor == yes) ||
                (yes->getSinglePredecessor() == guard.branch->getBlock() && dominance.dominates(yes, block));
            const bool no_dominates = (guard.branch->getBlock() == block && successor == no) ||
                (no->getSinglePredecessor() == guard.branch->getBlock() && dominance.dominates(no, block));
            if (yes_dominates == no_dominates) continue;
            const bool truth = yes_dominates != guard.inverted;
            const auto &number = guard.constant;
            // Strict inequalities conservatively include the endpoint.
            const bool lower = truth ? (guard.kind == ">" || guard.kind == ">=" || guard.kind == "==")
                                     : (guard.kind == "<" || guard.kind == "<=");
            const bool upper = truth ? (guard.kind == "<" || guard.kind == "<=" || guard.kind == "==")
                                     : (guard.kind == ">" || guard.kind == ">=");
            if (lower && lo.compare(number) == llvm::APFloat::cmpLessThan) lo = number;
            if (upper && hi.compare(number) == llvm::APFloat::cmpGreaterThan) hi = number;
        }
        // An inconsistent refinement denotes an unreachable path. Do not use
        // it to manufacture a bound or a non-poison value.
        if (lo.compare(hi) == llvm::APFloat::cmpGreaterThan) return result;
        return Bound::interval(lo, hi);
    }

    Bound binary(mlir::Value lhs, mlir::Value rhs, llvm::StringRef kind, bool checked = false, mlir::Block *block = nullptr) const {
        auto a = block ? at(lhs, block) : get(lhs), b = block ? at(rhs, block) : get(rhs);
        if (a.kind == Bound::Bottom || b.kind == Bound::Bottom) return {};
        // A successful +, -, or * cannot have consumed NaN or infinity. The
        // same holds for / with a finite nonzero constant divisor. Use this
        // only to bound a checked RESULT; it never justifies removing that
        // operation's own check. For example checked(x * .5) is bounded by
        // max/2 even if x comes from an opaque call or memory.
        auto divisor = constant(rhs);
        if (checked && (kind == "+" || kind == "-" || kind == "*" ||
                        (kind == "/" && divisor && divisor.getValue().isFinite() && !divisor.getValue().isZero()))) {
            if (a.kind == Bound::Unknown) a = Bound::full(lhs.getType());
            if (b.kind == Bound::Unknown) b = Bound::full(rhs.getType());
        }
        if (a.kind != Bound::Finite || b.kind != Bound::Finite) return Bound::unknown();
        auto maximum = *a.magnitude;
        // Round in the source format, exactly as the emitted operation does.
        // Positive add/mul and division by a positive constant are monotone;
        // rounding the maximum therefore also bounds every interior result.
        // Subtraction is bounded by |a|+|b|. No reassociation or contraction.
        if (kind == "+" || kind == "-")
            maximum.add(*b.magnitude, llvm::APFloat::rmNearestTiesToEven);
        else if (kind == "*")
            maximum.multiply(*b.magnitude, llvm::APFloat::rmNearestTiesToEven);
        else if (kind == "/") {
            // An upper magnitude bound alone cannot exclude a zero divisor.
            if (!divisor || divisor.getValue().isZero() || !divisor.getValue().isFinite())
                return Bound::unknown();
            auto magnitude = divisor.getValue();
            magnitude.clearSign();
            maximum.divide(magnitude, llvm::APFloat::rmNearestTiesToEven);
        } else return Bound::unknown();
        return Bound::finite(std::move(maximum));
    }

    template <typename Op>
    Bound ordinary(Op op, llvm::StringRef kind) const {
        // Mixed IR may permit reassociation or approximate reciprocals. Our
        // rounded endpoint proof describes only ordinary IEEE operations.
        if (op.getFastmath() != mlir::arith::FastMathFlags::none) return Bound::unknown();
        return binary(op.getLhs(), op.getRhs(), kind, false, op->getBlock());
    }

    Bound evaluate(mlir::Value value) const {
        if (auto it = storage.find(value); it != storage.end()) {
            Bound result;
            if (it->second.zero) result = Bound::finite(llvm::APFloat::getZero(
                mlir::cast<mlir::FloatType>(it->second.element).getFloatSemantics()));
            for (auto input : it->second.writes) result.join(at(input.value, input.block));
            return result;
        }
        if (auto it = load_roots.find(value); it != load_roots.end()) return get(it->second);
        if (mlir::isa<mlir::BlockArgument>(value)) {
            auto it = incoming.find(value);
            if (it == incoming.end() || it->second.empty()) return Bound::unknown();
            Bound joined;
            for (auto input : it->second) joined.join(at(input.value, input.block, mlir::cast<mlir::BlockArgument>(value).getOwner()));
            return joined;
        }
        if (auto attr = constant(value)) return Bound::interval(attr.getValue(), attr.getValue());
        if (auto op = value.getDefiningOp<gloin::CheckedFloatBinaryOp>()) {
            auto bound = binary(op.getLhs(), op.getRhs(), op.getKind(), true, op->getBlock());
            // Successful checked operations are finite even when their inputs
            // are unknown. Keep this check unless its operands prove it safe.
            return bound.kind == Bound::Unknown ? Bound::full(value.getType()) : bound;
        }
        if (auto op = value.getDefiningOp<mlir::arith::NegFOp>()) {
            if (op.getFastmath() != mlir::arith::FastMathFlags::none) return Bound::unknown();
            auto result = at(op.getOperand(), op->getBlock());
            if (result.kind != Bound::Finite) return result;
            auto lo = *result.upper, hi = *result.lower;
            lo.changeSign(); hi.changeSign();
            return Bound::interval(lo, hi);
        }
        if (auto op = value.getDefiningOp<mlir::arith::SelectOp>()) {
            // A poison condition poisons even two finite alternatives. That
            // result could become NaN when a checked consumer freezes it.
            // CFG joins are different: branching on poison is already UB.
            if (array_proofs && !op.getCondition().getDefiningOp<mlir::LLVM::FreezeOp>() &&
                !op.getCondition().getDefiningOp<mlir::arith::ConstantOp>() &&
                !op.getCondition().getDefiningOp<gloin::ConstantOp>()) return Bound::unknown();
            auto bound = at(op.getTrueValue(), op->getBlock());
            bound.join(at(op.getFalseValue(), op->getBlock()));
            return bound;
        }
        if (auto op = value.getDefiningOp<mlir::arith::AddFOp>()) return ordinary(op, "+");
        if (auto op = value.getDefiningOp<mlir::arith::SubFOp>()) return ordinary(op, "-");
        if (auto op = value.getDefiningOp<mlir::arith::MulFOp>()) return ordinary(op, "*");
        if (auto op = value.getDefiningOp<mlir::arith::DivFOp>()) return ordinary(op, "/");
        if (array_proofs)
            if (auto op = value.getDefiningOp<mlir::LLVM::FreezeOp>()) return get(op.getOperand());
        // Without stabilized checked inputs, freeze remains opaque: poison may
        // become any floating value, including infinity or NaN.
        return Bound::unknown();
    }

    void discover_guards() {
        function.walk([&](mlir::cf::CondBranchOp branch) {
            mlir::Value condition = branch.getCondition();
            bool inverted = false;
            if (auto negate = condition.getDefiningOp<mlir::arith::XOrIOp>()) {
                auto literal = negate.getRhs().getDefiningOp<mlir::arith::ConstantOp>();
                auto attr = literal ? mlir::dyn_cast<mlir::IntegerAttr>(literal.getValue()) : mlir::IntegerAttr{};
                if (attr && attr.getValue().isOne()) { condition = negate.getLhs(); inverted = true; }
            }
            auto compare = condition.getDefiningOp<gloin::CheckedFloatCompareOp>();
            if (!compare) return;
            auto value = compare.getLhs();
            auto number = constant(compare.getRhs());
            std::string kind = compare.getKind().str();
            if (!number) {
                number = constant(compare.getLhs()); value = compare.getRhs();
                if (kind == "<") kind = ">";
                else if (kind == "<=") kind = ">=";
                else if (kind == ">") kind = "<";
                else if (kind == ">=") kind = "<=";
            }
            if (!number || !number.getValue().isFinite()) return;
            guards[value].push_back({branch, number.getValue(), kind, inverted});
        });
    }

    bool discover_root(gloin::StackAllocOp allocation) {
        auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(allocation.getElementType());
        auto root = allocation.getAddress();
        auto pointer = mlir::dyn_cast<mlir::LLVM::LLVMPointerType>(root.getType());
        if (!array || !(array.getElementType().isF32() || array.getElementType().isF64()) ||
            !pointer || pointer.getAddressSpace() != 0 || !gloin_detail::storage_bytes(array)) return false;
        Storage item{array.getElementType(), {}, false};
        llvm::SmallVector<mlir::Value> work{root};
        llvm::DenseSet<mlir::Value> seen, addresses, slices;
        llvm::SmallVector<gloin::LoadOp> loads;
        llvm::SmallVector<mlir::Operation *> initializers;
        auto write = [&](mlir::Value value, mlir::Operation *op) {
            if (value.getType() != item.element) return false;
            item.writes.push_back({value, op->getBlock()}); return true;
        };
        while (!work.empty()) {
            auto value = work.pop_back_val();
            if (!seen.insert(value).second) continue;
            if (seen.size() > 256) return false;
            for (auto &use : value.getUses()) {
                auto *op = use.getOwner();
                if (auto store = mlir::dyn_cast<gloin::StoreOp>(op)) {
                    if (store.getAddress() != value) return false;
                    if (addresses.contains(value)) { if (!write(store.getValue(), op)) return false; }
                    else if (value == root && store.getValue().getType() == array) {
                        auto init = store.getValue();
                        if (init.getDefiningOp<gloin::ZeroedArrayOp>() || init.getDefiningOp<mlir::LLVM::ZeroOp>()) item.zero = true;
                        else if (auto literal = init.getDefiningOp<gloin::ArrayLiteralOp>()) {
                            for (auto element : literal.getElements()) if (!write(element, op)) return false;
                        } else if (auto repeat = init.getDefiningOp<gloin::RepeatArrayOp>()) {
                            if (!write(repeat.getElement(), op)) return false;
                        } else return false;
                        initializers.push_back(op);
                    } else return false;
                } else if (auto load = mlir::dyn_cast<gloin::LoadOp>(op)) {
                    if (load.getAddress() != value || !addresses.contains(value) ||
                        load.getValue().getType() != item.element || load.getLayoutType() != item.element) return false;
                    loads.push_back(load);
                } else if (auto fill = mlir::dyn_cast<gloin::ArrayFillOp>(op)) {
                    if (value != root || fill.getAddress() != root || fill.getLayoutType() != array ||
                        fill.getElementType() != item.element || !write(fill.getElement(), op)) return false;
                    initializers.push_back(op);
                } else if (auto address = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op)) {
                    if (value != root || address.getBase() != root || address.getArrayType() != array) return false;
                    addresses.insert(address.getAddress()); work.push_back(address.getAddress());
                } else if (auto slice = mlir::dyn_cast<gloin::SliceFromArrayOp>(op)) {
                    if (value != root || slice.getBase() != root || slice.getArrayType() != array) return false;
                    slices.insert(slice.getValue()); work.push_back(slice.getValue());
                } else if (auto slice = mlir::dyn_cast<gloin::SliceSubrangeOp>(op)) {
                    if (!slices.contains(value) || slice.getBase() != value || slice.getElementType() != item.element) return false;
                    slices.insert(slice.getValue()); work.push_back(slice.getValue());
                } else if (auto address = mlir::dyn_cast<gloin::SliceElementAddressOp>(op)) {
                    if (!slices.contains(value) || address.getBase() != value || address.getElementType() != item.element) return false;
                    addresses.insert(address.getAddress()); work.push_back(address.getAddress());
                } else if (auto length = mlir::dyn_cast<gloin::SliceLengthOp>(op)) {
                    if (!slices.contains(value) || length.getBase() != value) return false;
                } else return false;
            }
        }
        if (loads.empty()) return false;
        for (auto load : loads) {
            bool initialized = false;
            for (auto *init : initializers) initialized |= dominance.dominates(init, load);
            if (!initialized) return false;
        }
        storage.try_emplace(root, std::move(item));
        values.push_back(root);
        for (auto input : storage[root].writes) inputs[root].push_back(input.value);
        for (auto load : loads) { load_roots[load.getValue()] = root; inputs[load.getValue()].push_back(root); }
        return true;
    }

    void discover_storage() {
        auto module = function->getParentOfType<mlir::ModuleOp>();
        if (!module) return;
        auto attr = module->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
        auto layout = llvm::DataLayout::parse(attr ? attr.getValue() : "");
        if (!layout) { llvm::consumeError(layout.takeError()); return; }
        llvm::LLVMContext context;
        if (layout->getPointerSizeInBits() != 64 || layout->getIndexSizeInBits(0) != 64 ||
            layout->getTypeAllocSize(llvm::Type::getFloatTy(context)) != 4 ||
            layout->getTypeAllocSize(llvm::Type::getDoubleTy(context)) != 8) return;
        bool restore = false, raw_write = false;
        function.walk([&](mlir::LLVM::StackRestoreOp) { restore = true; });
        function.walk([&](mlir::LLVM::StoreOp) { raw_write = true; });
        if (restore || raw_write) return;
        unsigned roots = 0;
        function.walk([&](gloin::StackAllocOp op) { if (++roots <= 64) discover_root(op); });
    }

    void stabilize_checked_inputs() {
        // Array invariants and branch refinements must describe one concrete
        // value, including at the public mixed GIR/LLVM boundary. Freeze opaque
        // inputs before checked arithmetic establishes its finite result fact.
        // Place freezes at definitions so adjacent arithmetic stays adjacent.
        mlir::IRRewriter rewriter(function.getContext());
        llvm::DenseMap<mlir::Value, mlir::Value> frozen;
        llvm::SmallVector<gloin::CheckedFloatBinaryOp> operations;
        function.walk([&](gloin::CheckedFloatBinaryOp op) { operations.push_back(op); });
        for (auto op : operations) for (unsigned i = 0; i != 2; ++i) {
            auto value = op->getOperand(i);
            if (constant(value) || value.getDefiningOp<mlir::LLVM::FreezeOp>() ||
                value.getDefiningOp<gloin::CheckedFloatBinaryOp>()) continue;
            auto [it, inserted] = frozen.try_emplace(value);
            if (inserted) {
                rewriter.setInsertionPointAfterValue(value);
                it->second = rewriter.create<mlir::LLVM::FreezeOp>(op.getLoc(), value);
            }
            op->setOperand(i, it->second);
        }
    }

public:
    explicit FloatProofs(mlir::func::FuncOp function) : function(function), dominance(function) {
        discover_storage();
        array_proofs = !storage.empty();
        if (array_proofs) { stabilize_checked_inputs(); discover_guards(); }
        if (!function.getBody().hasOneBlock()) {
            mlir::DominanceInfo dominance(function);
            mlir::CFGLoopInfo loops(dominance.getDomTree(&function.getBody()));
            for (auto &block : function.getBody()) {
                auto *loop = loops.getLoopFor(&block);
                if (!loop) continue;
                llvm::SmallVector<mlir::Block *> latches;
                loop->getLoopLatches(latches);
                for (auto *latch : latches)
                    if (!dominance.dominates(&block, latch)) {
                        conditional_loop_blocks.insert(&block);
                        break;
                    }
            }
        }
        for (auto &block : function.getBody()) {
            for (auto arg : block.getArguments()) {
                if (!supported(arg)) continue;
                values.push_back(arg);
                // Only the explicit cf branch ABI is understood. Unknown
                // terminators, entry arguments and unreachable roots stay top.
                bool known = !block.hasNoPredecessors() && &block != &function.front();
                auto &edges = inputs[arg];
                auto add_edge = [&](mlir::Value value, mlir::Block *predecessor) {
                    edges.push_back(value); incoming[arg].push_back({value, predecessor});
                };
                for (auto *predecessor : block.getPredecessors()) {
                    auto *terminator = predecessor->getTerminator();
                    if (auto branch = mlir::dyn_cast<mlir::cf::BranchOp>(terminator))
                        add_edge(branch.getDestOperands()[arg.getArgNumber()], predecessor);
                    else if (auto branch = mlir::dyn_cast<mlir::cf::CondBranchOp>(terminator)) {
                        if (branch.getTrueDest() == &block)
                            add_edge(branch.getTrueDestOperands()[arg.getArgNumber()], predecessor);
                        if (branch.getFalseDest() == &block)
                            add_edge(branch.getFalseDestOperands()[arg.getArgNumber()], predecessor);
                    } else known = false;
                }
                if (!known) { edges.clear(); incoming[arg].clear(); }
            }
            for (auto &op : block) {
                for (auto value : op.getResults()) {
                    if (!supported(value)) continue;
                    values.push_back(value);
                    for (auto operand : op.getOperands())
                        if (supported(operand)) inputs[value].push_back(operand);
                }
            }
        }
        for (auto value : values) bounds.try_emplace(value);
        // Preserve IR order, rather than DenseMap's address-dependent order,
        // so widening and the resulting optimization are reproducible.
        for (auto value : values)
            for (auto operand : inputs[value]) dependents[operand].push_back(value);
    }

    void solve() {
        std::deque<mlir::Value> work(values.begin(), values.end());
        llvm::DenseSet<mlir::Value> queued(values.begin(), values.end());
        while (!work.empty()) {
            auto value = work.front();
            work.pop_front();
            queued.erase(value);
            auto next = evaluate(value);
            auto &previous = bounds[value];
            next.join(previous);
            if (next.kind == previous.kind &&
                (next.kind != Bound::Finite || (next.lower->bitwiseIsEqual(*previous.lower) &&
                                               next.upper->bitwiseIsEqual(*previous.upper)))) continue;
            // A growing induction variable must not require a simulation of
            // the loop. Widen to all finite values after four increases. Top
            // still wins later if any incoming path can be non-finite.
            if (previous.kind == Bound::Finite && next.kind == Bound::Finite && ++growth[value] > 4)
                next = Bound::full(storage.contains(value) ? storage[value].element : value.getType());
            previous = std::move(next);
            for (auto dependent : dependents[value])
                if (queued.insert(dependent).second) work.push_back(dependent);
        }

    }

    void collect(llvm::SmallPtrSetImpl<mlir::Operation *> &safe) {
        solve();
        for (auto value : values)
            if (auto op = value.getDefiningOp<gloin::CheckedFloatBinaryOp>();
                op && binary(op.getLhs(), op.getRhs(), op.getKind(), false, op->getBlock()).kind == Bound::Finite)
                safe.insert(op);
    }

    void run(mlir::IRRewriter &rewriter) {
        solve();
        // Decide everything at the fixed point before mutating IR. Mutually
        // supporting loop facts are inductive: every entry and backedge has
        // contributed, and every eliminated check is safe for that invariant.
        llvm::SmallVector<gloin::CheckedFloatBinaryOp> safe;
        for (auto value : values)
            if (auto op = value.getDefiningOp<gloin::CheckedFloatBinaryOp>();
                op && !conditional_loop_blocks.contains(op->getBlock()) &&
                binary(op.getLhs(), op.getRhs(), op.getKind()).kind == Bound::Finite)
                safe.push_back(op);
        for (auto op : safe) {
            rewriter.setInsertionPoint(op);
            auto kind = op.getKind();
            if (kind == "+") rewriter.replaceOpWithNewOp<mlir::arith::AddFOp>(op, op.getLhs(), op.getRhs());
            else if (kind == "-") rewriter.replaceOpWithNewOp<mlir::arith::SubFOp>(op, op.getLhs(), op.getRhs());
            else if (kind == "*") rewriter.replaceOpWithNewOp<mlir::arith::MulFOp>(op, op.getLhs(), op.getRhs());
            else if (kind == "/") rewriter.replaceOpWithNewOp<mlir::arith::DivFOp>(op, op.getLhs(), op.getRhs());
        }
    }
};
} // namespace

void find_proven_gloin_float_checks(mlir::func::FuncOp function,
                                   llvm::SmallPtrSetImpl<mlir::Operation *> &safe) {
    FloatProofs(function).collect(safe);
}

void remove_proven_gloin_float_checks(mlir::ModuleOp module) {
    mlir::IRRewriter rewriter(module.getContext());
    for (auto function : module.getOps<mlir::func::FuncOp>()) {
        if (function.isExternal()) continue;
        bool has_checks = false;
        function.walk([&](gloin::CheckedFloatBinaryOp) { has_checks = true; });
        if (has_checks) FloatProofs(function).run(rewriter);
    }
}
