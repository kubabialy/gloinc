#ifndef JIT_RUNNER_H
#define JIT_RUNNER_H

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"
#include <cstdint>
#include <optional>

struct ExecutionResult {
    std::optional<int32_t> value;
    std::shared_ptr<Diagnostics> diagnostics;
    std::optional<DiagnosticStage> failed_stage;
    bool success() const { return value.has_value() && !diagnostics->has_errors(); }
};

class JitRunner {
  public:
    // Borrows the module/context synchronously and lowers an owned clone.
    // Setup/invocation errors return no value. Runtime arithmetic traps terminate
    // the calling process; this API does not install signal recovery handlers.
    static ExecutionResult run(mlir::ModuleOp module);
};

#endif // JIT_RUNNER_H
