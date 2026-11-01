#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallPtrSet.h"

// Propagate finite magnitude bounds through SSA locals and CFG joins. Remove
// only checks proved unable to fail; retain ordinary IEEE operation order.
void remove_proven_gloin_float_checks(mlir::ModuleOp module);

// Recompute safety on the current IR, including checks retained for scalar
// profitability. Consume these facts before changing their operands/anchors.
void find_proven_gloin_float_checks(mlir::func::FuncOp function,
                                   llvm::SmallPtrSetImpl<mlir::Operation *> &safe);

// On verified GIR, combine single-use +, -, * checks within bounded contiguous
// runs. Every chain retains a mandatory check before the next operation.
void combine_gloin_float_checks(mlir::ModuleOp module);
