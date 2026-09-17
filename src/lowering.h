#pragma once

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"

// The caller's context must outlive every module. Verification borrows the IR.
bool verify_module(mlir::ModuleOp module, Diagnostics &diagnostics,
                   std::shared_ptr<const SourceFile> source = {});

// Consumes exclusive module ownership. Failure destroys partial IR and returns
// no module. Callers retaining high-level IR must explicitly clone before moving.
mlir::OwningOpRef<mlir::ModuleOp> lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp> module,
                                                Diagnostics &diagnostics,
                                                std::shared_ptr<const SourceFile> source = {});
