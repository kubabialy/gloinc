#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "sema.h"
#include <gtest/gtest.h>

namespace {
auto parse(const std::string &source) {
    return GloinParser(Lexer(source, "scopes.gloin")).parse_checked_program();
}

void expect_semantic_error(const std::string &source, const std::string &message,
                           const std::string &spelling = "") {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "scopes.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
    const auto &errors = result.diagnostics->all();
    auto found = std::find_if(errors.begin(), errors.end(), [&](const auto &error) {
        return error.message.find(message) != std::string::npos;
    });
    ASSERT_NE(found, errors.end()) << message;
    ASSERT_NE(found->span.source, nullptr);
    EXPECT_EQ(found->span.source->name, "scopes.gloin");
    if (!spelling.empty())
        EXPECT_EQ(
            found->span.source->text.substr(found->span.begin, found->span.end - found->span.begin),
            spelling);
}
} // namespace

TEST(ScopeTest, ForwardSignaturesProduceVerifiedCalls) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def main() -> i32 { later(); return identity(42); }
        def pub identity(value: int) -> i32 { return value; }
        def later() -> void {}
    )",
                                 "forward.gloin", context);
    ASSERT_TRUE(result.success());
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
    int functions = 0;
    int calls = 0;
    result.module->walk([&](mlir::func::FuncOp function) {
        ++functions;
        EXPECT_FALSE(function.isExternal());
    });
    result.module->walk([&](mlir::func::CallOp call) {
        ++calls;
        auto target = result.module->lookupSymbol<mlir::func::FuncOp>(call.getCallee());
        ASSERT_TRUE(target);
        EXPECT_EQ(call.getOperandTypes(), target.getFunctionType().getInputs());
        EXPECT_EQ(call.getResultTypes(), target.getFunctionType().getResults());
    });
    EXPECT_EQ(functions, 3);
    EXPECT_EQ(calls, 2);
}

TEST(ScopeTest, ForwardCallsStillCheckArguments) {
    expect_semantic_error("def main() -> i32 { return later(true); } "
                          "def later(x: i32) -> i32 { return x; }",
                          "Argument 1 type mismatch");
    expect_semantic_error("def main() -> i32 { return later(); } "
                          "def later(x: i32) -> i32 { return x; }",
                          "Incorrect number of arguments");
}

TEST(ScopeTest, DuplicateFunctionsCannotOverloadOrReplaceEarlierDefinitions) {
    for (const std::string second :
         {"def f() -> i32 { return 2; }", "def pub f(x: bool) -> bool { return x; }"})
        expect_semantic_error("def f() -> i32 { return 1; } " + second, "Duplicate declaration 'f'",
                              "f");
}

TEST(ScopeTest, ParametersAndBodyLocalsShareScope) {
    for (const std::string source :
         {"def f(x: i32, x: i32) -> i32 { return x; }",
          "def f(x: i32) -> i32 { def x: i32 = 1; return x; }",
          "def f() -> i32 { def x: i32 = 1; def mut x: i32 = 2; return x; }",
          "def f() -> void { { def x: i32 = 1; def x: i32 = 2; } }"})
        expect_semantic_error(source, "Duplicate declaration 'x'", "x");
}

TEST(ScopeTest, LocalBindingsAreNotHoistedOrVisibleInTheirOwnInitializer) {
    for (const std::string source : {"def f() -> i32 { x; def x: i32 = 1; return x; }",
                                     "def f() -> i32 { def x: i32 = x; return x; }"})
        expect_semantic_error(source, "Undefined variable 'x'", "x");
}

TEST(ScopeTest, BlocksBranchesLoopsAndFunctionsDoNotLeakBindings) {
    for (const std::string source :
         {"def f() -> i32 { { def x: i32 = 1; } return x; }",
          "def f() -> i32 { { def x: i32 = 1; } { return x; } }",
          "def f() -> i32 { if true { def x: i32 = 1; } else { return x; } return 0; }",
          "def f() -> i32 { if true { def x: i32 = 1; } return x; }",
          "def f() -> i32 { while false { def x: i32 = 1; } return x; }",
          "def f(x: i32) -> i32 { return x; } def g() -> i32 { return x; }",
          "def f() -> i32 { def x: i32 = 1; return x; } def g() -> i32 { return x; }"})
        expect_semantic_error(source, "Undefined variable 'x'", "x");
}

