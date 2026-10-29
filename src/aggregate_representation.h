#pragma once
#include "mlir/IR/BuiltinOps.h"

// Make object padding explicit before LLVM export, including Gloin call values.
// Physical sizes, alignments, and source field offsets remain unchanged.
mlir::LogicalResult preserve_aggregate_bytes(mlir::ModuleOp module);
