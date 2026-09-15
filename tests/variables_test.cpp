#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "sema.h"
#include <gtest/gtest.h>

namespace {
void rejects(const std::string &source, const std::string &message,
             DiagnosticStage stage = DiagnosticStage::Semantic) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "variables.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, stage);
    bool found = false;
    for (const auto &error : result.diagnostics->all()) {
        if (error.message.find(message) != std::string::npos) {
            found = true;
            ASSERT_NE(error.span.source, nullptr);
            EXPECT_EQ(error.span.source->name, "variables.gloin");
            EXPECT_LE(error.span.begin, error.span.end);
            if (error.span.begin == error.span.end)
                EXPECT_EQ(error.span.end, source.size());
        }
    }
    EXPECT_TRUE(found) << message;
}

void verifies(const std::string &source) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "variables.gloin", context);
    std::ostringstream errors;
    result.diagnostics->render(errors);
    ASSERT_TRUE(result.success()) << errors.str();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
}
} // namespace

TEST(VariablesTest, InvalidTargetsFailBeforeCodegen) {
    rejects("def f() -> void { 1 = 2; }", "Expected assignable target", DiagnosticStage::Parsing);
    rejects("def f() -> void { (1 + 2) = 3; }", "Expected assignable target",
            DiagnosticStage::Parsing);
    rejects("def f() -> void { missing = 2; }", "Undefined variable 'missing'");
    rejects("def f() -> void { f = 2; }", "Cannot assign to immutable variable 'f'");
    // The AST API must enforce the same target restriction without relying on parsing.
    auto parsed = GloinParser(Lexer("def f() -> void { def mut x: i32 = 1; x = 2; }"))
                      .parse_checked_program();
    ASSERT_TRUE(parsed.success);
    auto *function = dynamic_cast<FunctionDefinition *>(parsed.program.front().get());
    auto *statement = dynamic_cast<ExpressionStatement *>(function->body->statements.back().get());
    auto *assignment = dynamic_cast<AssignmentExpression *>(statement->expression.get());
    assignment->left = std::make_unique<IntegerLiteral>(1, "1");
    Sema sema;
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
    EXPECT_TRUE(sema.has_error());
}

TEST(VariablesTest, BindingTypesAreRequiredAndStoresMustMatch) {
    rejects("def f() -> void { def mut x = 1; }", "type", DiagnosticStage::Parsing);
    rejects("def f() -> void { def mut n: i8 = 3.14; }", "Type mismatch in variable declaration");
    rejects("def f() -> void { def mut n: i32; n = true; }", "Type mismatch in assignment");
    rejects("def f(x: u32) -> void { def mut n: i32 = x; }",
            "Type mismatch in variable declaration");
    rejects("def f(x: u32) -> void { def mut n: i32; n = x; }", "Type mismatch in assignment");
    rejects("def f() -> void { def x: void; }", "void is only allowed");
}

TEST(VariablesTest, UninitializedReadsIncludeConditionsArgumentsAndRightHandSides) {
    for (const std::string body :
         {"def mut x: i32; return x;", "def x: i32; return x;",
          "def mut x: i32; x = x + 1; return 0;",
          "def mut x: bool; if x { return 1; } else { return 0; }",
          "def mut x: i32; def y: i32 = x; return 0;", "def mut x: i32; return identity(x);"})
        rejects("def identity(n: i32) -> i32 { return n; } def f() -> i32 { " + body + " }",
                "Read of uninitialized variable 'x'");
}

TEST(VariablesTest, ImmutableBindingsPermitOnlyTheirFirstDefiniteInitialization) {
    verifies("def f() -> i32 { def x: i32; x = 42; return x; }");
    for (const std::string body : {"def x: i32 = 1; x = 2;", "def x: i32; x = 1; x = 2;",
                                   "def x: i32; if true { x = 1; } x = 2;"})
        rejects("def f() -> void { " + body + " }", "Cannot assign to immutable variable 'x'");
    rejects("def f(x: i32) -> void { x = 2; }", "Cannot assign to immutable variable 'x'");
}

TEST(VariablesTest, BothBranchesMustInitializeAndReturningBranchesAreExcluded) {
    for (const std::string modifier : {"", "mut "}) {
        verifies("def f(b: bool) -> i32 { def " + modifier +
                 "x: i32; if b { x = 42; } else { x = 7; } return x; }");
        verifies("def f(b: bool) -> i32 { def " + modifier +
                 "x: i32; if b { return 7; } else { x = 42; } return x; }");
        verifies("def f(b: bool) -> i32 { def " + modifier +
                 "x: i32; if b { x = 42; } else { return 7; } return x; }");
        rejects("def f(b: bool) -> i32 { def " + modifier + "x: i32; if b { x = 42; } return x; }",
                "Read of uninitialized variable 'x'");
    }
    verifies("def f(b: bool) -> i32 { def x: i32; if b { return 1; } x = 42; return x; }");
    rejects("def f() -> i32 { def mut x: i32; if true { x = 42; } return x; }",
            "Read of uninitialized variable 'x'");
}

