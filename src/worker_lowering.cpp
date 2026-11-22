#include "worker_lowering.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "target_layout.h"
#include <cstddef>

using namespace mlir;

LLVM::LLVMFunctionType gloin::workers::runtime_type(MLIRContext *context, StringRef name) {
    auto ptr = LLVM::LLVMPointerType::get(context);
    auto size = IntegerType::get(context, 64);
    auto nothing = LLVM::LLVMVoidType::get(context);
    if (name == launch_name)
        return LLVM::LLVMFunctionType::get(nothing, {ptr, ptr, ptr, size, size}, false);
    assert(name == join_name);
    return LLVM::LLVMFunctionType::get(nothing, {ptr, ptr, size}, false);
}

namespace {
using namespace gloin;
using namespace gloin::workers;

LLVM::LLVMFuncOp declare_runtime(ModuleOp module, StringRef name) {
    OpBuilder b(module.getContext());
    b.setInsertionPointToEnd(module.getBody());
    auto type = runtime_type(module.getContext(), name);
    if (auto *existing = module.lookupSymbol(name)) {
        auto function = dyn_cast<LLVM::LLVMFuncOp>(existing);
        if (function && function.isExternal() && function.getFunctionType() == type &&
            function.getCConv() == LLVM::CConv::C &&
            function.getLinkage() == LLVM::Linkage::External)
            return function;
        existing->emitError("conflicting worker runtime declaration");
        return {};
    }
    return b.create<LLVM::LLVMFuncOp>(module.getLoc(), name, type);
}

Value integer(OpBuilder &b, Location loc, uint64_t n) {
    return b.create<LLVM::ConstantOp>(loc, b.getI64Type(), b.getI64IntegerAttr(n));
}

// The caller needs these slots only during the runtime call. Put them in the
// entry block so repeated launches in a loop use a constant amount of stack.
Value slot(OpBuilder &b, Operation *use, Type type) {
    OpBuilder::InsertionGuard guard(b);
    b.setInsertionPointToStart(&use->getParentOfType<func::FuncOp>().getBody().front());
    return b.create<LLVM::AllocaOp>(use->getLoc(), LLVM::LLVMPointerType::get(b.getContext()), type,
                                    integer(b, use->getLoc(), 1), 0);
}

Value as_layout(OpBuilder &b, Location loc, Value value, Type layout) {
    if (value.getType() == layout)
        return value;
    return b.create<ToLayoutOp>(loc, layout, value);
}

Value as_source(OpBuilder &b, Location loc, Value value, Type source) {
    if (value.getType() == source)
        return value;
    return b.create<FromLayoutOp>(loc, source, value);
}

struct Adapter {
    LLVM::LLVMFuncOp function;
    LLVM::LLVMStructType arguments;
    LLVM::LLVMStructType completion;
    uint64_t argument_size;
    uint64_t completion_size;
};

FailureOr<Adapter> make_adapter(ModuleOp module, func::FuncOp worker, Type payload,
                                LLVM::LLVMStructType error, const llvm::DataLayout &layout) {
    OpBuilder b(module.getContext());
    auto loc = worker.getLoc();
    auto ptr = LLVM::LLVMPointerType::get(b.getContext());
    auto inputs = worker->getAttrOfType<ArrayAttr>("gloin.layout_inputs");
    auto results = worker->getAttrOfType<ArrayAttr>("gloin.layout_results");
    if (!inputs || !results) {
        worker.emitError("worker requires a checked layout signature");
        return failure();
    }
    SmallVector<Type> fields;
    for (auto input : inputs)
        fields.push_back(cast<TypeAttr>(input).getValue());
    auto args = LLVM::LLVMStructType::getLiteral(b.getContext(), fields);
    auto signature = worker.getFunctionType();
    bool fallible = signature.getNumResults() && isa<GloinResultType>(signature.getResult(0));
    auto result = GloinResultType::get(b.getContext(), payload);
    SmallVector<Type> completionFields{b.getI1Type()};
    if (!results.empty() && !fallible)
        completionFields.push_back(cast<TypeAttr>(results[0]).getValue());
    completionFields.push_back(error);
    auto completion = fallible ? cast<LLVM::LLVMStructType>(cast<TypeAttr>(results[0]).getValue())
                               : LLVM::LLVMStructType::getLiteral(b.getContext(), completionFields);
    auto inputSize = measure_type_layout(args, layout);
    auto outputSize = measure_type_layout(completion, layout);
    if (!inputSize || !outputSize) {
        worker.emitError(
            llvm::toString(llvm::joinErrors(inputSize.takeError(), outputSize.takeError())));
        return failure();
    }

    if (inputSize->alignment > alignof(std::max_align_t) ||
        outputSize->alignment > alignof(std::max_align_t)) {
        worker.emitError("worker frame exceeds the native allocator alignment");
        return failure();
    }

    // A distinct C callback has a pointer-only signature. Its direct Gloin
    // call goes through the same aggregate and endian lowering as a sync call.
    b.setInsertionPointToEnd(module.getBody());
    auto callbackType =
        LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(b.getContext()), {ptr, ptr}, false);
    auto callback =
        b.create<LLVM::LLVMFuncOp>(loc, ("gloin.worker.entry." + worker.getName()).str(),
                                   callbackType, LLVM::Linkage::Internal);
    auto *entry = callback.addEntryBlock(b);
    b.setInsertionPointToStart(entry);
    Value captured;
    if (!fields.empty()) {
        if (inputSize->size)
            captured = b.create<LLVM::LoadOp>(loc, args, entry->getArgument(0));
        else
            captured = b.create<LLVM::ZeroOp>(loc, args);
    }
    SmallVector<Value> arguments;
    for (auto [i, type] : llvm::enumerate(signature.getInputs())) {
        auto field = b.create<LLVM::ExtractValueOp>(loc, captured,
                                                    ArrayRef<int64_t>{static_cast<int64_t>(i)});
        arguments.push_back(as_source(b, loc, field, type));
    }
    auto call = b.create<func::CallOp>(loc, worker, arguments);
    Value outcome;
    if (fallible)
        outcome = call.getResult(0);
    else
        outcome =
            b.create<ResultSuccessOp>(loc, result, call.getResults(), TypeAttr::get(completion));
    b.create<LLVM::StoreOp>(loc, as_layout(b, loc, outcome, completion), entry->getArgument(1));
    b.create<LLVM::ReturnOp>(loc, ValueRange{});
    return Adapter{callback, args, completion, inputSize->size, outputSize->size};
}
} // namespace

