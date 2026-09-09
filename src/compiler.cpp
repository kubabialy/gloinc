#include "compiler.h"
#include "codegen.h"
#include "parser.h"
#include "sema.h"

CompilationResult compile_source(std::string text, std::string filename,
                                 mlir::MLIRContext &context) {
    auto diagnostics = std::make_shared<Diagnostics>();
    GloinParser parser(Lexer(std::move(text), std::move(filename), diagnostics));
    auto parsed = parser.parse_checked_program();
    if (!parsed.success)
        return {{}, diagnostics, diagnostics->all().front().stage};

    Sema sema(diagnostics);
    if (!sema.check_program(parsed.program))
        return {{}, diagnostics, DiagnosticStage::Semantic};

    CodeGen codegen(context, diagnostics);
    auto module = codegen.generate(parsed.program);
    if (!module)
        return {{}, diagnostics, DiagnosticStage::Codegen};
    return {mlir::OwningOpRef<mlir::ModuleOp>(module), diagnostics, std::nullopt};
}
