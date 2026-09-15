#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "sema.h"
#include <gtest/gtest.h>
#include <type_traits>

using RawProgram = std::vector<std::unique_ptr<Statement>>;
static_assert(!std::is_default_constructible_v<CheckedProgram>);
static_assert(!std::is_copy_constructible_v<CheckedProgram>);
static_assert(!std::is_constructible_v<CheckedProgram, RawProgram, SemanticData>);
static_assert(!std::is_invocable_v<decltype(&CodeGen::generate), CodeGen &, const RawProgram &>);
static_assert(std::is_invocable_v<decltype(&CodeGen::generate), CodeGen &, const CheckedProgram &>);

namespace {
std::unique_ptr<CheckedProgram> check(const std::string &source) {
    GloinParser parser(Lexer(source, "checked.gloin"));
    auto parsed = parser.parse_checked_program();
    if (!parsed.success) {
        ADD_FAILURE() << "Fixture did not parse";
        return nullptr;
    }
    Sema sema(parser.diagnostics());
    auto result = sema.check_for_codegen(std::move(parsed.program));
    if (!result) {
        std::ostringstream errors;
        sema.diagnostics()->render(errors);
        ADD_FAILURE() << errors.str();
    }
    return result;
}
} // namespace

TEST(CheckedProgramTest, EveryCoreScalarHasOneSignatureAndStorageType) {
    for (const auto &info : core_types) {
        if (info.id == CoreType::Void)
            continue;
        std::string name(info.name);
        auto program = check("def identity(value: " + name + ") -> " + name +
                             " { def mut copy: " + name + " = value; return copy; }");
        ASSERT_NE(program, nullptr) << name;
        ASSERT_EQ(program->symbols().size(), 3u);
        for (const auto &symbol : program->symbols())
            EXPECT_EQ(symbol.type, info.id);
        EXPECT_EQ(program->symbols()[0].parameters, std::vector<CoreType>{info.id});
        EXPECT_GT(program->typed_node_count(), 4u);
        mlir::MLIRContext context;
        CodeGen codegen(context);
        auto module = codegen.generate(*program);
        ASSERT_TRUE(module) << name;
        EXPECT_TRUE(mlir::succeeded(mlir::verify(*module))) << name;
        auto function = module->lookupSymbol<mlir::func::FuncOp>("identity");
        ASSERT_TRUE(function);
        auto type = function.getFunctionType().getInput(0);
        if (info.id == CoreType::F32)
            EXPECT_TRUE(type.isF32());
        else if (info.id == CoreType::F64)
            EXPECT_TRUE(type.isF64());
        else
            EXPECT_TRUE(type.isInteger(info.bits));
        EXPECT_EQ(type, function.getFunctionType().getResult(0));
        int loads = 0;
        function.walk([&](mlir::LLVM::LoadOp load) {
            ++loads;
            EXPECT_EQ(load.getType(), type);
        });
        EXPECT_EQ(loads, 1);
    }
}

TEST(CheckedProgramTest, AliasesResolveBeforeCodegen) {
    auto program = check("def signed_value(x: int) -> i32 { def value: i32 = x; return value; } "
                         "def unsigned_value(x: usize) -> u64 { return x; }");
    ASSERT_NE(program, nullptr);
    EXPECT_EQ(program->symbols()[0].type, CoreType::I32);
    EXPECT_EQ(program->symbols()[0].parameters, std::vector<CoreType>{CoreType::I32});
    EXPECT_EQ(program->symbols()[1].type, CoreType::U64);
    EXPECT_EQ(program->symbols()[1].parameters, std::vector<CoreType>{CoreType::U64});
    EXPECT_EQ(program->target().pointer_bits, 64u);
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(*program);
    ASSERT_TRUE(module);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
}

TEST(CheckedProgramTest, SignednessAndBooleanRemainDistinct) {
    EXPECT_NE(resolve_core_type("i32"), resolve_core_type("u32"));
    EXPECT_NE(resolve_core_type("bool"), resolve_core_type("i8"));
    EXPECT_TRUE(core_type_info(CoreType::I32).is_signed);
    EXPECT_FALSE(core_type_info(CoreType::U32).is_signed);
    EXPECT_FALSE(core_type_info(CoreType::Bool).is_integer);
    mlir::MLIRContext context;
    for (const std::string source : {"def f(x: i32) -> void { def y: u32 = x; }",
                                     "def f(x: bool) -> void { def y: i8 = x; }"}) {
        auto result = compile_source(source, "types.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
        EXPECT_FALSE(result.module);
    }
}

TEST(CheckedProgramTest, UnknownAndDeferredTypesFailInEveryAnnotationPosition) {
    mlir::MLIRContext context;
    for (const std::string type :
         {"Mystery", "String", "string", "i128", "u128", "f128", "char", "u4", "be_u16"}) {
        for (const std::string source :
             {"def f(x: " + type + ") -> void {}", "def f() -> " + type + " {}",
              "def f() -> void { def mut x: " + type + "; }"}) {
            auto result = compile_source(source, "unknown.gloin", context);
            EXPECT_FALSE(result.success()) << source;
            EXPECT_FALSE(result.module);
            EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
            ASSERT_FALSE(result.diagnostics->all().empty());
            auto diagnostic = result.diagnostics->all().front();
            EXPECT_NE(diagnostic.message.find(type), std::string::npos);
            EXPECT_EQ(diagnostic.span.source->text.substr(
                          diagnostic.span.begin, diagnostic.span.end - diagnostic.span.begin),
                      type);
        }
    }
}

TEST(CheckedProgramTest, VoidOnlyAppearsInReturnSignatures) {
    mlir::MLIRContext context;
    for (const std::string source :
         {"def f(x: void) -> void {}", "def f() -> void { def mut x: void; }"}) {
        auto result = compile_source(source, "void.gloin", context);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
    }
    auto program = check("def work() -> void {} def main() -> i32 { work(); return 42; }");
    ASSERT_NE(program, nullptr);
    CodeGen codegen(context);
    auto module = codegen.generate(*program);
    ASSERT_TRUE(module);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    EXPECT_EQ(module->lookupSymbol<mlir::func::FuncOp>("work").getFunctionType().getNumResults(),
              0u);
}

TEST(CheckedProgramTest, ShadowedDeclarationsHaveDifferentIdentities) {
    auto program = check("def f(x: i32) -> i32 { { def x: int = 7; x; } return x; }");
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->symbols().size(), 3u);
    EXPECT_EQ(program->symbols()[1].name, "x");
    EXPECT_EQ(program->symbols()[2].name, "x");
    EXPECT_NE(program->symbols()[1].id, program->symbols()[2].id);
    EXPECT_EQ(program->symbols()[1].kind, SymbolKind::Parameter);
    EXPECT_EQ(program->symbols()[2].kind, SymbolKind::Variable);
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(*program);
    ASSERT_TRUE(module);
    auto function = module->lookupSymbol<mlir::func::FuncOp>("f");
    auto ret = llvm::dyn_cast<mlir::func::ReturnOp>(function.getBody().front().back());
    ASSERT_TRUE(ret);
    EXPECT_EQ(ret.getOperand(0), function.getArgument(0));
}

