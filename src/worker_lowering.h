#pragma once
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"

namespace gloin::workers {
inline constexpr llvm::StringLiteral launch_name = "gloin_worker_launch";
inline constexpr llvm::StringLiteral join_name = "gloin_worker_join";
mlir::LLVM::LLVMFunctionType runtime_type(mlir::MLIRContext *context, llvm::StringRef name);
mlir::LogicalResult lower_launches(mlir::ModuleOp module);
} // namespace gloin::workers
