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
    case StandardPrimitive::NetOpen:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false);
    case StandardPrimitive::NetFinishConnect:
    case StandardPrimitive::NetClose:
    case StandardPrimitive::NetShutdownWrite:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer}, false);
    case StandardPrimitive::NetBind:
    case StandardPrimitive::NetConnect:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, i32, mlir::IntegerType::get(&context, 16), pointer}, false);
    case StandardPrimitive::NetReuseAddress:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, i32, pointer}, false);
    case StandardPrimitive::NetListen:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, i32, pointer}, false);
    case StandardPrimitive::NetAccept:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, pointer, pointer, pointer}, false);
    case StandardPrimitive::NetWait:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, i32, i32, pointer, pointer}, false);
    case StandardPrimitive::NetWaitMany:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, pointer, i64, i32, pointer, pointer}, false);
    case StandardPrimitive::NetResolveIpv4:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, i64, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::NetRecv:
    case StandardPrimitive::NetSend:
    case StandardPrimitive::NetSendText:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::NetLocal:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, pointer, pointer}, false);
    case StandardPrimitive::NetTlsCreate:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {i32, pointer, i64, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::NetTlsHandshake:
    case StandardPrimitive::NetTlsShutdown:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false);
    case StandardPrimitive::NetTlsServerConfig:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::NetTlsServerCreate:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, pointer, pointer}, false);
    case StandardPrimitive::NetTlsRead:
    case StandardPrimitive::NetTlsWrite:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, i64, pointer, pointer, pointer}, false);
    case StandardPrimitive::NetTlsClose:
    case StandardPrimitive::NetTlsServerConfigClose:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer}, false);
    case StandardPrimitive::TimeMonotonic:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false);
    case StandardPrimitive::RandomSplitMix64:
        return mlir::LLVM::LLVMFunctionType::get(i64, {pointer}, false);
    case StandardPrimitive::FsMetadata:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, pointer, pointer},
                                                 false);
    case StandardPrimitive::FsMkdir:
    case StandardPrimitive::FsRemoveDir:
    case StandardPrimitive::FsRemoveFile:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer}, false);
    case StandardPrimitive::FsSymlink:
    case StandardPrimitive::FsRenameReplace:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer}, false);
    case StandardPrimitive::FsReplaceFile:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer, i64, pointer}, false);
    case StandardPrimitive::FsCanonicalPath:
    case StandardPrimitive::FsTempDir:
    case StandardPrimitive::FsReadLink:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::FsDirOpen:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::FsDirNext:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, pointer, pointer, pointer}, false);
    case StandardPrimitive::FsDirClose:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false);
    case StandardPrimitive::ProcessCount:
        return mlir::LLVM::LLVMFunctionType::get(i64, {}, false);
    case StandardPrimitive::ProcessArg:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessEnv:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessCwd:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessStart:
        return mlir::LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessWait:
    case StandardPrimitive::ProcessObserve:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i64, i32, pointer, pointer, pointer}, false);
    case StandardPrimitive::ProcessSignal:
    case StandardPrimitive::ProcessGroupSignal:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i64, i32, pointer}, false);
    case StandardPrimitive::ProcessStartPiped:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, {pointer, i64, pointer, i64, pointer, pointer, pointer, pointer, pointer}, false);
    case StandardPrimitive::ProcessPipeRead:
    case StandardPrimitive::ProcessPipeWrite:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer, i64, pointer, pointer}, false);
    case StandardPrimitive::ProcessPipeClose:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, pointer}, false);
    case StandardPrimitive::ProcessPipeWait:
        return mlir::LLVM::LLVMFunctionType::get(i32, {i32, i32, i32, i32, pointer, pointer}, false);
    case StandardPrimitive::ProcessStartOptions:
        return mlir::LLVM::LLVMFunctionType::get(i32,
            {pointer, i64, pointer, i64, pointer, i64, pointer, i64, i32, i32, i32, i64,
             pointer, pointer, pointer, pointer, pointer}, false);
    case StandardPrimitive::IoStandard:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {i32}, false);
    case StandardPrimitive::IoOpen:
        return mlir::LLVM::LLVMFunctionType::get(pointer, {pointer, i64, i32, pointer, pointer},
                                                 false);
    case StandardPrimitive::IoRead:
    case StandardPrimitive::IoReadBytes:
        return mlir::LLVM::LLVMFunctionType::get(
            i32, kind == StandardPrimitive::IoRead
                     ? llvm::SmallVector<mlir::Type>{pointer, pointer, i64, i32, pointer, pointer}
                     : llvm::SmallVector<mlir::Type>{pointer, pointer, i64, pointer, pointer},
            false);
    case StandardPrimitive::IoWrite:
    case StandardPrimitive::IoWriteBytes:
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
        else if (spec->input == std::string_view("u8"))
            input = mlir::IntegerType::get(&context, 8);
        else if (spec->input == std::string_view("u16"))
            input = mlir::IntegerType::get(&context, 16);
        else if (spec->input != std::string_view("i8") &&
                 spec->input != std::string_view("i16") &&
                 spec->input != std::string_view("i32") &&
                 spec->input != std::string_view("u32"))
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
