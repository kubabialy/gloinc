#include "lowering.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"

namespace {
void record_error(Diagnostics &diagnostics, DiagnosticStage stage, mlir::Location location,
                  std::string message, const std::shared_ptr<const SourceFile> &source) {
    SourceSpan span;
    std::optional<DiagnosticPosition> position;
    if (auto file = location->findInstanceOf<mlir::FileLineColLoc>()) {
        position = DiagnosticPosition{file.getFilename().str(), file.getLine(), file.getColumn()};
        if (source && source->name == position->filename && position->line > 0 &&
            position->line <= source->line_starts.size() && position->column > 0) {
            auto offset = source->line_starts[position->line - 1] + position->column - 1;
            if (offset <= source->text.size())
                span = {source, offset, offset};
        }
    }
    diagnostics.error(stage, std::move(span), std::move(message), std::move(position));
}

bool check_legality(mlir::ModuleOp module, bool final) {
    bool valid = true;
    module.walk([&](mlir::Operation *op) {
        const auto dialect = op->getName().getDialectNamespace();
        bool allowed =
            op == module.getOperation() || (op->getName().isRegistered() && dialect == "llvm");
        if (!final)
            allowed |= mlir::isa<mlir::UnrealizedConversionCastOp>(op) ||
                       (op->getName().isRegistered() &&
                        (dialect == "func" || dialect == "arith" || dialect == "cf" ||
                         dialect == "scf" || dialect == "memref"));
        if (!allowed) {
            op->emitError(final ? "Operation is not legal at LLVM export: "
                                : "No supported lowering for operation: ")
                << op->getName();
            valid = false;
        }
        auto check_type = [&](mlir::Type type) {
            const auto ns = type.getDialect().getNamespace();
            const bool legal =
                final ? mlir::LLVM::isCompatibleOuterType(type) : ns == "builtin" || ns == "llvm";
            if (!legal) {
                op->emitError(final ? "Type is not legal at LLVM export: "
                                    : "No supported lowering for type: ")
                    << type;
                valid = false;
            }
        };
        for (auto type : op->getOperandTypes())
            type.walk(check_type);
        for (auto type : op->getResultTypes())
            type.walk(check_type);
        for (auto &region : op->getRegions())
            for (auto &block : region)
                for (auto argument : block.getArguments())
                    argument.getType().walk(check_type);
        op->getAttrDictionary().walk<mlir::WalkOrder::PreOrder>(
            [&](mlir::Attribute attr) {
                // LLVM's memref/index conversions retain index-typed integer
                // payloads in llvm.mlir.constant with a concrete integer result.
                // The exporter supports these literals; index SSA types do not survive.
                if (final && mlir::isa<mlir::LLVM::ConstantOp>(op) &&
                    attr == op->getAttr("value")) {
                    if (auto integer = mlir::dyn_cast<mlir::IntegerAttr>(attr);
                        integer && integer.getType().isIndex())
                        return mlir::WalkResult::skip();
                }
                return mlir::WalkResult::advance();
            },
            check_type);
    });
    return valid;
}
} // namespace

bool verify_module(mlir::ModuleOp module, Diagnostics &diagnostics,
                   std::shared_ptr<const SourceFile> source) {
    if (diagnostics.has_errors())
        return false;
    if (!module) {
        diagnostics.error(DiagnosticStage::Verification, {}, "Cannot verify a null module");
        return false;
    }
    mlir::ScopedDiagnosticHandler handler(module.getContext(), [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            record_error(diagnostics, DiagnosticStage::Verification, diagnostic.getLocation(),
                         diagnostic.str(), source);
        return mlir::success();
    });
    const auto result = mlir::verify(module);
    if (mlir::failed(result) && !diagnostics.has_errors())
        record_error(diagnostics, DiagnosticStage::Verification, module.getLoc(),
                     "Module verification failed", source);
    return mlir::succeeded(result) && !diagnostics.has_errors();
}

mlir::OwningOpRef<mlir::ModuleOp> lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp> module,
                                                Diagnostics &diagnostics,
                                                std::shared_ptr<const SourceFile> source) {
    if (!verify_module(module.get(), diagnostics, source))
        return {};
    mlir::ScopedDiagnosticHandler handler(module->getContext(), [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            record_error(diagnostics, DiagnosticStage::Lowering, diagnostic.getLocation(),
                         diagnostic.str(), source);
        return mlir::success();
    });
    if (!check_legality(*module, false))
        return {};
    mlir::PassManager passes(module->getContext());
    passes.enableVerifier(true);
    passes.addPass(mlir::createSCFToControlFlowPass());
    passes.addPass(mlir::createConvertControlFlowToLLVMPass());
    passes.addPass(mlir::createArithToLLVMConversionPass());
    passes.addPass(mlir::createConvertFuncToLLVMPass());
    passes.addPass(mlir::createFinalizeMemRefToLLVMConversionPass());
    passes.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(passes.run(*module)) || !check_legality(*module, true) ||
        mlir::failed(mlir::verify(*module)) || diagnostics.has_errors()) {
        if (!diagnostics.has_errors())
            record_error(diagnostics, DiagnosticStage::Lowering, module->getLoc(),
                         "LLVM lowering failed", source);
        return {};
    }
    return module;
}
