#pragma once
#include "memory_abi.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

inline mlir::LLVM::LLVMFunctionType memory_runtime_type(mlir::MLIRContext &context,
                                                       MemoryPrimitive kind) {
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto integer = mlir::IntegerType::get(&context, 64);
    if (kind == MemoryPrimitive::Allocate)
        return mlir::LLVM::LLVMFunctionType::get(pointer, {integer, integer}, false);
    return mlir::LLVM::LLVMFunctionType::get(
        mlir::LLVM::LLVMVoidType::get(&context), {pointer}, false);
}
