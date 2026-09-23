#include "arena_lowering.h"
#include "codegen.h"
#include "target_layout.h"

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
    auto result = builder.create<mlir::LLVM::CallOp>(location(), function, arguments);
    return result.getNumResults() ? result.getResult() : mlir::Value{};
}

mlir::Value CodeGen::emit_arena_allocation(mlir::func::FuncOp method, bool nullable,
                                           mlir::ValueRange arguments) {
    auto target = native_target_layout();
    if (!target)
        fail(llvm::toString(target.takeError()));
    auto layout =
        measure_type_layout(arguments[1].getType(), llvm::DataLayout(target->data_layout));
    if (!layout)
        fail(llvm::toString(layout.takeError()));
    auto size = builder.create<mlir::arith::ConstantIntOp>(location(), layout->size, 64);
    auto alignment = builder.create<mlir::arith::ConstantIntOp>(location(), layout->alignment, 64);
    auto call = builder.create<mlir::func::CallOp>(location(), method,
                                                   mlir::ValueRange{arguments[0], size, alignment});
    auto storage = call.getResult(0);
    auto zero = builder.create<mlir::LLVM::ZeroOp>(location(), storage.getType());
    auto success = builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne,
                                                      storage, zero);
    if (!nullable) {
        require_runtime(success);
        builder.create<mlir::LLVM::StoreOp>(location(), arguments[1], storage);
    } else {
        auto *region = builder.getBlock()->getParent();
        auto *initialize = new mlir::Block();
        auto *done = new mlir::Block();
        region->push_back(initialize);
        region->push_back(done);
        builder.create<mlir::cf::CondBranchOp>(location(), success, initialize, mlir::ValueRange{},
                                               done, mlir::ValueRange{});
        builder.setInsertionPointToStart(initialize);
        builder.create<mlir::LLVM::StoreOp>(location(), arguments[1], storage);
        branch_if_open(done);
        builder.setInsertionPointToStart(done);
    }
    return storage;
}
