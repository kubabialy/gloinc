// Thin public-API adapter for mixed-IR regression programs written in Gloin.
// This keeps fixture construction and all expectations in the Gloin driver.
#include "codegen.h"
#include "lowering.h"
#include "native_output.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const std::string mode = argv[1];
    if (mode != "emit" && mode != "O0" && mode != "O2") return 2;
    mlir::MLIRContext context;
    CodeGen dialects(context);
    auto input = mlir::parseSourceFile<mlir::ModuleOp>(argv[2], &context);
    if (!input) return 1;
    Diagnostics diagnostics;
    auto module = lower_to_llvm(std::move(input), diagnostics);
    if (!module) { diagnostics.render(std::cerr); return 1; }
    if (mode == "emit") {
        std::error_code error;
        llvm::raw_fd_ostream output(argv[3], error, llvm::sys::fs::OF_Text);
        if (error) { std::cerr << error.message() << '\n'; return 1; }
        module->print(output);
        output.flush();
        return output.has_error() ? 1 : 0;
    }
    if (!emit_native(*module, argv[3], NativeOutput::Executable,
                     mode == "O2" ? NativeOptimization::O2 : NativeOptimization::O0,
                     argv[0], diagnostics)) {
        diagnostics.render(std::cerr);
        return 1;
    }
    return 0;
}
