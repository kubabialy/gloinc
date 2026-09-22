#include "compiler.h"
#include "codegen.h"
#include "lowering.h"
#include "module_loader.h"
#include "parser.h"
#include "sema.h"

CompilationResult compile_source(std::string text, std::string filename, mlir::MLIRContext &context,
                                 CompilationMode mode, CompilationOutput output,
                                 std::string standard_library_directory) {
    auto diagnostics = std::make_shared<Diagnostics>();
    Lexer lexer(std::move(text), std::move(filename), diagnostics);
    auto source = lexer.source_file();
    GloinParser parser(std::move(lexer));
    auto parsed = parser.parse_checked_program();
    if (!parsed.success)
        return {{}, diagnostics, diagnostics->all().front().stage};
    if (standard_library_directory.empty())
        standard_library_directory = GLOIN_BUILD_STDLIB_DIR;
    if (!load_standard_modules(parsed.program, standard_library_directory, diagnostics))
        return {{}, diagnostics, diagnostics->all().back().stage};

    Sema sema(diagnostics);
    auto checked = sema.check_for_codegen(std::move(parsed.program), {}, mode);
    if (!checked)
        return {{}, diagnostics, DiagnosticStage::Semantic};

    CodeGen codegen(context, diagnostics);
    auto module = codegen.generate(*checked);
    if (!module)
        return {{}, diagnostics, DiagnosticStage::Codegen};
    if (output == CompilationOutput::LLVM)
        module = lower_to_llvm(std::move(module), *diagnostics, source);
    else if (!verify_module(*module, *diagnostics, source))
        module = {};
    if (!module)
        return {{}, diagnostics, diagnostics->all().back().stage};
    return {std::move(module), diagnostics, std::nullopt};
}
