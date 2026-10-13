#include "compiler.h"
#include "compilation_timings.h"
#include "codegen.h"
#include "lowering.h"
#include "module_loader.h"
#include "parser.h"
#include "sema.h"

CompilationResult compile_source(std::string text, std::string filename, mlir::MLIRContext &context,
                                 CompilationMode mode, CompilationOutput output,
                                 std::string standard_library_directory, CompilationTimings *timings) {
    CompilationTimings::Stage parsing(timings, "parsing");
    auto diagnostics = std::make_shared<Diagnostics>();
    Lexer lexer(std::move(text), std::move(filename), diagnostics);
    auto source = lexer.source_file();
    GloinParser parser(std::move(lexer));
    auto parsed = parser.parse_checked_program();
    if (!parsed.success)
        return {{}, diagnostics, diagnostics->all().front().stage};
    parsing.stop();
    CompilationTimings::Stage modules(timings, "module_loading");
    if (standard_library_directory.empty())
        standard_library_directory = GLOIN_BUILD_STDLIB_DIR;
    if (!load_modules(parsed.program, source->name, standard_library_directory, diagnostics))
        return {{}, diagnostics, diagnostics->all().back().stage};
    modules.stop();

    CompilationTimings::Stage semantics(timings, "semantic_analysis");
    Sema sema(diagnostics);
    auto checked = sema.check_for_codegen(std::move(parsed.program), {}, mode);
    if (!checked)
        return {{}, diagnostics, DiagnosticStage::Semantic};
    semantics.stop();

    CompilationTimings::Stage generation(timings, "gir_generation");
    CodeGen codegen(context, diagnostics);
    auto module = codegen.generate(*checked);
    if (!module)
        return {{}, diagnostics, DiagnosticStage::Codegen};
    generation.stop();
    if (timings)
        timings->snapshot("gir", *module);
    if (output == CompilationOutput::LLVM)
        module = lower_to_llvm(std::move(module), *diagnostics, source, timings, mode);
    else {
        CompilationTimings::Stage verification(timings, "ir_verification");
        if (!verify_module(*module, *diagnostics, source))
            module = {};
    }
    if (!module)
        return {{}, diagnostics, diagnostics->all().back().stage};
    return {std::move(module), diagnostics, std::nullopt};
}
