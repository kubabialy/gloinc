#include "compiler.h"
#include "jit_runner.h"
#include "native_output.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {
enum class Mode { Run, Check, EmitIR, EmitLLVM, EmitObject, EmitExecutable };
constexpr std::string_view usage =
    "Usage: gloinc [--jit | --run | --check | --emit-ir | --emit-llvm | --emit-object | "
    "--emit-exe] "
    "[-O0 | -O2] [-o PATH] [--stdlib-dir DIR] [--] FILE [-- ARG...]\n"
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
                   "  --jit        Compile and run main() -> i32 in the JIT.\n"
                   "  --run        Alias for --jit.\n"
                   "  --check      Compile and verify without execution; main is optional.\n"
                   "  --emit-ir    Print verified high-level MLIR without execution.\n"
                   "  --emit-llvm  Print verified LLVM-dialect MLIR without execution.\n"
                   "  --emit-object  Write a native macOS arm64 object file.\n"
                   "  --emit-exe     Link a standalone macOS arm64 executable (default).\n"
                   "  -O0, -O2      Native optimization level (default: -O0).\n"
                   "  -o PATH        Set native output path (default: a.out for executables).\n"
                   "  --stdlib-dir DIR  Load standard module files from DIR.\n"
                   "  --           Before FILE: end compiler options; after FILE: forward program "
                   "arguments.\n"
                   "  -h, --help   Show this help.\n"
                   "  -V, --version  Show compiler and LLVM/MLIR versions.\n\n"
                   "Run returns main's low eight bits as the process exit status.\n"
                   "Only explicit output calls write to stdout during execution.\n"
                   "Exit status: main's result for --jit/--run; 0 for other successful modes;\n"
                   "1 compiler/I/O/JIT error, 2 usage error (with stderr diagnostics).\n"
                   "Runtime arithmetic traps terminate the process with a signal.\n";
            return finish_output();
        }
        if (option == "--version" || option == "-V") {
            llvm::outs() << "gloinc " << GLOIN_VERSION << " (LLVM/MLIR " << GLOIN_LLVM_VERSION
                         << ")\n";
            return finish_output();
        }
    }
    Mode mode = Mode::EmitExecutable;
    bool has_mode = false;
    bool options = true;
    bool forwarded = false;
    std::vector<std::string> program_arguments;
    std::optional<std::string> filename;
    std::optional<std::string> library_directory;
    std::optional<std::string> output_path;
    NativeOptimization optimization = NativeOptimization::O0;
    bool has_optimization = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (filename && argument == "--") {
            forwarded = true;
            for (++i; i < argc; ++i)
                program_arguments.emplace_back(argv[i]);
            break;
        }
        if (options && argument == "--") {
            options = false;
            continue;
        }
        if (options && argument.starts_with('-')) {
            if (argument == "--stdlib-dir") {
                if (library_directory || i + 1 == argc || std::string_view(argv[i + 1]).empty())
                    return usage_error(
                        "--stdlib-dir requires one directory and may appear only once");
                library_directory = argv[++i];
                continue;
            }
            if (argument == "-o") {
                if (output_path || i + 1 == argc || std::string_view(argv[i + 1]).empty())
                    return usage_error("-o requires one path and may appear only once");
                output_path = argv[++i];
                continue;
            }
            if (argument == "-O0" || argument == "-O2") {
                if (has_optimization)
                    return usage_error("choose exactly one optimization level");
                optimization =
                    argument == "-O2" ? NativeOptimization::O2 : NativeOptimization::O0;
                has_optimization = true;
                continue;
            }
            Mode selected;
            if (argument == "--jit" || argument == "--run")
                selected = Mode::Run;
            else if (argument == "--check")
                selected = Mode::Check;
            else if (argument == "--emit-ir")
                selected = Mode::EmitIR;
            else if (argument == "--emit-llvm")
                selected = Mode::EmitLLVM;
            else if (argument == "--emit-object")
                selected = Mode::EmitObject;
            else if (argument == "--emit-exe")
                selected = Mode::EmitExecutable;
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
    const bool native = mode == Mode::EmitObject || mode == Mode::EmitExecutable;
    if (has_optimization && !native)
        return usage_error("-O0 and -O2 require native output mode");
    if (mode == Mode::EmitObject && !output_path)
        return usage_error("-o PATH is required for object output");
    if (!native && output_path)
        return usage_error("-o PATH is only valid for native output modes");
    if (mode == Mode::EmitExecutable && !output_path)
        output_path = "a.out";
    if (native &&
        (*output_path == *filename || (llvm::sys::fs::exists(*output_path) &&
                                       llvm::sys::fs::equivalent(*filename, *output_path))))
        return usage_error("output path must differ from input file");

    if (forwarded && mode != Mode::Run)
        return usage_error("program arguments require run mode");
    program_arguments.insert(program_arguments.begin(), *filename);

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
    if (!library_directory) {
        auto executable =
            llvm::sys::fs::getMainExecutable(argv[0], reinterpret_cast<void *>(&main));
        llvm::SmallString<256> directory(llvm::sys::path::parent_path(executable));
        llvm::sys::path::append(directory, "stdlib");
        if (!llvm::sys::fs::is_directory(directory)) {
            directory = llvm::sys::path::parent_path(executable);
            llvm::sys::path::append(directory, "..", "share", "gloinc", "stdlib");
        }
        library_directory = directory.str().str();
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(
        (*buffer)->getBuffer().str(), *filename, context,
        mode == Mode::Run || native ? CompilationMode::Executable : CompilationMode::Module,
        mode == Mode::EmitLLVM || native ? CompilationOutput::LLVM : CompilationOutput::HighLevel,
        *library_directory);
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
    if (native) {
        auto executable =
            llvm::sys::fs::getMainExecutable(argv[0], reinterpret_cast<void *>(&main));
        if (!emit_native(*compiled.module, *output_path,
                         mode == Mode::EmitObject ? NativeOutput::Object : NativeOutput::Executable,
                         optimization, executable, *compiled.diagnostics)) {
            compiled.diagnostics->render(std::cerr);
            return 1;
        }
        return 0;
    }
    auto executed = JitRunner::run(*compiled.module, program_arguments);
    if (!executed.success()) {
        executed.diagnostics->render(std::cerr);
        return 1;
    }
    if (const auto error = finish_output())
        return error;
    return static_cast<uint32_t>(*executed.value) & 0xff;
}