TEST(ScopeTest, NearestBindingControlsCallResolution) {
    for (const std::string body :
         {"def f: i32 = 1; return f();", "{ def f: i32 = 1; return f(); }"})
        expect_semantic_error("def main() -> i32 { " + body + " } def f() -> i32 { return 42; }",
                              "Expression is not callable");
    expect_semantic_error("def f(f: i32) -> i32 { return f(); }", "Expression is not callable");
    expect_semantic_error("def f() -> i32 { return missing(); }", "Undefined variable 'missing'",
                          "missing");
}

TEST(ScopeTest, ParameterMutabilityAndShadowingAreIndependent) {
    auto parsed = parse("def f(x: i32) -> i32 { { def mut x: i32 = x; x = 7; } return x; }");
    ASSERT_TRUE(parsed.success);
    Sema sema;
    auto checked = sema.check_for_codegen(std::move(parsed.program));
    ASSERT_NE(checked, nullptr);
    ASSERT_EQ(checked->symbols().size(), 3u);
    EXPECT_FALSE(checked->symbols()[0].is_mutable);
    EXPECT_EQ(checked->symbols()[1].kind, SymbolKind::Parameter);
    EXPECT_FALSE(checked->symbols()[1].is_mutable);
    EXPECT_TRUE(checked->symbols()[2].is_mutable);
    EXPECT_NE(checked->symbols()[1].id, checked->symbols()[2].id);
    expect_semantic_error("def f(x: i32) -> i32 { x = 1; return x; }",
                          "Cannot assign to immutable variable 'x'");
    expect_semantic_error(
        "def f(x: i32) -> i32 { { def mut x: i32 = 0; x = 7; } x = 1; return x; }",
        "Cannot assign to immutable variable 'x'");
}

TEST(ScopeTest, SignaturesAreCheckedBeforeBodies) {
    expect_semantic_error("def main() -> i32 { return missing; } def later(x: Mystery) -> void {}",
                          "Unknown or unsupported core type 'Mystery'", "Mystery");
    auto parsed = parse("def main() -> i32 { return missing; } def later(x: Mystery) -> void {}");
    Sema sema;
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
    for (const auto &error : sema.diagnostics()->all())
        EXPECT_EQ(error.message.find("missing"), std::string::npos);
}

TEST(ScopeTest, BuiltinTypeNamesCannotBeRedeclaredThroughAstApi) {
    for (const std::string name : {"i32", "int", "usize", "bool"}) {
        auto parsed = parse("def f() -> void {}");
        ASSERT_TRUE(parsed.success);
        auto *function = dynamic_cast<FunctionDefinition *>(parsed.program.front().get());
        ASSERT_NE(function, nullptr);
        function->name->value = name;
        Sema sema;
        EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
        ASSERT_FALSE(sema.diagnostics()->all().empty());
        EXPECT_NE(sema.diagnostics()->all().front().message.find("Cannot redeclare built-in type"),
                  std::string::npos);
    }
}

TEST(ScopeTest, CompilerInvocationsDoNotShareCollectedFunctions) {
    Sema sema;
    for (const std::string type : {"i32", "bool"}) {
        auto parsed = parse("def first(x: " + type + ") -> " + type +
                            " { return later(x); } "
                            "def later(x: " +
                            type + ") -> " + type + " { return x; }");
        ASSERT_TRUE(parsed.success);
        auto checked = sema.check_for_codegen(std::move(parsed.program));
        ASSERT_NE(checked, nullptr);
        mlir::MLIRContext context;
        CodeGen generator(context);
        auto module = generator.generate(*checked);
        ASSERT_TRUE(module);
        EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    }
    auto parsed = parse("def main() -> i32 { return later(42); }");
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
    // A new compiler invocation succeeds even after another invocation failed.
    mlir::MLIRContext context;
    EXPECT_FALSE(
        compile_source("def main() -> i32 { return later(42); }", "bad.gloin", context).success());
    auto result = compile_source("def main() -> i32 { return later(42); } "
                                 "def later(x: i32) -> i32 { return x; }",
                                 "good.gloin", context);
    ASSERT_TRUE(result.success());
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
}
