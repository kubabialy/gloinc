#include "jit_runner.h"
#include "arena_lowering.h"
#include "arena_runtime.h"
#include "lowering.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "standard_runtime.h"
#include "stdlib_lowering.h"
#include "stdlib_runtime.h"
#include "io_runtime.h"
#include "context_runtime.h"
#include "math_runtime.h"
#include "time_runtime_internal.h"
#include "random_runtime.h"
#include "context_runtime_internal.h"
#include "target_layout.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <cstdlib>

namespace {
void standard_output(const char *bytes, uint64_t length) {
    auto &output = llvm::outs();
    if (length)
        output.write(bytes, length);
    output.flush();
}

bool is_standard_output(mlir::LLVM::LLVMFuncOp function) {
    auto &context = *function.getContext();
    auto type = mlir::LLVM::LLVMFunctionType::get(
        mlir::LLVM::LLVMVoidType::get(&context),
        {mlir::LLVM::LLVMPointerType::get(&context), mlir::IntegerType::get(&context, 64)}, false);
    return function.getName() == standard_output_symbol && function.isExternal() &&
           function.getFunctionType() == type && function.getCConv() == mlir::LLVM::CConv::C &&
           function.getLinkage() == mlir::LLVM::Linkage::External;
}

bool is_defer_allocator(mlir::LLVM::LLVMFuncOp function) {
    auto &context = *function.getContext();
    const bool allocate = function.getName() == "malloc";
    mlir::Type pointer = mlir::LLVM::LLVMPointerType::get(&context);
    auto type = mlir::LLVM::LLVMFunctionType::get(
        allocate ? pointer : mlir::LLVM::LLVMVoidType::get(&context),
        {allocate ? mlir::IntegerType::get(&context, 64) : pointer}, false);
    return function.isExternal() && function.getFunctionType() == type &&
           function.getCConv() == mlir::LLVM::CConv::C &&
           function.getLinkage() == mlir::LLVM::Linkage::External;
}

void execution_error(Diagnostics &diagnostics, mlir::Location location, std::string message) {
    std::optional<DiagnosticPosition> position;
    if (auto file = location->findInstanceOf<mlir::FileLineColLoc>())
        position = DiagnosticPosition{file.getFilename().str(), file.getLine(), file.getColumn()};
    diagnostics.error(DiagnosticStage::Execution, {}, std::move(message), std::move(position));
}

mlir::LLVM::LLVMFuncOp validate_entry(mlir::ModuleOp module, Diagnostics &diagnostics) {
    auto main = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("main");
    if (!main) {
        execution_error(diagnostics, module.getLoc(),
                        "JIT requires a defined main() -> i32 function");
        return {};
    }
    auto type = main.getFunctionType();
    const auto linkage = main.getLinkage();
    if (main.isExternal() || !type.getParams().empty() || type.isVarArg() ||
        !type.getReturnType().isSignlessInteger(32) || main.getCConv() != mlir::LLVM::CConv::C ||
        (linkage != mlir::LLVM::Linkage::External && linkage != mlir::LLVM::Linkage::Internal &&
         linkage != mlir::LLVM::Linkage::Private)) {
        execution_error(diagnostics, main.getLoc(),
                        "JIT entry must define non-variadic main() -> i32 with C calling "
                        "convention and emitted linkage");
        return {};
    }
    // Only explicitly registered output, defer, and arena ABIs can be external.
    for (auto function : module.getOps<mlir::LLVM::LLVMFuncOp>()) {
        if (auto kind = standard_operation(function.getName().str(), standard_runtime_names)) {
            if (!function.isExternal() ||
                function.getFunctionType() != standard_runtime_type(*module.getContext(), *kind) ||
                function.getCConv() != mlir::LLVM::CConv::C ||
                function.getLinkage() != mlir::LLVM::Linkage::External)
                execution_error(diagnostics, function.getLoc(), "Invalid standard-library runtime ABI");
            continue;
        }
        if (auto kind = arena_operation(function.getName().str(), arena_runtime_names)) {
            if (!function.isExternal() ||
                function.getFunctionType() != arena_runtime_type(*module.getContext(), *kind) ||
                function.getCConv() != mlir::LLVM::CConv::C ||
                function.getLinkage() != mlir::LLVM::Linkage::External)
                execution_error(diagnostics, function.getLoc(), "Invalid arena runtime ABI");
            continue;
        }
        if (function.getName() == "malloc" || function.getName() == "free") {
            if (!is_defer_allocator(function))
                execution_error(diagnostics, function.getLoc(), "Invalid defer allocator runtime ABI");
            continue;
        }
        if (function.getName() == standard_output_symbol) {
            if (!is_standard_output(function))
                execution_error(diagnostics, function.getLoc(), "Invalid standard output runtime ABI");
            continue;
        }
        if (function.isExternal())
            execution_error(diagnostics, function.getLoc(),
                            "External function is not supported by the core JIT: " +
                                function.getName().str());
        else if (function.getCConv() != mlir::LLVM::CConv::C ||
                 (function.getLinkage() != mlir::LLVM::Linkage::External &&
                  function.getLinkage() != mlir::LLVM::Linkage::Internal &&
                  function.getLinkage() != mlir::LLVM::Linkage::Private))
            execution_error(
                diagnostics, function.getLoc(),
                "Core JIT functions require C calling convention and emitted linkage: " +
                    function.getName().str());
    }
    for (auto global : module.getOps<mlir::LLVM::GlobalOp>()) {
        if (!global.getValueOrNull() && global.getInitializerRegion().empty())
            execution_error(diagnostics, global.getLoc(),
                            "External global is not supported by the core JIT: " +
                                global.getSymName().str());
    }
    return diagnostics.has_errors() ? mlir::LLVM::LLVMFuncOp{} : main;
}

std::string add_entry_adapter(mlir::ModuleOp module, mlir::LLVM::LLVMFuncOp main) {
    // User symbols such as _mlir_main must not collide with MLIR's packed wrapper.
    // Choose both the adapter and its future wrapper name before creating either.
    std::string name = "gloin.jit.entry";
    unsigned suffix = 0;
    while (module.lookupSymbol(name) || module.lookupSymbol("_mlir_" + name))
        name = "gloin.jit.entry." + std::to_string(++suffix);
    mlir::OpBuilder builder(module.getContext());
    builder.setInsertionPointToEnd(module.getBody());
    auto adapter =
        builder.create<mlir::LLVM::LLVMFuncOp>(main.getLoc(), name, main.getFunctionType());
    builder.setInsertionPointToStart(adapter.addEntryBlock(builder));
    auto call = builder.create<mlir::LLVM::CallOp>(main.getLoc(), main, mlir::ValueRange{});
    builder.create<mlir::LLVM::ReturnOp>(main.getLoc(), call.getResults());
    return name;
}
} // namespace

