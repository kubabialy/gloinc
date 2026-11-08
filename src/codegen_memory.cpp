#include "codegen.h"
#include "memory_lowering.h"
#include "target_layout.h"
#include <limits>

mlir::Value CodeGen::emit_memory_primitive(MemoryPrimitive kind, mlir::ValueRange arguments) {
    const auto name = memory_runtime_names.at(static_cast<size_t>(kind));
    auto function = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
    if (!function) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(theModule.getBody());
        function = builder.create<mlir::LLVM::LLVMFuncOp>(location(), name,
                                                          memory_runtime_type(context, kind));
    }
    return emit_abi_call(function, arguments);
}

mlir::Value CodeGen::emit_memory_intrinsic(MemoryIntrinsic kind, const ValueType &type,
                                           mlir::ValueRange arguments) {
    auto target = native_target_layout();
    if (!target)
        fail(llvm::toString(target.takeError()));
    auto element_type = lower_type(type);
    auto layout = measure_type_layout(element_type, llvm::DataLayout(target->data_layout));
    if (!layout)
        fail(llvm::toString(layout.takeError()));
    if (layout->size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        layout->alignment > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("Raw memory element layout exceeds the signed size range");
    if (kind == MemoryIntrinsic::SizeOf || kind == MemoryIntrinsic::AlignOf)
        return builder.create<mlir::arith::ConstantIntOp>(
            location(), kind == MemoryIntrinsic::SizeOf ? layout->size : layout->alignment, 64);

    if (kind == MemoryIntrinsic::IsAligned) {
        ValueType raw(CoreType::U8);
        raw.pointers.push_back({true, false});
        return builder.create<gloin::RawIsAlignedOp>(
            location(), builder.getI1Type(), as_source_value(arguments[0], source_type(raw)),
            arguments[1]);
    }

    if (kind == MemoryIntrinsic::PackedPointer || kind == MemoryIntrinsic::PackedMutPointer) {
        const bool read_only = kind == MemoryIntrinsic::PackedPointer;
        ValueType raw(CoreType::U8);
        raw.pointers.push_back({true, read_only});
        auto pointer = type;
        pointer.pointers.insert(pointer.pointers.begin(), {true, read_only});
        auto source = source_type(pointer);
        auto view = builder.create<gloin::RawPackedPointerOp>(
            location(), source, as_source_value(arguments[0], source_type(raw)),
            mlir::TypeAttr::get(source));
        return builder.create<gloin::ToLayoutOp>(location(), arguments[0].getType(), view);
    }
    if (kind == MemoryIntrinsic::Padding) {
        ValueType byte_pointer(CoreType::U8);
        byte_pointer.pointers.push_back({true, false});
        return builder.create<gloin::RawPaddingOp>(
            location(), builder.getI64Type(),
            as_source_value(arguments[0], source_type(byte_pointer)),
            mlir::TypeAttr::get(element_type));
    }

    ValueType byte_pointer(CoreType::U8);
    byte_pointer.pointers.push_back({true, false});
    auto result = type;
    result.pointers.insert(result.pointers.begin(), {true, false});
    auto result_source = source_type(result);
    if (kind == MemoryIntrinsic::Reserve) {
        auto reserved = builder.create<gloin::RawReserveOp>(
            location(), result_source, as_source_value(arguments[0], source_type(byte_pointer)),
            arguments[1], arguments[2], mlir::TypeAttr::get(result_source),
            mlir::TypeAttr::get(element_type));
        return builder.create<gloin::ToLayoutOp>(location(), arguments[0].getType(), reserved);
    }
    auto placed = builder.create<gloin::RawPlaceOp>(
        location(), result_source, as_source_value(arguments[0], source_type(byte_pointer)),
        arguments[1], as_source_value(arguments[2], source_type(type)),
        mlir::TypeAttr::get(result_source), mlir::TypeAttr::get(source_type(type)),
        mlir::TypeAttr::get(element_type));
    return builder.create<gloin::ToLayoutOp>(location(), arguments[0].getType(), placed);
}
