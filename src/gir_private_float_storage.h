#pragma once

#include "dialect/GloinDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <cstdint>
#include <optional>

namespace gloin_detail {
// Our native targets use 64-bit addresses. Do not infer valid storage from a
// type whose nested extent could wrap allocation/address calculations. This
// bound is deliberately stricter than unsigned pointer-size representability.
inline std::optional<uint64_t> storage_bytes(mlir::Type type, unsigned depth = 0) {
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
// passed to calls. Callers separately prove each access safe and restrict loop
// effects. Confinement alone makes no no-alias claim: accepted views can overlap.
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

} // namespace gloin_detail
