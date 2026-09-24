#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "sema.h"
#include <gtest/gtest.h>

namespace {
void rejects(const std::string &source, const std::string &message,
             CompilationMode mode = CompilationMode::Module) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "functions.gloin", context, mode);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
    bool found = false;
    for (const auto &error : result.diagnostics->all()) {
        if (error.message.find(message) != std::string::npos) {
            found = true;
            if (!source.empty()) {
                ASSERT_TRUE(error.span.source);
                EXPECT_EQ(error.span.source->name, "functions.gloin");
                EXPECT_LT(error.span.begin, error.span.end);
            }
        }
    }
    EXPECT_TRUE(found) << message;
}

void verifies(const std::string &source, CompilationMode mode = CompilationMode::Module) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "functions.gloin", context, mode);
    std::ostringstream errors;
    result.diagnostics->render(errors);
    ASSERT_TRUE(result.success()) << errors.str();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
    result.module->walk([](mlir::func::ReturnOp ret) {
        auto function = ret->getParentOfType<mlir::func::FuncOp>();
        EXPECT_EQ(ret.getOperandTypes(), function.getFunctionType().getResults());
    });
}
} // namespace

TEST(FunctionsTest, ReturnValuesMustMatchCanonicalTypes) {
    for (const auto &info : core_types) {
        if (info.id == CoreType::Void)
            continue;
        std::string type(info.name);
        verifies("def identity(x: " + type + ") -> " + type + " { return x; }");
        const std::string other = info.id == CoreType::Bool ? "i8" : "bool";
        rejects("def bad(x: " + type + ") -> " + other + " { return x; }", "Return type mismatch");
    }
    rejects("def bad(x: i32) -> u32 { return x; }", "Return type mismatch");
    rejects("def bad(x: f32) -> f64 { return x; }", "Return type mismatch");
    verifies("def identity(x: int) -> i32 { return x; } "
             "def wide(x: usize) -> u64 { return x; }");
}

TEST(FunctionsTest, MissingValuesAndFallthroughFailBeforeCodegen) {
    rejects("def f() -> i32 { return; }", "must return a value");
    for (const std::string body :
         {"", "def x: i32 = 1;", "if b { return 1; }", "{ if b { return 1; } }",
          "if b { return 1; } else {}", "while b { return 1; }", "while true { return 1; }",
          "if true { return 1; }", "f(true);"})
        rejects("def f(b: bool) -> i32 { " + body + " }", "can reach the end");
}

TEST(FunctionsTest, NestedReturningPathsAndLoopFallbacksVerify) {
    verifies("def f(a: bool, b: bool) -> i32 { if a { if b { return 1; } "
             "else { return 2; } } else { { return 3; } } }");
    verifies("def f(a: bool) -> i32 { while a { if a { return 1; } "
             "else { return 2; } } return 3; }");
    verifies("def f(a: bool) -> i32 { if a { return 1; } { return 2; } }");
}

TEST(FunctionsTest, VoidFunctionsAllowBareAndImplicitReturnsOnly) {
    verifies("def empty() -> void {} def bare() -> void { return; } "
             "def branch(b: bool) -> void { if b { return; } empty(); } "
             "def all(b: bool) -> void { if b { return; } else { return; } }");
    rejects("def f() -> void { return 1; }", "Void function cannot return a value");
    rejects("def f() -> void { return f(); }", "Void function cannot return a value");
}

TEST(FunctionsTest, VoidCallsCannotEnterValueContexts) {
    for (const std::string body :
         {"def x: i32 = work();", "def mut x: i32 = 1; x = work();", "take(work());",
          "if work() {}", "work() == work();", "work() + work();", "return work();"})
        rejects("def work() -> void {} def take(x: i32) -> void {} def f() -> void { " + body +
                    " }",
                "void call cannot be used as a value");
    verifies("def value() -> i32 { return 1; } def f() -> void { value(); }");
}

TEST(FunctionsTest, UnreachableSourceIsRejectedAtItsStatement) {
    for (const std::string prefix :
         {"return 1;", "{ return 1; }", "if b { return 1; } else { return 2; }",
          "if b { if b { return 1; } else { return 2; } } else { return 3; }"}) {
        for (const std::string tail : {"return 0;", "def x: i32 = 1;", "{}", "missing();"})
            rejects("def f(b: bool) -> i32 { " + prefix + " " + tail + " }",
                    "Unreachable statement");
    }
    rejects("def f() -> void { return; f(); }", "Unreachable statement");
    // Literal conditions do not hide invalid source or create unconditional returns.
    rejects("def f() -> i32 { if false { return true; } return 1; }", "Return type mismatch");
    rejects("def f() -> void { while false { missing(); } }", "Undefined variable");
}

