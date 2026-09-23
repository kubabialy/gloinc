#include "codegen.h"
#include "standard_runtime.h"
#include <algorithm>

std::vector<mlir::Value> CodeGen::gen_call_arguments(const CallExpression *call) {
    std::vector<mlir::Value> arguments;
    if (auto receiver = checked_data->method_receivers.find(call);
        receiver != checked_data->method_receivers.end()) {
        const auto *member = static_cast<const MemberAccessExpression *>(call->function.get());
        arguments.push_back(receiver->second ? gen_address(member->left.get())
                                             : gen_expression(member->left.get()));
    }
    for (const auto &argument : call->arguments)
        arguments.push_back(gen_expression(argument.get()));
    return arguments;
}

mlir::Value CodeGen::emit_checked_call(const CallExpression *call, mlir::ValueRange arguments) {
    if (auto primitive = checked_data->arena_runtime_calls.find(call);
        primitive != checked_data->arena_runtime_calls.end())
        return emit_arena_primitive(primitive->second, arguments);
    if (checked_data->runtime_calls.contains(call)) {
        auto ptr = builder.create<mlir::LLVM::ExtractValueOp>(location(), arguments.front(),
                                                              llvm::ArrayRef<int64_t>{0});
        auto len = builder.create<mlir::LLVM::ExtractValueOp>(location(), arguments.front(),
                                                              llvm::ArrayRef<int64_t>{1});
        auto runtime = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(standard_output_symbol);
        if (!runtime) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(theModule.getBody());
            auto type = mlir::LLVM::LLVMFunctionType::get(
                mlir::LLVM::LLVMVoidType::get(&context),
                {mlir::LLVM::LLVMPointerType::get(&context), builder.getI64Type()}, false);
            runtime =
                builder.create<mlir::LLVM::LLVMFuncOp>(location(), standard_output_symbol, type);
        }
        builder.create<mlir::LLVM::CallOp>(location(), runtime, mlir::ValueRange{ptr, len});
        return {};
    }
    const auto *callee = dynamic_cast<const Identifier *>(call->function.get());
    if (const auto *member = dynamic_cast<const MemberAccessExpression *>(call->function.get()))
        callee = dynamic_cast<const Identifier *>(member->member.get());
    if (!callee)
        fail("Checked call has no direct callee");
    auto found = checked_functions.find(checked_binding(callee));
    if (found == checked_functions.end())
        fail("Checked function has not been emitted");
    if (auto allocation = checked_data->arena_allocations.find(call);
        allocation != checked_data->arena_allocations.end())
        return emit_arena_allocation(found->second, allocation->second, arguments);
    auto result = builder.create<mlir::func::CallOp>(location(), found->second, arguments);
    return result.getNumResults() ? result.getResult(0) : mlir::Value{};
}

mlir::LLVM::LLVMFuncOp CodeGen::defer_allocator(bool allocate) {
    const auto name = allocate ? "malloc" : "free";
    if (auto function = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name))
        return function;
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(theModule.getBody());
    mlir::Type pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto type = mlir::LLVM::LLVMFunctionType::get(
        allocate ? pointer : mlir::LLVM::LLVMVoidType::get(&context),
        {allocate ? builder.getI64Type() : pointer}, false);
    return builder.create<mlir::LLVM::LLVMFuncOp>(location(), name, type);
}

mlir::Value CodeGen::defer_field(mlir::Type record, mlir::Value pointer, unsigned index) {
    return builder.create<mlir::LLVM::GEPOp>(
        location(), mlir::LLVM::LLVMPointerType::get(&context), record, pointer,
        llvm::ArrayRef<mlir::LLVM::GEPArg>{0, static_cast<int32_t>(index)});
}

void CodeGen::prepare_defers(const FunctionDefinition *function) {
    auto found = checked_data->defers.find(function);
    if (found == checked_data->defers.end())
        return;
    current_function_defers = found->second;
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    for (const auto *statement : current_function_defers) {
        const auto *call = static_cast<const CallExpression *>(statement->call.get());
        std::vector<mlir::Type> fields{pointer, builder.getI64Type()};
        if (checked_data->method_receivers.contains(call))
            fields.push_back(pointer);
        for (const auto &argument : call->arguments)
            fields.push_back(checked_type(argument.get()));
        defer_record_types.push_back(mlir::LLVM::LLVMStructType::getLiteral(&context, fields));
    }
    defer_head = create_entry_alloca(pointer);
    auto zero = builder.create<mlir::LLVM::ZeroOp>(location(), pointer);
    builder.create<mlir::LLVM::StoreOp>(location(), zero, defer_head);
}

