#ifndef JIT_RUNNER_H
#define JIT_RUNNER_H

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"
#include "time_runtime.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    // Arguments are copied for this invocation; no synthetic argument zero.
    // Embedded NUL is rejected; nested/thread-local contexts restore on return.
    // Clock descriptor is copied, userdata borrowed during the run. nullptr explicitly
    // selects the OS clock; invocation scopes restore the prior host provider.
    static ExecutionResult run(mlir::ModuleOp module,
                               const std::vector<std::string> &arguments = {},
                               const GloinClockSource *clock = nullptr);
};

#endif // JIT_RUNNER_H