TEST(FunctionsTest, CallsCheckArityTypesAndDirectNames) {
    const std::string helper = "def take(x: i32, y: bool) -> i32 { return x; } ";
    for (const std::string call : {"take()", "take(1)", "take(1, true, 2)"})
        rejects(helper + "def f() -> i32 { return " + call + "; }", "number of arguments");
    rejects(helper + "def f() -> i32 { return take(true, false); }", "Argument 1 type mismatch");
    rejects(helper + "def f(x: u32) -> i32 { return take(x, true); }", "Argument 1 type mismatch");
    rejects(helper + "def f() -> i32 { return take(1, 2); }", "Argument 2 type mismatch");
    rejects("def f() -> i32 { def f: i32 = 1; return f(); }", "not callable");
    rejects("def f() -> i32 { return 1; } def g() -> i32 { return f()(); }",
            "direct function name");
    rejects("def f() -> i32 { return 1; } def g() -> i32 { return (f + f)(); }",
            "direct function name");
}

TEST(FunctionsTest, ParameterRulesCannotBeBypassed) {
    rejects("def f(x: i32, x: bool) -> void {}", "Duplicate declaration");
    rejects("def f(x: void) -> void {}", "void is only allowed");
    rejects("def f(x: i32) -> void { x = 2; }", "immutable variable");
    rejects("def f(x: i32) -> void { def x: i32 = 2; }", "Duplicate declaration");
    verifies("def f(x: i32) -> i32 { { def x: bool = true; x; } return x; }");
}

TEST(FunctionsTest, ScalarCallSignaturesAndArgumentEvaluationOrderAgree) {
    for (const auto &info : core_types) {
        if (info.id == CoreType::Void)
            continue;
        const std::string type(info.name);
        mlir::MLIRContext context;
        auto result =
            compile_source("def caller(x: " + type + ", y: " + type + ") -> " + type +
                               " { return combine(left(x), right(y)); } " + "def left(x: " + type +
                               ") -> " + type + " { return x; } def right(x: " + type + ") -> " +
                               type + " { return x; } def combine(x: " + type + ", y: " + type +
                               ") -> " + type + " { return y; }",
                           "calls.gloin", context);
        ASSERT_TRUE(result.success()) << type;
        EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
        std::vector<std::string> order;
        result.module->walk([&](mlir::func::CallOp call) {
            order.push_back(call.getCallee().str());
            auto callee = result.module->lookupSymbol<mlir::func::FuncOp>(call.getCallee());
            ASSERT_TRUE(callee);
            EXPECT_EQ(call.getOperandTypes(), callee.getFunctionType().getInputs());
            EXPECT_EQ(call.getResultTypes(), callee.getFunctionType().getResults());
        });
        EXPECT_EQ(order, (std::vector<std::string>{"left", "right", "combine"}));
    }
}

TEST(FunctionsTest, EntrySignatureIsValidatedInBothModes) {
    for (auto mode : {CompilationMode::Module, CompilationMode::Executable}) {
        for (const std::string source :
             {"def main() -> void {}", "def main(x: i32) -> i32 { return x; }",
              "def main() -> u32 { return 0; }", "def main() -> i64 { return 0; }",
              "def main() -> bool { return true; }", "def const main: i32 = 42;"})
            rejects(source, "Entry point must be", mode);
        verifies("def main() -> int { return 42; }", mode);
        verifies("def pub main() -> i32 { return -1; }", mode);
        verifies("def priv main() -> i32 { return 42; }", mode);
        rejects("def main() -> i32 { return 1; } def main() -> i32 { return 2; }",
                "Duplicate declaration", mode);
    }
}

TEST(FunctionsTest, ExecutableModeRequiresEntryButModuleModeDoesNot) {
    for (const std::string source : {"", "def helper() -> i32 { return 42; }",
                                     "def helper() -> void { def main: i32 = 42; }"}) {
        rejects(source, "Executable requires an entry point", CompilationMode::Executable);
        verifies(source);
    }
    Sema sema;
    for (auto mode : {CompilationMode::Executable, CompilationMode::Module}) {
        auto parsed = GloinParser(Lexer(mode == CompilationMode::Executable
                                            ? "def main() -> i32 { return 42; }"
                                            : "def helper() -> void {}"))
                          .parse_checked_program();
        ASSERT_TRUE(parsed.success);
        auto checked = sema.check_for_codegen(std::move(parsed.program), {}, mode);
        ASSERT_TRUE(checked);
        EXPECT_EQ(checked->mode(), mode);
        EXPECT_EQ(checked->entry_point().has_value(), mode == CompilationMode::Executable);
        if (checked->entry_point())
            EXPECT_EQ(checked->symbols().at(*checked->entry_point()).name, "main");
    }
}

TEST(FunctionsTest, TopLevelReturnsFailThroughSourceAndAstApis) {
    mlir::MLIRContext context;
    auto result = compile_source("return 42;", "top.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Parsing);
    auto parsed = GloinParser(Lexer("def f() -> i32 { return 42; }")).parse_checked_program();
    ASSERT_TRUE(parsed.success);
    auto *function = dynamic_cast<FunctionDefinition *>(parsed.program.front().get());
    ASSERT_TRUE(function);
    std::vector<std::unique_ptr<Statement>> program;
    program.push_back(std::move(function->body->statements.front()));
    Sema sema;
    EXPECT_FALSE(sema.check_for_codegen(std::move(program)));
    EXPECT_TRUE(sema.has_error());
}
