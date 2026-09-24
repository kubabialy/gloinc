#pragma once

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"
#include <string>

enum class NativeOutput { Object, Executable };

// Consumes verified LLVM-dialect MLIR. Output is replaced only after emission
// (and, for executables, linking) succeeds.
bool emit_native(mlir::ModuleOp module, const std::string &output, NativeOutput kind,
                 const std::string &compiler_path, Diagnostics &diagnostics);
