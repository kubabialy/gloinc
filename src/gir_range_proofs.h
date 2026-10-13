#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallPtrSet.h"

// Internal, transient proofs over GIR after source layouts have been resolved.
// Nothing in textual IR can request an unchecked operation. The lowering
// patterns consume these sets immediately, before the analyzed operations die.
// Analysis also versions eligible counted slice loops, retaining a checked
// fallback. Arithmetic proofs include the fast copy's signed/unsigned +1.
struct GloinRangeProofs {
    llvm::SmallPtrSet<mlir::Operation *, 32> arithmetic;
    llvm::SmallPtrSet<mlir::Operation *, 32> bounds;
};

GloinRangeProofs prove_gloin_ranges(mlir::ModuleOp module);
