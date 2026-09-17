#include "codegen.h"
#include "compiler.h"
#include "lowering.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <gtest/gtest.h>

namespace {
std::string render(const Diagnostics &diagnostics) {
    std::ostringstream text;
    diagnostics.render(text);
    return text.str();
}
std::string print(mlir::ModuleOp module) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream);
    return text;
}
void expect_exportable(mlir::ModuleOp module) {
    ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
    module.walk([](mlir::Operation *op) {
        EXPECT_TRUE(mlir::isa<mlir::ModuleOp>(op) || op->getName().getDialectNamespace() == "llvm")
            << op->getName().getStringRef().str();
    });
    // Translation registration belongs to the consumer, not the lowering API.
    mlir::registerBuiltinDialectTranslation(*module.getContext());
    mlir::registerLLVMDialectTranslation(*module.getContext());
    llvm::LLVMContext context;
    auto exported = mlir::translateModuleToLLVMIR(module, context);
    ASSERT_NE(exported, nullptr);
    EXPECT_FALSE(llvm::verifyModule(*exported, &llvm::errs()));
}
void execute_lowered(mlir::ModuleOp module, int expected) {
    ASSERT_NO_FATAL_FAILURE(expect_exportable(module));
    auto value = gloin_test::run_external_mlir(print(module), {gloin_test::mlir_opt, {}},
                                               {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(value)) << llvm::toString(value.takeError());
    EXPECT_EQ(*value, expected);
}
mlir::OwningOpRef<mlir::ModuleOp> parse(mlir::MLIRContext &context, llvm::StringRef text) {
    // Match compiler dialect loading, without bypassing source checking in normal compilation.
    CodeGen load_dialects(context);
    return mlir::parseSourceString<mlir::ModuleOp>(text, &context);
}
} // namespace

TEST(LoweringTest, CompilerOffersVerifiedHighLevelAndLLVMOutput) {
    mlir::MLIRContext context;
    auto high = compile_source("def main() -> i32 { return 42; }", "answer.gloin", context);
    ASSERT_TRUE(high.success());
    EXPECT_TRUE(high.module->lookupSymbol<mlir::func::FuncOp>("main"));
    auto low = compile_source("def main() -> i32 { return 42; }", "answer.gloin", context,
                              CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(low.success()) << render(*low.diagnostics);
    EXPECT_TRUE(low.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main"));
    execute_lowered(*low.module, 42);
}

TEST(LoweringTest, NestedControlFlowCallsAndMutableStorageExecute) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def twice(x: i32) -> i32 { return x * 2; }
        def main() -> i32 {
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 3 && true; i = i + 1 {
                def mut j: i32 = 0;
                while j < 2 {
                    unless i < 0 { total = total + twice(3); }
                    j = j + 1;
                }
            }
            if total == 36 { return total + 6; } else { return -1; }
        }
    )",
                                 "nested.gloin", context, CompilationMode::Executable,
                                 CompilationOutput::LLVM);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    execute_lowered(*result.module, 42);
}

TEST(LoweringTest, EveryCoreScalarSignatureAndInternalWideArithmeticExports) {
    mlir::MLIRContext context;
    std::string source = "def noop() -> void {}";
    for (const std::string type :
         {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"})
        source += "def copy_" + type + "(x: " + type + ") -> " + type + " { def mut y: " + type +
                  " = x; y = y + " + (type.starts_with("f") ? "1.0" : "1") + "; return y; }";
    source += "def flag(x: bool) -> bool { return !x; }";
    auto result = compile_source(source, "types.gloin", context, CompilationMode::Module,
                                 CompilationOutput::LLVM);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    expect_exportable(*result.module);
    EXPECT_NE(print(*result.module).find("i128"), std::string::npos);
}

TEST(LoweringTest, StructuredControlFlowAndMemrefsUseTheSamePipeline) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @main() -> i32 {
                %slot = memref.alloca() : memref<i32>
                %zero = arith.constant 0 : index
                %one = arith.constant 1 : index
                %three = arith.constant 3 : index
                %base = arith.constant 0 : i32
                %step = arith.constant 14 : i32
                memref.store %base, %slot[] : memref<i32>
                scf.for %i = %zero to %three step %one {
                    %old = memref.load %slot[] : memref<i32>
                    %next = arith.addi %old, %step : i32
                    memref.store %next, %slot[] : memref<i32>
                }
                %result = memref.load %slot[] : memref<i32>
                return %result : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_FALSE(module);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, CloneLoweringPreservesHighLevelModuleAndLocations) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 { return 42; }", "owned.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    const auto original = print(*result.module);
    Diagnostics diagnostics;
    auto lowered =
        lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp>(result.module->clone()), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(original, print(*result.module));
    auto function = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main");
    auto loc = function.getLoc()->findInstanceOf<mlir::FileLineColLoc>();
    ASSERT_TRUE(loc);
    EXPECT_EQ(loc.getFilename().str(), "owned.gloin");
    EXPECT_EQ(loc.getLine(), 1u);
}

TEST(LoweringTest, InvalidIRStopsBeforeConversionAndKeepsSourceSpan) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 {\n  return 42;\n}", "invalid.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    auto function = result.module->lookupSymbol<mlir::func::FuncOp>("main");
    auto *ret = function.front().getTerminator();
    ret->setOperands(mlir::ValueRange{});
    auto source =
        std::make_shared<SourceFile>("invalid.gloin", "def main() -> i32 {\n  return 42;\n}");
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(result.module), diagnostics, source);
    EXPECT_FALSE(lowered);
    EXPECT_FALSE(result.module);
    ASSERT_FALSE(diagnostics.all().empty());
    const auto &error = diagnostics.all().front();
    EXPECT_EQ(error.stage, DiagnosticStage::Verification);
    ASSERT_EQ(error.span.source, source);
    EXPECT_EQ(source->line_column(error.span.begin), (std::pair<size_t, size_t>{2, 3}));
}

