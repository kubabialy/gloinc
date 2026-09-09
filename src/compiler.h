#ifndef GLOINC_COMPILER_H
#define GLOINC_COMPILER_H

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"
#include <optional>

struct CompilationResult {
    // The caller's MLIR context must outlive the result and its owned module.
    mlir::OwningOpRef<mlir::ModuleOp> module;
    std::shared_ptr<Diagnostics> diagnostics;
    std::optional<DiagnosticStage> failed_stage;
    bool success() const { return module && !diagnostics->has_errors(); }
};

// Stops after the first failed stage. Lowering/execution and the file CLI remain
// SPEC-018 through SPEC-020; this API produces the current high-level module.
CompilationResult compile_source(std::string text, std::string filename,
                                 mlir::MLIRContext &context);

#endif
