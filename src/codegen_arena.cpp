#include "arena_lowering.h"
#include "codegen.h"
#include "target_layout.h"
#include <limits>

mlir::Value CodeGen::emit_arena_primitive(ArenaPrimitive kind, mlir::ValueRange arguments) {
    if (kind == ArenaPrimitive::Require) {
        auto zero = builder.create<mlir::LLVM::ZeroOp>(location(),
                                                       mlir::LLVM::LLVMPointerType::get(&context));
        require_runtime(builder.create<mlir::LLVM::ICmpOp>(
            location(), mlir::LLVM::ICmpPredicate::ne, arguments.front(), zero));
        return {};
    }
    const auto name = arena_runtime_names.at(static_cast<size_t>(kind));
    auto function = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
    if (!function) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(theModule.getBody());
        function = builder.create<mlir::LLVM::LLVMFuncOp>(location(), name,
                                                          arena_runtime_type(context, kind));
    }
    return emit_abi_call(function, arguments);
}

mlir::Value CodeGen::emit_arena_allocation(mlir::func::FuncOp method, bool nullable,
                                           mlir::ValueRange arguments,
                                           mlir::Type result_source) {
    auto target = native_target_layout();
    if (!target)
        fail(llvm::toString(target.takeError()));
    auto layout =
        measure_type_layout(arguments[1].getType(), llvm::DataLayout(target->data_layout));
    if (!layout)
        fail(llvm::toString(layout.takeError()));
    auto size = builder.create<mlir::arith::ConstantIntOp>(location(), layout->size, 64);
    auto alignment = builder.create<mlir::arith::ConstantIntOp>(location(), layout->alignment, 64);
    auto storage = emit_checked_function_call(
        method, mlir::ValueRange{arguments[0], size, alignment});
    auto raw_source = method.getFunctionType().getResult(0);
    auto typed = builder.create<gloin::ArenaTypedPointerOp>(
        location(), result_source, as_source_value(storage, raw_source),
        mlir::TypeAttr::get(result_source),
        mlir::TypeAttr::get(arguments[1].getType()));
    auto typed_storage = builder.create<gloin::ToLayoutOp>(
        location(), storage.getType(), typed);
    if (!nullable) {
        builder.create<mlir::LLVM::StoreOp>(location(), arguments[1], typed_storage);
    } else {
        auto zero = builder.create<mlir::LLVM::ZeroOp>(location(), storage.getType());
        auto success = builder.create<mlir::LLVM::ICmpOp>(
            location(), mlir::LLVM::ICmpPredicate::ne, storage, zero);
        auto *region = builder.getBlock()->getParent();
        auto *initialize = new mlir::Block();
        auto *done = new mlir::Block();
        region->push_back(initialize);
        region->push_back(done);
        builder.create<mlir::cf::CondBranchOp>(location(), success, initialize, mlir::ValueRange{},
                                               done, mlir::ValueRange{});
        builder.setInsertionPointToStart(initialize);
        builder.create<mlir::LLVM::StoreOp>(location(), arguments[1], typed_storage);
        branch_if_open(done);
        builder.setInsertionPointToStart(done);
    }
    return typed_storage;
}

mlir::Value CodeGen::emit_arena_many_allocation(mlir::func::FuncOp method,
                                                mlir::ValueRange arguments,
                                                mlir::Type result_source) {
    auto target = native_target_layout();
    if (!target)
        fail(llvm::toString(target.takeError()));
    auto layout = measure_type_layout(arguments[1].getType(),
                                      llvm::DataLayout(target->data_layout));
    if (!layout)
        fail(llvm::toString(layout.takeError()));
    if (layout->size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("Arena element size exceeds the supported signed allocation range");
    auto element_size = builder.create<mlir::arith::ConstantIntOp>(
        location(), layout->size, 64);
    auto alignment = builder.create<mlir::arith::ConstantIntOp>(
        location(), layout->alignment, 64);
    auto zero = builder.create<mlir::arith::ConstantIntOp>(location(), 0, 64);
    require_runtime(builder.create<mlir::arith::CmpIOp>(
        location(), mlir::arith::CmpIPredicate::sge, arguments[2], zero));
    auto byte_count = checked_integer_arithmetic("*", element_size, arguments[2], CoreType::I64);
    auto storage = emit_checked_function_call(
        method, mlir::ValueRange{arguments[0], byte_count, alignment});
    auto raw_source = method.getFunctionType().getResult(0);
    auto typed = builder.create<gloin::ArenaTypedPointerOp>(
        location(), result_source, as_source_value(storage, raw_source),
        mlir::TypeAttr::get(result_source),
        mlir::TypeAttr::get(arguments[1].getType()));
    auto element_source = mlir::cast<gloin::GloinPointerType>(result_source).getPointee();
    builder.create<gloin::ArenaFillOp>(
        location(), typed, as_source_value(arguments[1], element_source), arguments[2],
        mlir::TypeAttr::get(result_source), mlir::TypeAttr::get(element_source),
        mlir::TypeAttr::get(arguments[1].getType()));
    return builder.create<gloin::ToLayoutOp>(
        location(), mlir::LLVM::LLVMPointerType::get(&context), typed);
}
