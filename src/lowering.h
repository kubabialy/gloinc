#pragma once

#include "diagnostics.h"
#include "compilation_mode.h"
#include "mlir/IR/BuiltinOps.h"
class CompilationTimings;

// The caller's context must outlive every module. Verification borrows the IR.
bool verify_module(mlir::ModuleOp module, Diagnostics &diagnostics,
                   std::shared_ptr<const SourceFile> source = {});

// Consumes exclusive module ownership. Failure destroys partial IR and returns
// no module. Callers retaining high-level IR must explicitly clone before moving.
// Executable mode prunes checked GIR functions unreachable from main. Module
// and Object preserve definitions, as does arbitrary/previously lowered MLIR.
mlir::OwningOpRef<mlir::ModuleOp> lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp> module,
                                                Diagnostics &diagnostics,
                                                std::shared_ptr<const SourceFile> source = {},
                                                CompilationTimings *timings = nullptr,
                                                CompilationMode mode = CompilationMode::Module);