ExecutionResult JitRunner::run(mlir::ModuleOp module, const std::vector<std::string> &arguments, const GloinClockSource *clock) {
    auto diagnostics = std::make_shared<Diagnostics>();
    auto failure = [&]() -> ExecutionResult {
        return {std::nullopt, diagnostics, diagnostics->all().back().stage};
    };
    auto lowered = lower_to_llvm(module ? mlir::OwningOpRef<mlir::ModuleOp>(module.clone())
                                        : mlir::OwningOpRef<mlir::ModuleOp>{},
                                 *diagnostics);
    if (!lowered)
        return failure();
    auto main = validate_entry(*lowered, *diagnostics);
    if (!main)
        return failure();
    const auto entry = add_entry_adapter(*lowered, main);
    if (!verify_module(*lowered, *diagnostics))
        return failure();

    auto target = native_target_layout();
    if (!target) {
        execution_error(*diagnostics, main.getLoc(), llvm::toString(target.takeError()));
        return failure();
    }
    auto &context = *lowered->getContext();
    mlir::registerBuiltinDialectTranslation(context);
    mlir::registerLLVMDialectTranslation(context);
    mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            execution_error(*diagnostics, diagnostic.getLocation(), diagnostic.str());
        return mlir::success();
    });
    mlir::ExecutionEngineOptions options;
    options.enableGDBNotificationListener = false;
    options.enablePerfNotificationListener = false;
    auto verify_llvm = [](llvm::Module *llvm_module) -> llvm::Error {
        std::string message;
        llvm::raw_string_ostream stream(message);
        if (llvm::verifyModule(*llvm_module, &stream))
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", message.c_str());
        return llvm::Error::success();
    };
    options.transformer = verify_llvm;
    auto engine = mlir::ExecutionEngine::create(*lowered, options);
    if (!engine) {
        execution_error(*diagnostics, main.getLoc(),
                        "Cannot create JIT: " + llvm::toString(engine.takeError()));
        return failure();
    }
    if (diagnostics->has_errors())
        return failure();
    {
        (*engine)->registerSymbols([](llvm::orc::MangleAndInterner mangle) {
            llvm::orc::SymbolMap symbols;
            symbols[mangle(standard_output_symbol)] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&standard_output), llvm::JITSymbolFlags::Exported);
            symbols[mangle("malloc")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&std::malloc), llvm::JITSymbolFlags::Exported);
            symbols[mangle("free")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&std::free), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_arena_general_create")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_arena_general_create),
                llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_arena_general_alloc")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_arena_general_alloc),
                llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_arena_general_reset")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_arena_general_reset),
                llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_arena_general_destroy")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_arena_general_destroy),
                llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_arena_zero_bytes")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_arena_zero_bytes),
                llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_i32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_i32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_i32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_i32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_input")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_std_input), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_strings_copy")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&gloin_strings_copy), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_i64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_i64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_u64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_u64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_f32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_f32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_parse_bool")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_parse_bool), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_i64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_i64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_u64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_u64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_f32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_f32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_f32_fixed")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_f32_fixed), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_format_f64_fixed")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_format_f64_fixed), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_i64_from_i32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_i64_from_i32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_i32_from_i64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_i32_from_i64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_u64_from_i64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_u64_from_i64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_i64_from_u64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_i64_from_u64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_f64_from_i64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_f64_from_i64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_f64_from_u64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_f64_from_u64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_i64_from_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_i64_from_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_u64_from_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_u64_from_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_f64_from_f32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_f64_from_f32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_std_f32_from_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_std_f32_from_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_standard")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_standard), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_open")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_open), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_read")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_read), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_write")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_write), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_flush")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_flush), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_close")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_close), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_io_error_message")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_io_error_message), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_fs_metadata")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_fs_metadata), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_fs_mkdir")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_fs_mkdir), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_fs_remove_file")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_fs_remove_file), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_fs_rename_replace")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_fs_rename_replace), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_process_arg_count")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_process_arg_count), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_process_arg")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_process_arg), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_process_env")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_process_env), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_process_cwd")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_process_cwd), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_math_unary_f32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_math_unary_f32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_math_unary_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_math_unary_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_math_binary_f32")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_math_binary_f32), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_math_binary_f64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_math_binary_f64), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_time_monotonic")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_time_monotonic), llvm::JITSymbolFlags::Exported);
            symbols[mangle("gloin_random_splitmix64")] = llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&gloin_random_splitmix64), llvm::JITSymbolFlags::Exported);
            return symbols;
        });
    }
    gloin::process::ArgumentsScope scope(arguments);
    if (!scope.valid()) {
        execution_error(*diagnostics, main.getLoc(), "Cannot bind program arguments: embedded NUL or allocation failure");
        return failure();
    }
    gloin::time::ClockScope clock_scope(clock);
    if (!clock_scope.valid()) {
        execution_error(*diagnostics, main.getLoc(), "Cannot bind clock source: invalid callback or allocation failure");
        return failure();
    }
    int32_t value = 0;
    void *entry_arguments[] = {&value};
    if (auto error = (*engine)->invokePacked(entry, entry_arguments)) {
        execution_error(*diagnostics, main.getLoc(),
                        "Cannot invoke JIT entry: " + llvm::toString(std::move(error)));
        return failure();
    }
    if (diagnostics->has_errors())
        return failure();
    if (llvm::outs().has_error()) {
        const auto error = llvm::outs().error();
        llvm::outs().clear_error();
        execution_error(*diagnostics, main.getLoc(), "Cannot write stdout: " + error.message());
        return failure();
    }
    return {value, diagnostics, std::nullopt};
}
