#pragma once

#include "mlir/Dialect/Func/IR/FuncOps.h"

struct GloinRangeProofs;

// After slot promotion, version ordered dot products over proven local storage.
// Missing provenance/effect/failure facts leave the original loop untouched.
void version_gloin_float_reductions(mlir::func::FuncOp function, GloinRangeProofs &proofs);