LogicalResult gloin::workers::lower_launches(ModuleOp module) {
    SmallVector<SpawnOp> launches;
    module.walk([&](SpawnOp op) { launches.push_back(op); });
    if (launches.empty())
        return success();
    auto launchFn = declare_runtime(module, launch_name);
    auto joinFn = declare_runtime(module, join_name);
    if (!launchFn || !joinFn)
        return failure();
    auto attr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!attr)
        return module.emitError("worker frames require a target data layout");
    auto layout = llvm::DataLayout::parse(attr.getValue());
    if (!layout)
        return module.emitError(llvm::toString(layout.takeError()));
    // The C launcher writes a native receipt and memcpy uses these exact byte sizes.
    auto host = native_target_layout();
    if (!host)
        return module.emitError(llvm::toString(host.takeError()));
    if (*layout != llvm::DataLayout(host->data_layout))
        return module.emitError("worker frames require the native runtime data layout");
    OpBuilder b(module.getContext());
    auto ptr = LLVM::LLVMPointerType::get(b.getContext());
    auto string = LLVM::LLVMStructType::getIdentified(b.getContext(), "gloin.string");
    if (!string.isInitialized() || string.isOpaque() ||
        !llvm::equal(string.getBody(), ArrayRef<Type>{ptr, b.getI64Type()}) || string.isPacked())
        return module.emitError("worker launches require the checked string layout");
    auto error =
        LLVM::LLVMStructType::getLiteral(b.getContext(), {string, b.getI32Type(), b.getI32Type()});
    auto receipt = LLVM::LLVMStructType::getLiteral(b.getContext(), {b.getI1Type(), ptr, error});
    llvm::DenseMap<Operation *, Adapter> adapters;
    for (auto launch : launches) {
        auto worker = module.lookupSymbol<func::FuncOp>(launch.getCallee());
        auto handle = cast<GloinSpawnType>(launch.getHandle().getType().getValueType());
        auto found = adapters.find(worker);
        if (found == adapters.end()) {
            auto adapter = make_adapter(module, worker, handle.getValueType(), error, *layout);
            if (failed(adapter))
                return failure();
            found = adapters.try_emplace(worker, *adapter).first;
        }
        auto &adapter = found->second;
        for (auto *use : launch.getHandle().getUsers()) {
            if (auto transfer = dyn_cast<ResultValueOp>(use)) {
                for (auto *consumer : llvm::make_early_inc_range(transfer.getValue().getUsers())) {
                    auto join = cast<JoinOp>(consumer);
                    b.setInsertionPoint(join);
                    auto loc = join.getLoc();
                    auto out = slot(b, join, adapter.completion);
                    b.create<LLVM::CallOp>(loc, joinFn,
                                           ValueRange{as_layout(b, loc, join.getHandle(), ptr), out,
                                                      integer(b, loc, adapter.completion_size)});
                    auto loaded = b.create<LLVM::LoadOp>(loc, adapter.completion, out);
                    join.getResult().replaceAllUsesWith(
                        as_source(b, loc, loaded, join.getResult().getType()));
                    join.erase();
                }
            }
        }
        auto loc = launch.getLoc();
        b.setInsertionPoint(launch);
        Value captured = b.create<LLVM::ZeroOp>(loc, adapter.arguments);
        for (auto [i, value] : llvm::enumerate(launch.getOperands())) {
            auto field = as_layout(b, loc, value, adapter.arguments.getBody()[i]);
            captured = b.create<LLVM::InsertValueOp>(loc, captured, field,
                                                     ArrayRef<int64_t>{static_cast<int64_t>(i)});
        }
        auto input = slot(b, launch, adapter.arguments);
        b.create<LLVM::StoreOp>(loc, captured, input);
        auto out = slot(b, launch, receipt);
        auto address = b.create<LLVM::AddressOfOp>(loc, adapter.function);
        b.create<LLVM::CallOp>(loc, launchFn,
                               ValueRange{out, address, input,
                                          integer(b, loc, adapter.argument_size),
                                          integer(b, loc, adapter.completion_size)});
        auto loaded = b.create<LLVM::LoadOp>(loc, receipt, out);
        launch.getHandle().replaceAllUsesWith(
            as_source(b, loc, loaded, launch.getHandle().getType()));
        launch.erase();
    }
    return success();
}