TEST(VariablesTest, NestedBranchesMergeInitializationByDeclarationIdentity) {
    verifies(R"(def f(a: bool, b: bool) -> i32 {
        def x: i32;
        if a { if b { x = 1; } else { return 2; } }
        else { if b { return 3; } else { x = 4; } }
        return x;
    })");
    rejects(R"(def f(a: bool, b: bool) -> i32 {
        def mut x: i32;
        if a { if b { x = 1; } } else { x = 2; }
        return x;
    })",
            "Read of uninitialized variable 'x'");
}

TEST(VariablesTest, LoopsDoNotEstablishInitializationAfterExit) {
    rejects("def f(b: bool) -> i32 { def mut x: i32; while b { x = 1; } return x; }",
            "Read of uninitialized variable 'x'");
    rejects("def f(b: bool) -> void { def x: i32; while b { x = 1; } }",
            "Cannot assign to immutable variable 'x'");
    rejects("def f(b: bool) -> void { def mut x: i32; while b { x = x + 1; } }",
            "Read of uninitialized variable 'x'");
    verifies(
        "def f(b: bool) -> i32 { def mut x: i32 = 7; while b { x = 42; return x; } return x; }");
    verifies("def f(b: bool) -> void { while b { def x: i32; x = 42; x; } }");
}

TEST(VariablesTest, ShadowingCannotInitializeOrReadAnOuterUninitializedBinding) {
    rejects("def f() -> i32 { def mut x: i32; { def x: i32 = 42; } return x; }",
            "Read of uninitialized variable 'x'");
    rejects("def f() -> void { def mut x: i32; { def x: i32 = x; } }",
            "Read of uninitialized variable 'x'");
    verifies("def f() -> i32 { def x: i32; { def mut x: i32 = 7; x = 9; } x = 42; return x; }");
}

TEST(VariablesTest, InitializationUsesResolvedStorageWidths) {
    for (const auto &type : core_types) {
        if (type.id == CoreType::Void)
            continue;
        std::string name(type.name);
        verifies("def f(value: " + name + ") -> " + name + " { def x: " + name +
                 "; x = value; def mut y: " + name + "; y = x; return y; }");
    }
}

TEST(VariablesTest, ConstantsRequireTypesInitializersAndImmutability) {
    rejects("def const X: i32;", "requires an initializer", DiagnosticStage::Parsing);
    rejects("def const X = 1;", "type", DiagnosticStage::Parsing);
    rejects("def const mut X: i32 = 1;", "cannot be combined", DiagnosticStage::Parsing);
    rejects("def const X: i32 = true;", "Type mismatch in constant declaration");
    rejects("def const X: i64 = 1;", "Type mismatch in constant declaration");
    rejects("def const X: i32 = 1; def f() -> void { X = 2; }",
            "Cannot assign to immutable variable 'X'");
    rejects("def f() -> void { def const X: i32 = 1; X = 2; }",
            "Cannot assign to immutable variable 'X'");
}

TEST(VariablesTest, ConstantsRejectRuntimeBindingsAndCallsEvenWhenUnused) {
    rejects("def runtime() -> i32 { return 1; } def const X: i32 = runtime();",
            "Expression is not permitted in a constant initializer");
    for (const std::string prefix : {"def x: i32 = 1;", "def mut x: i32 = 1;"})
        rejects("def f() -> void { " + prefix + " def const X: i32 = x; }",
                "cannot reference runtime binding 'x'");
    rejects("def f(x: i32) -> void { def const X: i32 = x; }",
            "cannot reference runtime binding 'x'");
    rejects("def runtime() -> bool { return true; } def const X: bool = false && runtime();",
            "Expression is not permitted in a constant initializer");
    rejects("def f() -> void { def const X: bool = true || missing; }",
            "Undefined constant 'missing'");
}

TEST(VariablesTest, ConstantDependenciesFollowLexicalOrderAndShareNamespace) {
    verifies("def f() -> i32 { return X; } def const X: i32 = 42;");
    verifies("def const X: i32 = 40; def const Y: int = X + 2; def f() -> i32 { return Y; }");
    rejects("def const X: i32 = Y; def const Y: i32 = 42;", "Undefined constant 'Y'");
    rejects("def const X: i32 = X;", "Undefined constant 'X'");
    rejects("def const X: i32 = Y; def const Y: i32 = X;", "Undefined constant 'Y'");
    rejects("def const X: i32 = 1; def const X: i32 = 2;", "Duplicate declaration 'X'");
    rejects("def const X: i32 = 1; def X() -> i32 { return 2; }", "Duplicate declaration 'X'");
    rejects("def X() -> i32 { return 2; } def const X: i32 = 1;", "Duplicate declaration 'X'");
    rejects("def f(X: i32) -> void { def const X: i32 = 1; }", "Duplicate declaration 'X'");
    rejects("def f() -> i32 { { def const X: i32 = 1; } return X; }", "Undefined variable 'X'");
}