TEST(CheckedProgramTest, CallsUseResolvedFunctionIdentity) {
    auto program = check("def f(x: int) -> i32 { return x; } "
                         "def main() -> i32 { def result: int = f(42); return result; }");
    ASSERT_NE(program, nullptr);
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(*program);
    ASSERT_TRUE(module);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    int calls = 0;
    module->walk([&](mlir::func::CallOp call) {
        ++calls;
        EXPECT_EQ(call.getCallee(), "f");
        EXPECT_TRUE(call.getResult(0).getType().isInteger(32));
    });
    EXPECT_EQ(calls, 1);
}

TEST(CheckedProgramTest, CheckedProgramOutlivesFrontendAndModuleOutlivesCodegen) {
    auto program = check("def main() -> int { return 42; }");
    ASSERT_NE(program, nullptr);
    EXPECT_EQ(program->symbols()[0].span.source->name, "checked.gloin");
    mlir::MLIRContext context;
    mlir::OwningOpRef<mlir::ModuleOp> module;
    {
        CodeGen codegen(context);
        module = codegen.generate(*program);
        ASSERT_TRUE(module);
    }
    program.reset();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    EXPECT_TRUE(module->lookupSymbol<mlir::func::FuncOp>("main"));
}

TEST(CheckedProgramTest, FailedCheckingCannotProduceCheckedProgram) {
    for (const std::string source :
         {"def f() -> i32 { return missing; }", "def f(x: i32, x: i32) -> void {}",
          "def f() -> void {} def f() -> void {}",
          "def f() -> void { def x: i32 = 1; def x: i32 = 2; }"}) {
        GloinParser parser{Lexer(source)};
        auto parsed = parser.parse_checked_program();
        ASSERT_TRUE(parsed.success);
        Sema sema;
        EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
        EXPECT_TRUE(sema.has_error());
    }
}

TEST(CheckedProgramTest, FunctionValuesCannotMasqueradeAsScalars) {
    mlir::MLIRContext context;
    auto result = compile_source("def f() -> i32 { return 1; } def main() -> i32 { return f; }",
                                 "function.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
}

TEST(CheckedProgramTest, UnsupportedTargetDoesNotGuessUsize) {
    GloinParser parser{Lexer("def f(x: usize) -> usize { return x; }")};
    auto parsed = parser.parse_checked_program();
    ASSERT_TRUE(parsed.success);
    Sema sema;
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program), TargetInfo{32}), nullptr);
    ASSERT_TRUE(sema.has_error());
}

TEST(CheckedProgramTest, IndependentRunsCannotReuseSemanticBindings) {
    Sema sema;
    auto parse = [](const std::string &source) {
        return GloinParser(Lexer(source)).parse_checked_program();
    };
    auto first = parse("def f(x: i32) -> i32 { return x; }");
    auto one = sema.check_for_codegen(std::move(first.program));
    ASSERT_NE(one, nullptr);
    auto second = parse("def f(x: bool) -> bool { return x; }");
    auto two = sema.check_for_codegen(std::move(second.program));
    ASSERT_NE(two, nullptr);
    EXPECT_EQ(one->symbols()[0].type, CoreType::I32);
    EXPECT_EQ(two->symbols()[0].type, CoreType::Bool);
    auto invalid = parse("def main() -> i32 { return f(1); }");
    EXPECT_EQ(sema.check_for_codegen(std::move(invalid.program)), nullptr);
}

TEST(CheckedProgramTest, UnimplementedContractsCannotEmitWrongTypedOperations) {
    mlir::MLIRContext context;
    for (const std::string source :
         {"def f(x: f64) -> f64 { return x + x; }", "def f(x: u64) -> u64 { return x / x; }",
          "def f(x: u64) -> bool { return x < x; }", "def f(x: bool) -> i32 { return x; }",
          "def f(x: i32) -> u32 { return x; }", "def f() -> i32 { return; }"}) {
        auto result = compile_source(source, "pending.gloin", context);
        EXPECT_FALSE(result.success()) << source;
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Codegen);
    }
}
