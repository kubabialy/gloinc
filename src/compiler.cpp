#include "compiler.h"
#include "codegen.h"
#include "parser.h"
#include "sema.h"

CompilationResult compile_source(std::string text, std::string filename, mlir::MLIRContext &context,
                                 CompilationMode mode) {
    auto diagnostics = std::make_shared<Diagnostics>();
    GloinParser parser(Lexer(std::move(text), std::move(filename), diagnostics));
    auto parsed = parser.parse_checked_program();
    if (!parsed.success)
        return {{}, diagnostics, diagnostics->all().front().stage};

    Sema sema(diagnostics);
    auto checked = sema.check_for_codegen(std::move(parsed.program), {}, mode);
    if (!checked)
        return {{}, diagnostics, DiagnosticStage::Semantic};

    CodeGen codegen(context, diagnostics);
    auto module = codegen.generate(*checked);
    if (!module)
        return {{}, diagnostics, DiagnosticStage::Codegen};
    return {std::move(module), diagnostics, std::nullopt};
}
