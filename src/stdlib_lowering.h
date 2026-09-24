#pragma once
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "stdlib_abi.h"

inline mlir::LLVM::LLVMFunctionType standard_runtime_type(mlir::MLIRContext &context,
                                                          StandardPrimitive kind) {
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto i32 = mlir::IntegerType::get(&context, 32);
    auto i64 = mlir::IntegerType::get(&context, 64);
    auto nothing = mlir::LLVM::LLVMVoidType::get(&context);
    if (kind >= StandardPrimitive::MathUnaryF32 && kind <= StandardPrimitive::MathBinaryF64) {
        mlir::Type scalar =
            kind == StandardPrimitive::MathUnaryF32 || kind == StandardPrimitive::MathBinaryF32
                ? mlir::Type(mlir::Float32Type::get(&context))
                : mlir::Type(mlir::Float64Type::get(&context));
        llvm::SmallVector<mlir::Type> args{i32, scalar};
        if (kind == StandardPrimitive::MathBinaryF32 || kind == StandardPrimitive::MathBinaryF64)
            args.push_back(scalar);
        args.push_back(pointer);
        return mlir::LLVM::LLVMFunctionType::get(i32, args, false);
    }
    switch (kind) {
    case StandardPrimitive::TimeMonotonic:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false);
    case StandardPrimitive::RandomSplitMix64:
        return mlir::LLVM::LLVMFunctionType::get(i64, {pointer}, false);
    case StandardPrimitive::FsMetadata:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, pointer, pointer},
                                                 false);
    case StandardPrimitive::FsMkdir:
    case StandardPrimitive::FsRemoveFile:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer}, false);
    case StandardPrimitive::FsRenameReplace:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer}, false);
    case StandardPrimitive::ProcessCount:
        return mlir::LLVM::LLVMFunctionType::get(i64, {}, false);
    case StandardPrimitive::ProcessArg:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessEnv:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessCwd:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::IoStandard:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {i32}, false);
    case StandardPrimitive::IoOpen:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, i64, i32, pointer, pointer},
                                                 false);
    case StandardPrimitive::IoRead:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, i64, i32, pointer, pointer}, false);
    case StandardPrimitive::IoWrite:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, i64, i32, pointer, pointer}, false);
    case StandardPrimitive::IoFlush:
    case StandardPrimitive::IoClose:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false);
    case StandardPrimitive::IoErrorMessage:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, pointer}, false);
    default:
        break;
    }
    if (auto spec = numeric_signature(kind)) {
        if (spec->group == NumericPrimitiveGroup::Parse)
            return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer}, false);
        mlir::Type input = i32;
        if (spec->input == std::string_view("f32"))
            input = mlir::Float32Type::get(&context);
        else if (spec->input == std::string_view("f64"))
            input = mlir::Float64Type::get(&context);
        else if (spec->input != std::string_view("i32"))
            input = i64;
        llvm::SmallVector<mlir::Type> arguments{input};
        if (spec->mode || spec->group == NumericPrimitiveGroup::Fixed)
            arguments.push_back(i32);
        if (spec->group != NumericPrimitiveGroup::Convert)
            arguments.push_back(pointer);
        arguments.push_back(pointer);
        return mlir::LLVM::LLVMFunctionType::get(i32, arguments, false);
    }
    if (kind == StandardPrimitive::FormatI32)
        return mlir::LLVM::LLVMFunctionType::get(nothing, {i32, pointer, pointer}, false);
    if (kind == StandardPrimitive::StringCopy)
        return mlir::LLVM::LLVMFunctionType::get(nothing, {pointer, i64, pointer}, false);
    return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer}, false);
}
