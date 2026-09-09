#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace gloin_test {
struct ToolCommand {
    std::string path;
    std::vector<std::string> arguments;
};

// Failure is separate from the program's i32 result, including a valid -1.
llvm::Expected<int> run_external_mlir(llvm::StringRef source, const ToolCommand &optimizer,
                                      const ToolCommand &runner, unsigned timeout_seconds = 10);
} // namespace gloin_test