void CodeGen::register_defer(const DeferStatement *statement) {
    if (!checked_data || !defer_head)
        fail("defer requires checked function registration metadata");
    auto found =
        std::find(current_function_defers.begin(), current_function_defers.end(), statement);
    if (found == current_function_defers.end())
        fail("Missing checked defer site");
    const auto index = found - current_function_defers.begin();
    const auto record = defer_record_types[index];
    const auto *call = static_cast<const CallExpression *>(statement->call.get());
    // Evaluate every capture before allocating/linking a registration. Traps here
    // leave the new call unregistered, with the existing no-unwind semantics.
    auto arguments = gen_call_arguments(call);
    auto size = builder.create<mlir::arith::ConstantIntOp>(location(), get_type_size(record), 64);
    auto allocation = builder.create<mlir::LLVM::CallOp>(location(), defer_allocator(true),
                                                         mlir::ValueRange{size});
    auto node = allocation.getResult();
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto zero = builder.create<mlir::LLVM::ZeroOp>(location(), pointer);
    require_runtime(
        builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne, node, zero));
    auto previous = builder.create<mlir::LLVM::LoadOp>(location(), pointer, defer_head);
    auto tag = builder.create<mlir::arith::ConstantIntOp>(location(), index, 64);
    builder.create<mlir::LLVM::StoreOp>(location(), previous, defer_field(record, node, 0));
    builder.create<mlir::LLVM::StoreOp>(location(), tag, defer_field(record, node, 1));
    for (size_t i = 0; i < arguments.size(); ++i)
        builder.create<mlir::LLVM::StoreOp>(location(), arguments[i],
                                            defer_field(record, node, i + 2));
    builder.create<mlir::LLVM::StoreOp>(location(), node, defer_head);
}

void CodeGen::emit_deferred() {
    if (!defer_head)
        return;
    auto *region = builder.getBlock()->getParent();
    auto *loop = new mlir::Block();
    auto *dispatch = new mlir::Block();
    auto *done = new mlir::Block();
    region->push_back(loop);
    region->push_back(dispatch);
    region->push_back(done);
    branch_if_open(loop);
    builder.setInsertionPointToStart(loop);
    auto pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto node = builder.create<mlir::LLVM::LoadOp>(location(), pointer, defer_head);
    auto zero = builder.create<mlir::LLVM::ZeroOp>(location(), pointer);
    auto active =
        builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne, node, zero);
    builder.create<mlir::cf::CondBranchOp>(location(), active, dispatch, mlir::ValueRange{}, done,
                                           mlir::ValueRange{});
    builder.setInsertionPointToStart(dispatch);
    // All record layouts have the same two-field prefix.
    auto header = mlir::LLVM::LLVMStructType::getLiteral(&context, {pointer, builder.getI64Type()});
    auto previous =
        builder.create<mlir::LLVM::LoadOp>(location(), pointer, defer_field(header, node, 0));
    auto tag = builder.create<mlir::LLVM::LoadOp>(location(), builder.getI64Type(),
                                                  defer_field(header, node, 1));
    builder.create<mlir::LLVM::StoreOp>(location(), previous, defer_head);
    for (size_t i = 0; i < current_function_defers.size(); ++i) {
        auto *invoke = new mlir::Block();
        auto *next = new mlir::Block();
        region->push_back(invoke);
        region->push_back(next);
        auto expected = builder.create<mlir::arith::ConstantIntOp>(location(), i, 64);
        auto matches = builder.create<mlir::arith::CmpIOp>(
            location(), mlir::arith::CmpIPredicate::eq, tag, expected);
        builder.create<mlir::cf::CondBranchOp>(location(), matches, invoke, mlir::ValueRange{},
                                               next, mlir::ValueRange{});
        builder.setInsertionPointToStart(invoke);
        const auto record = defer_record_types[i];
        std::vector<mlir::Value> arguments;
        for (size_t field = 2; field < record.getBody().size(); ++field)
            arguments.push_back(builder.create<mlir::LLVM::LoadOp>(
                location(), record.getBody()[field], defer_field(record, node, field)));
        // Captures are now SSA values. Release the record before invoking user
        // code, so nested defers/recursive calls own only their own frame's log.
        builder.create<mlir::LLVM::CallOp>(location(), defer_allocator(false),
                                           mlir::ValueRange{node});
        const auto *call =
            static_cast<const CallExpression *>(current_function_defers[i]->call.get());
        emit_checked_call(call, arguments);
        branch_if_open(loop);
        builder.setInsertionPointToStart(next);
    }
    builder.create<mlir::LLVM::Trap>(location()); // Corrupt internal registration tag.
    builder.create<mlir::LLVM::UnreachableOp>(location());
    builder.setInsertionPointToStart(done);
}
