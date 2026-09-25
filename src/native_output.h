#pragma once

#include "diagnostics.h"
#include "mlir/IR/BuiltinOps.h"
#include <string>

enum class NativeOutput { Object, Executable };
enum class NativeOptimization { O0, O2 };

// Consumes verified LLVM-dialect MLIR. Output is replaced only after emission
// (and, for executables, linking) succeeds.
bool emit_native(mlir::ModuleOp module, const std::string &output, NativeOutput kind,
                 NativeOptimization optimization, const std::string &compiler_path,
                 Diagnostics &diagnostics);
