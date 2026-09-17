#include "compiler.h"
#include "jit_runner.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {
enum class Mode { Run, Check, EmitIR, EmitLLVM };
constexpr std::string_view usage =
    "Usage: gloinc [--run | --check | --emit-ir | --emit-llvm] [--] FILE\n"
    "       gloinc --help\n"
    "       gloinc --version\n";

int usage_error(std::string_view message) {
    std::cerr << "gloinc: error: " << message << '\n' << usage;
    return 2;
}

int finish_output() {
    auto &output = llvm::outs();
    output.flush();
    if (!output.has_error())
        return 0;
    const auto error = output.error();
    output.clear_error();
    std::cerr << "gloinc: error: cannot write stdout: " << error.message() << '\n';
    return 1;
}
} // namespace

int main(int argc, char *argv[]) {
    if (argc == 2) {
        const std::string_view option = argv[1];
        if (option == "--help" || option == "-h") {
            llvm::outs()
                << usage
                << "\n"
                   "  --run        Compile and run main() -> i32 (default).\n"
                   "  --check      Compile and verify without execution; main is optional.\n"
                   "  --emit-ir    Print verified high-level MLIR without execution.\n"
                   "  --emit-llvm  Print verified LLVM-dialect MLIR without execution.\n"
                   "  --           Treat remaining arguments as filenames.\n"
                   "  -h, --help   Show this help.\n"
                   "  -V, --version  Show compiler and LLVM/MLIR versions.\n\n"
                   "Run prints the signed i32 result followed by a newline.\n"
                   "Exit status: 0 success (any result), 1 compiler/I/O/JIT error, 2 usage error.\n"
                   "Runtime arithmetic traps terminate the process with a signal.\n";
            return finish_output();
        }
        if (option == "--version" || option == "-V") {
            llvm::outs() << "gloinc " << GLOIN_VERSION << " (LLVM/MLIR " << GLOIN_LLVM_VERSION
                         << ")\n";
            return finish_output();
        }
    }
    Mode mode = Mode::Run;
    bool has_mode = false;
    bool options = true;
    std::optional<std::string> filename;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (options && argument == "--") {
            options = false;
            continue;
        }
        if (options && argument.starts_with('-')) {
            Mode selected;
            if (argument == "--run")
                selected = Mode::Run;
            else if (argument == "--check")
                selected = Mode::Check;
            else if (argument == "--emit-ir")
                selected = Mode::EmitIR;
            else if (argument == "--emit-llvm")
                selected = Mode::EmitLLVM;
            else
                return usage_error("unknown or misplaced option '" + std::string(argument) + "'");
            if (has_mode)
                return usage_error("choose exactly one mode");
            mode = selected;
            has_mode = true;
        } else {
            if (filename)
                return usage_error("expected exactly one input file");
            filename = argument;
        }
    }
    if (!filename || filename->empty())
        return usage_error("expected one input file");

    llvm::sys::fs::file_status status;
    if (const auto error = llvm::sys::fs::status(*filename, status)) {
        std::cerr << "gloinc: error: cannot read '" << *filename << "': " << error.message()
                  << '\n';
        return 1;
    }
    if (!llvm::sys::fs::is_regular_file(status)) {
        std::cerr << "gloinc: error: input is not a regular file: '" << *filename << "'\n";
        return 1;
    }
    auto buffer = llvm::MemoryBuffer::getFile(*filename);
    if (!buffer) {
        std::cerr << "gloinc: error: cannot read '" << *filename
                  << "': " << buffer.getError().message() << '\n';
        return 1;
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(
        (*buffer)->getBuffer().str(), *filename, context,
        mode == Mode::Run ? CompilationMode::Executable : CompilationMode::Module,
        mode == Mode::EmitLLVM ? CompilationOutput::LLVM : CompilationOutput::HighLevel);
    if (!compiled.success()) {
        compiled.diagnostics->render(std::cerr);
        return 1;
    }
    if (mode == Mode::Check)
        return 0;
    if (mode == Mode::EmitIR || mode == Mode::EmitLLVM) {
        compiled.module->print(llvm::outs(), mlir::OpPrintingFlags().enableDebugInfo());
        llvm::outs() << '\n';
        return finish_output();
    }
    auto executed = JitRunner::run(*compiled.module);
    if (!executed.success()) {
        executed.diagnostics->render(std::cerr);
        return 1;
    }
    llvm::outs() << *executed.value << '\n';
    return finish_output();
}