TEST(LoweringTest, RejectsGloinOperationsInsteadOfSilentlyDroppingThem) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @main() -> i32 {
                %c = "gloin.constant"() {value = 42 : i32} : () -> i32 loc("custom.gloin":4:7)
                return %c : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    EXPECT_FALSE(lowered);
    ASSERT_FALSE(diagnostics.all().empty());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    std::ostringstream message;
    diagnostics.render(message);
    EXPECT_NE(message.str().find("custom.gloin:4:7: error:"), std::string::npos);
    EXPECT_NE(message.str().find("gloin.constant"), std::string::npos);
}

TEST(LoweringTest, RejectsCustomTypesNestedInsideFunctionSignatures) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module { func.func private @pending(!gloin.spawn<i32>) }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    EXPECT_NE(diagnostics.all().front().message.find("type"), std::string::npos);
}

TEST(LoweringTest, RejectsUnresolvedConversionCastsAtExportBoundary) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @cast(%x: i32) -> f32 {
                %y = builtin.unrealized_conversion_cast %x : i32 to f32 loc("cast.gloin":3:9)
                return %y : f32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    std::ostringstream message;
    diagnostics.render(message);
    EXPECT_NE(message.str().find("cast.gloin:3:9"), std::string::npos);
    EXPECT_NE(message.str().find("unrealized_conversion_cast"), std::string::npos);
}

TEST(LoweringTest, RejectsNonLLVMTypesInMetadataAfterConversion) {
    mlir::MLIRContext context;
    auto module = parse(context, "module attributes {test.type = tensor<2xi32>} {}");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_NE(diagnostics.all().front().message.find("Type is not legal at LLVM export"),
              std::string::npos);
}

TEST(LoweringTest, NullModulesAndExistingErrorsCannotSucceed) {
    Diagnostics null_diagnostics;
    EXPECT_FALSE(lower_to_llvm({}, null_diagnostics));
    ASSERT_TRUE(null_diagnostics.has_errors());
    EXPECT_EQ(null_diagnostics.all().front().stage, DiagnosticStage::Verification);
    mlir::MLIRContext context;
    auto module = parse(context, "module {}");
    ASSERT_TRUE(module);
    Diagnostics prior;
    prior.error(DiagnosticStage::Semantic, {}, "previous error");
    EXPECT_FALSE(lower_to_llvm(std::move(module), prior));
    EXPECT_EQ(prior.all().size(), 1u);
}

TEST(LoweringTest, EarlierSourceErrorsKeepTheirOriginalStageInLLVMMode) {
    mlir::MLIRContext context;
    for (const auto &[source, stage] : std::vector<std::pair<std::string, DiagnosticStage>>{
             {"def main() -> i32 { return; }", DiagnosticStage::Semantic},
             {"def main() -> i32 { return 1 }", DiagnosticStage::Parsing},
             {"def main() -> i32 { return 0x; }", DiagnosticStage::Lexing}}) {
        auto result = compile_source(source, "error.gloin", context, CompilationMode::Executable,
                                     CompilationOutput::LLVM);
        EXPECT_FALSE(result.success());
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, stage);
    }
}

TEST(LoweringTest, RepeatedCompilationAndExecutionHaveStableResults) {
    std::string previous;
    for (unsigned i = 0; i < 3; ++i) {
        mlir::MLIRContext context;
        auto result = compile_source("def main() -> i32 { def mut i: i32 = 0; "
                                     "for ; i < 3; i = i + 1 {} return i - 4; }",
                                     "repeat.gloin", context, CompilationMode::Executable,
                                     CompilationOutput::LLVM);
        ASSERT_TRUE(result.success()) << render(*result.diagnostics);
        const auto ir = print(*result.module);
        if (i)
            EXPECT_EQ(ir, previous);
        previous = ir;
        execute_lowered(*result.module, -1);
    }
}

TEST(LoweringTest, RejectsUnknownOperationsAndNestedModules) {
    mlir::MLIRContext context;
    context.allowUnregisteredDialects();
    for (const std::string text :
         {"module { \"alien.operation\"() : () -> () }", "module { module @nested {} }"}) {
        auto module = parse(context, text);
        ASSERT_TRUE(module);
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
        ASSERT_TRUE(diagnostics.has_errors());
        EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
        EXPECT_NE(diagnostics.all().front().message.find("No supported lowering"),
                  std::string::npos);
    }
}

TEST(LoweringTest, ConversionErrorsDiscardPartialIRAndStayInDiagnostics) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module { func.func @bad(%x: memref<4xi32, "unsupported">) { return } }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    EXPECT_FALSE(lowered);
    EXPECT_FALSE(module);
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    EXPECT_NE(render(diagnostics).find("memory space"), std::string::npos);
}
