#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace gloin_test {
struct ToolCommand {
    std::string path;
    std::vector<std::string> arguments;
};

// Consumes a clone through the production lowering pipeline before using tools.
llvm::Expected<int> run_external_module(mlir::ModuleOp module, const ToolCommand &optimizer,
                                        const ToolCommand &runner, unsigned timeout_seconds = 10);

// Input is already lowered; the optimizer parses/verifies it without conversions.
// Failure is separate from the program's i32 result, including a valid -1.
llvm::Expected<int> run_external_mlir(llvm::StringRef source, const ToolCommand &optimizer,
                                      const ToolCommand &runner, unsigned timeout_seconds = 10);
} // namespace gloin_test
