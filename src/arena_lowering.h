#pragma once
#include "arena_abi.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

inline mlir::LLVM::LLVMFunctionType arena_runtime_type(mlir::MLIRContext &context,
                                                       ArenaPrimitive kind) {
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto integer = mlir::IntegerType::get(&context, 64);
    auto nothing = mlir::LLVM::LLVMVoidType::get(&context);
    if (kind == ArenaPrimitive::Create)
        return mlir::LLVM::LLVMFunctionType::get(pointer, {}, false);
    if (kind == ArenaPrimitive::Allocate)
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, integer, integer}, false);
    return mlir::LLVM::LLVMFunctionType::get(nothing, {pointer}, false);
}
