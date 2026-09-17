#ifndef GLOINC_COMPILER_H
#define GLOINC_COMPILER_H

#include "compilation_mode.h"
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

enum class CompilationOutput { HighLevel, LLVM };

// Stops after the first failed stage. Both output forms are verified; LLVM uses
// the shared lowering pipeline. Execution and the file CLI remain SPEC-019/020.
// Module mode permits helper-only source. Use Executable before running main.
CompilationResult compile_source(std::string text, std::string filename, mlir::MLIRContext &context,
                                 CompilationMode mode = CompilationMode::Module,
                                 CompilationOutput output = CompilationOutput::HighLevel);

#endif
