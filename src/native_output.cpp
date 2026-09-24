#include "native_output.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include <unistd.h>

namespace {
void error(Diagnostics &diagnostics, const std::string &message) {
    diagnostics.error(DiagnosticStage::Execution, {}, message);
}

bool add_native_entry(llvm::Module &module, Diagnostics &diagnostics) {
    if (module.getFunction("gloin_process_arguments_push") ||
        module.getFunction("gloin_process_arguments_pop")) {
        error(diagnostics, "Source symbol conflicts with native process runtime");
        return false;
    }
    auto *program = module.getFunction("main");
    auto &context = module.getContext();
    auto *i32 = llvm::Type::getInt32Ty(context);
    if (!program || program->isDeclaration() || program->arg_size() != 0 ||
        program->getReturnType() != i32 || program->isVarArg()) {
        error(diagnostics, "Native output requires a defined main() -> i32");
        return false;
    }
    // Rename the checked source entry, preserving internal calls to it.
    program->setName("gloin.native.program.main");
    program->setLinkage(llvm::GlobalValue::InternalLinkage);
    auto *pointer = llvm::PointerType::getUnqual(context);
    auto *i64 = llvm::Type::getInt64Ty(context);
    auto *entry_type = llvm::FunctionType::get(i32, {i32, pointer}, false);
    auto *entry =
        llvm::Function::Create(entry_type, llvm::GlobalValue::ExternalLinkage, "main", module);
    auto *block = llvm::BasicBlock::Create(context, "entry", entry);
    llvm::IRBuilder<> builder(block);
    auto argc = entry->getArg(0);
    auto argv = entry->getArg(1);
    auto push = module.getOrInsertFunction("gloin_process_arguments_push",
                                           llvm::FunctionType::get(pointer, {i64, pointer}, false));
    auto pop = module.getOrInsertFunction("gloin_process_arguments_pop",
                                          llvm::FunctionType::get(i32, {pointer}, false));
    auto *scope = builder.CreateCall(push, {builder.CreateZExt(argc, i64), argv});
    auto *ready = llvm::BasicBlock::Create(context, "ready", entry);
    auto *failed = llvm::BasicBlock::Create(context, "failed", entry);
    builder.CreateCondBr(builder.CreateIsNotNull(scope), ready, failed);
    builder.SetInsertPoint(failed);
    builder.CreateRet(llvm::ConstantInt::get(i32, 1));
    builder.SetInsertPoint(ready);
    auto *result = builder.CreateCall(program);
    builder.CreateCall(pop, {scope});
    builder.CreateRet(result);
    return true;
}

bool temporary(const std::string &output, llvm::SmallString<256> &path, Diagnostics &diagnostics) {
    int fd = -1;
    if (auto ec = llvm::sys::fs::createUniqueFile(output + ".tmp-%%%%%%", fd, path)) {
        error(diagnostics, "Cannot create output beside '" + output + "': " + ec.message());
        return false;
    }
    ::close(fd);
    return true;
}
} // namespace

bool emit_native(mlir::ModuleOp module, const std::string &output, NativeOutput kind,
                 const std::string &compiler_path, Diagnostics &diagnostics) {
#if !defined(__APPLE__) || !defined(__aarch64__)
    error(diagnostics, "Native output in 0.0.1 supports Apple Silicon macOS only");
    return false;
#else
    if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter()) {
        error(diagnostics, "Cannot initialize native target");
        return false;
    }
    auto builder = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!builder) {
        error(diagnostics, llvm::toString(builder.takeError()));
        return false;
    }
    auto machine = builder->createTargetMachine();
    if (!machine) {
        error(diagnostics, llvm::toString(machine.takeError()));
        return false;
    }
    auto &mlir_context = *module.getContext();
    mlir::registerBuiltinDialectTranslation(mlir_context);
    mlir::registerLLVMDialectTranslation(mlir_context);
    llvm::LLVMContext llvm_context;
    auto translated = mlir::translateModuleToLLVMIR(module, llvm_context);
    if (!translated) {
        error(diagnostics, "Cannot translate verified LLVM-dialect module");
        return false;
    }
    translated->setTargetTriple((*machine)->getTargetTriple());
    translated->setDataLayout((*machine)->createDataLayout());
    if (!add_native_entry(*translated, diagnostics))
        return false;
    std::string verification;
    llvm::raw_string_ostream report(verification);
    if (llvm::verifyModule(*translated, &report)) {
        error(diagnostics, "Invalid native LLVM IR: " + verification);
        return false;
    }
    llvm::SmallString<256> object_path;
    const std::string object_output = kind == NativeOutput::Object ? output : output + ".o";
    if (!temporary(object_output, object_path, diagnostics))
        return false;
    llvm::FileRemover object_cleanup(object_path);
    std::error_code ec;
    llvm::raw_fd_ostream object(object_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error(diagnostics, "Cannot write native object: " + ec.message());
        return false;
    }
    llvm::legacy::PassManager passes;
    if ((*machine)->addPassesToEmitFile(passes, object, nullptr,
                                        llvm::CodeGenFileType::ObjectFile)) {
        error(diagnostics, "Native target cannot emit object files");
        return false;
    }
    passes.run(*translated);
    object.flush();
    if (object.has_error()) {
        error(diagnostics, "Cannot write native object");
        return false;
    }
    object.close();
    if (kind == NativeOutput::Object) {
        if (auto rename_error = llvm::sys::fs::rename(object_path, output)) {
            error(diagnostics, "Cannot place native object: " + rename_error.message());
            return false;
        }
        return true;
    }
    llvm::SmallString<256> executable_path;
    if (!temporary(output, executable_path, diagnostics))
        return false;
    llvm::FileRemover executable_cleanup(executable_path);
    llvm::SmallString<256> runtime(compiler_path);
    llvm::sys::path::remove_filename(runtime);
    llvm::sys::path::append(runtime, "libgloin_runtime.a");
    if (!llvm::sys::fs::exists(runtime)) {
        llvm::sys::path::remove_filename(runtime);
        llvm::sys::path::append(runtime, "..", "lib", "libgloin_runtime.a");
    }
    if (!llvm::sys::fs::exists(runtime)) {
        error(diagnostics, "Cannot find libgloin_runtime.a beside the compiler installation");
        return false;
    }
    const std::string linker = GLOIN_NATIVE_LINKER;
    const std::string object_arg = object_path.str().str();
    const std::string runtime_arg = runtime.str().str();
    const std::string output_arg = executable_path.str().str();
    llvm::SmallVector<llvm::StringRef> args{linker, object_arg, runtime_arg};
#if GLOIN_NATIVE_SANITIZED
    args.push_back("-fsanitize=address,undefined");
#endif
    args.push_back("-o");
    args.push_back(output_arg);
    std::string message;
    bool launch_failed = false;
    int status =
        llvm::sys::ExecuteAndWait(linker, args, std::nullopt, {}, 0, 0, &message, &launch_failed);
    if (launch_failed || status != 0) {
        error(diagnostics,
              "Native link failed" + (message.empty() ? std::string{} : ": " + message));
        return false;
    }
    if (auto rename_error = llvm::sys::fs::rename(executable_path, output)) {
        error(diagnostics, "Cannot place executable: " + rename_error.message());
        return false;
    }
    return true;
#endif
}
