#ifndef GLOINC_TARGET_LAYOUT_H
#define GLOINC_TARGET_LAYOUT_H
#include "mlir/IR/Types.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

struct TargetLayout {
    std::string triple;
    std::string data_layout;
};
struct TypeLayout {
    uint64_t size;
    uint64_t alignment;
    std::vector<uint64_t> field_offsets;
};
llvm::Expected<TargetLayout> native_target_layout();
llvm::Expected<TypeLayout> measure_type_layout(mlir::Type type, const llvm::DataLayout &layout);
#endif