TEST(VariablesTest, ConstantOperatorsRejectInvalidTypes) {
    for (const std::string expression :
         {"true + false", "1 && 2", "1.0 % 2.0", "!1", "-true", "1 + true"})
        rejects("def const X: i32 = " + expression + ";", "constant expression");
    verifies(
        "def const X: bool = !(1 < 2) || (3 >= 3 && true != false); def f() -> bool { return X; }");
}

TEST(VariablesTest, ConstantArithmeticDiagnosesOverflowAndZeroDivisors) {
    for (const std::string expression :
         {"2147483647 + 1", "(-2147483647 - 1) - 1", "50000 * 50000", "(-2147483647 - 1) / -1",
          "(-2147483647 - 1) % -1", "-(-2147483647 - 1)"})
        rejects("def const X: i32 = " + expression + ";", "Integer overflow");
    for (const std::string expression : {"1 / 0", "1 % 0", "1 / (2 - 2)"})
        rejects("def const X: i32 = " + expression + ";", "by zero");
    rejects("def const X: i32 = 2147483648;", "out of range");
    rejects("def const X: f32 = 1.0 / 0.0;", "by zero");
    rejects("def const X: f32 = 1e30 * 1e30;", "Non-finite result");
    rejects("def const X: f32 = 1e100;", "out of range");
    verifies("def const LOW: i32 = -2147483647 - 1; def f() -> i32 { return LOW; }");
    verifies("def const X: bool = false && (1 / 0 == 0); def f() -> bool { return X; }");
    verifies("def const X: bool = true || (1 / 0 == 0); def f() -> bool { return X; }");
    rejects("def const X: bool = true && (1 / 0 == 0);", "by zero");
}

TEST(VariablesTest, FoldedConstantsHaveNoRuntimeStorageOrArithmetic) {
    mlir::MLIRContext context;
    auto result =
        compile_source("def const GLOBAL: i32 = 6 * 7; "
                       "def f() -> i32 { def const LOCAL: i32 = GLOBAL / 2; return LOCAL; }",
                       "folded.gloin", context);
    ASSERT_TRUE(result.success());
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
    int constants = 0;
    result.module->walk([&](mlir::Operation *operation) {
        EXPECT_FALSE((llvm::isa<mlir::LLVM::AllocaOp, mlir::LLVM::GlobalOp, mlir::arith::MulIOp,
                                mlir::arith::DivSIOp>(operation)));
        if (auto value = llvm::dyn_cast<mlir::arith::ConstantIntOp>(operation)) {
            ++constants;
            EXPECT_EQ(value.value(), 21);
        }
    });
    EXPECT_EQ(constants, 1);
}

TEST(VariablesTest, FloatConstantsRoundToF32AndPreserveNegativeZero) {
    mlir::MLIRContext context;
    auto result = compile_source("def const X: f32 = (16777216.0 + 1.0) - 16777216.0; "
                                 "def f() -> f32 { return X; } "
                                 "def z() -> f32 { def const ZERO: f32 = -0.0; return ZERO; }",
                                 "float.gloin", context);
    ASSERT_TRUE(result.success());
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
    for (const std::string name : {"f", "z"}) {
        auto function = result.module->lookupSymbol<mlir::func::FuncOp>(name);
        int constants = 0;
        function.walk([&](mlir::arith::ConstantOp operation) {
            auto value = llvm::cast<mlir::FloatAttr>(operation.getValue()).getValue();
            EXPECT_TRUE(value.isZero());
            EXPECT_EQ(value.isNegative(), name == "z");
            ++constants;
        });
        EXPECT_EQ(constants, 1);
    }
}

TEST(VariablesTest, InitializationAndConstantValuesDoNotLeakBetweenChecks) {
    Sema sema;
    mlir::MLIRContext context;
    for (int value : {21, 42}) {
        auto parsed = GloinParser(Lexer("def const X: i32 = " + std::to_string(value) +
                                        "; def main() -> i32 { def x: i32; x = X; return x; }"))
                          .parse_checked_program();
        ASSERT_TRUE(parsed.success);
        auto checked = sema.check_for_codegen(std::move(parsed.program));
        ASSERT_NE(checked, nullptr);
        mlir::OwningOpRef<mlir::ModuleOp> module;
        {
            CodeGen generator(context);
            module = generator.generate(*checked);
        }
        checked.reset();
        ASSERT_TRUE(module);
        EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
        int constants = 0;
        module->walk([&](mlir::arith::ConstantIntOp operation) {
            ++constants;
            EXPECT_EQ(operation.value(), value);
        });
        EXPECT_EQ(constants, 1);
    }
    auto parsed = GloinParser(Lexer("def f() -> i32 { return X; }")).parse_checked_program();
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
}
