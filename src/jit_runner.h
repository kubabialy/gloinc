#ifndef JIT_RUNNER_H
#define JIT_RUNNER_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include <memory>
#include <string>

class JitRunner {
public:
    static int run(mlir::ModuleOp module);
};

#endif // JIT_RUNNER_H
