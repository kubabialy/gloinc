#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "parser.h"
#include "sema.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include <gtest/gtest.h>

namespace {
void rejects(const std::string &source) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "operators.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, DiagnosticStage::Semantic);
    ASSERT_FALSE(result.diagnostics->all().empty());
    EXPECT_TRUE(result.diagnostics->all().front().span.source);
}

llvm::Expected<int> execute(mlir::ModuleOp module) {
    if (mlir::failed(mlir::verify(module))) {
        ADD_FAILURE() << "Invalid generated module";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "Verification failed");
    }
    std::string ir;
    llvm::raw_string_ostream stream(ir);
    module.print(stream);
    return gloin_test::run_external_mlir(ir, {gloin_test::mlir_opt, {}},
                                         {gloin_test::mlir_runner, {}});
}

llvm::Expected<int> run(const std::string &source) {
    mlir::MLIRContext context;
    auto result = compile_source(source, "operators.gloin", context, CompilationMode::Executable);
    if (!result.success()) {
        std::ostringstream errors;
        result.diagnostics->render(errors);
        ADD_FAILURE() << source << "\n" << errors.str();
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "Compilation failed");
    }
    return execute(*result.module);
}

void returns_42(const std::string &source) {
    SCOPED_TRACE(source);
    auto result = run(source);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

void traps(const std::string &source) {
    SCOPED_TRACE(source);
    auto result = run(source);
    ASSERT_FALSE(static_cast<bool>(result));
    const auto error = llvm::toString(result.takeError());
    // A compiler/optimizer error or a timeout is not evidence of a runtime trap.
    EXPECT_TRUE(error.starts_with(std::string(gloin_test::mlir_runner) + " failed (status -"))
        << error;
    EXPECT_EQ(error.find("timed out"), std::string::npos) << error;
    EXPECT_TRUE(error.find("Trace/BPT trap") != std::string::npos ||
                error.find("Trace/breakpoint trap") != std::string::npos ||
                error.find("Illegal instruction") != std::string::npos)
        << error;
}

std::string integer_max(const CoreTypeInfo &type) {
    llvm::SmallString<32> text;
    auto value = type.is_signed ? llvm::APInt::getSignedMaxValue(type.bits)
                                : llvm::APInt::getMaxValue(type.bits);
    value.toString(text, 10, type.is_signed);
    return text.str().str();
}
std::string integer_min(const CoreTypeInfo &type) {
    llvm::SmallString<32> text;
    llvm::APInt::getSignedMinValue(type.bits).toString(text, 10, true);
    return text.str().str();
}
std::string runtime_expression(const std::string &type, const std::string &left,
                               const std::string &op, const std::string &right) {
    return "def calculate(a: " + type + ", b: " + type + ") -> " + type + " { return a " + op +
           " b; } def main() -> i32 { calculate(" + left + ", " + right + "); return 42; }";
}
} // namespace

TEST(OperatorsTest, ValidOperatorsProduceVerifiedScalarSignatures) {
    for (const auto &info : core_types) {
        if (info.id == CoreType::Void)
            continue;
        const std::string type(info.name);
        for (const std::string op :
             {"+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||"}) {
            const bool logical = op == "&&" || op == "||";
            const bool comparison =
                op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
            if (info.id == CoreType::Bool ? !(logical || op == "==" || op == "!=")
                                          : logical || (op == "%" && !info.is_integer))
                continue;
            SCOPED_TRACE(type + " " + op);
            mlir::MLIRContext context;
            auto result = compile_source("def f(a: " + type + ", b: " + type + ") -> " +
                                             (logical || comparison ? "bool" : type) +
                                             " { return a " + op + " b; }",
                                         "operators.gloin", context);
            ASSERT_TRUE(result.success());
            EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
        }
    }
}

TEST(OperatorsTest, InvalidOperandCategoriesFailBeforeCodegen) {
    for (const std::string expression :
         {"true + false", "true - false", "true * false", "true / false", "true % false",
          "true < false", "true <= false", "true > false", "true >= false", "1 && 2", "1 || 2",
          "1.0 % 2.0", "!1", "!1.0", "-true", "true == 1", "1 + 1.0"}) {
        rejects("def f() -> void { " + expression + "; }");
        rejects("def const BAD: bool = " + expression + ";");
    }
    rejects("def f(x: u32) -> void { -x; }");
    rejects("def f(x: i32, y: u32) -> void { x == y; }");
    rejects("def f(x: f32, y: f64) -> void { x + y; }");
    rejects("def f() -> bool { return false && (1 + true); }");
}

TEST(OperatorsTest, UnsupportedOperatorsAndAssignmentExpressionsCannotBypassParser) {
    for (const std::string expression : {"1 << 2", "1 >> 2", "1 & 2", "1 | 2", "1 ^ 2", "+1", "~1",
                                         "x += 1", "x -= 1", "x *= 2", "x /= 2"}) {
        mlir::MLIRContext context;
        auto result = compile_source("def f() -> void { def mut x: i32 = 0; " + expression + "; }",
                                     "operators.gloin", context);
        EXPECT_FALSE(result.success()) << expression;
        EXPECT_FALSE(result.module);
    }
    for (const std::string op : {"<<", ">>", "&", "|", "^", "+="}) {
        auto parsed =
            GloinParser(Lexer("def f() -> i32 { return 1 + 2; }")).parse_checked_program();
        auto *function = dynamic_cast<FunctionDefinition *>(parsed.program[0].get());
        auto *ret = dynamic_cast<ReturnStatement *>(function->body->statements[0].get());
        dynamic_cast<InfixExpression *>(ret->return_value.get())->op = op;
        Sema sema;
        EXPECT_FALSE(sema.check_for_codegen(std::move(parsed.program))) << op;
    }
    auto parsed = GloinParser(Lexer("def f() -> i32 { def mut x: i32 = 0; x = 1; return x; }"))
                      .parse_checked_program();
    auto *function = dynamic_cast<FunctionDefinition *>(parsed.program[0].get());
    auto *assignment = dynamic_cast<ExpressionStatement *>(function->body->statements[1].get());
    auto *ret = dynamic_cast<ReturnStatement *>(function->body->statements[2].get());
    ret->return_value = std::move(assignment->expression);
    function->body->statements.erase(function->body->statements.begin() + 1);
    Sema sema;
    EXPECT_FALSE(sema.check_for_codegen(std::move(parsed.program)));
}

TEST(OperatorsTest, IntegerArithmeticAndComparisonsExecuteAtEveryWidth) {
    for (const auto &info : core_types) {
        if (!info.is_integer)
            continue;
        const std::string type(info.name);
        returns_42("def check(a: " + type + ", b: " + type +
                   ") -> bool { return "
                   "a + b == 10 && a - b == 4 && a * b == 21 && a / b == 2 && a % b == 1 && "
                   "a > b && a >= b && b < a && b <= a && a != b && a == a && a <= a && a >= a; } "
                   "def main() -> i32 { if check(7, 3) { return 42; } return 0; }");
        if (info.is_signed)
            returns_42("def check(a: " + type + ", b: " + type +
                       ") -> bool { return "
                       "-a == 7 && a / b == -2 && a % b == -1 && a / -b == 2 && "
                       "a % -b == -1 && b / a == 0 && "
                       "b % a == 3 && a < b && a <= b && !(a > b) && !(a >= b); } "
                       "def main() -> i32 { if check(-7, 3) { return 42; } return 0; }");
        else
            returns_42("def check(a: " + type + ", b: " + type +
                       ") -> bool { return "
                       "a > b && a >= b && !(a < b) && !(a <= b) && a / a == 1 && a % a == 0 "
                       "&& a / b > b && a % b == 1; } "
                       "def main() -> i32 { if check(" +
                       integer_max(info) + ", 2) { return 42; } return 0; }");
    }
}

TEST(OperatorsTest, FloatingArithmeticAndComparisonsExecuteAtBothWidths) {
    for (const std::string type : {"f32", "f64"})
        returns_42("def check(a: " + type + ", b: " + type +
                   ") -> bool { return "
                   "a + b == 10.0 && a - b == 4.0 && a * b == 21.0 && a / b > 2.0 && "
                   "-a == -7.0 && a > b && a >= b && b < a && b <= a && a != b && a == a && "
                   "a <= a && a >= a && !(a < a) && !(a > a) && !(a != a) && -0.0 == 0.0; } "
                   "def main() -> i32 { if check(7.0, 3.0) { return 42; } return 0; }");
}

TEST(OperatorsTest, RepresentableIntegerBoundaryResultsDoNotTrap) {
    for (const auto &info : core_types) {
        if (!info.is_integer)
            continue;
        const std::string type(info.name);
        returns_42(
            "def check(high: " + type + ", low: " + type +
            ") -> bool { return "
            "high + 0 == high && high - 0 == high && high * 1 == high && high / 1 == high && "
            "high - high == 0 && low + 0 == low && low - 0 == low && low * 1 == low && "
            "low / 1 == low && low % 1 == 0; } "
            "def main() -> i32 { if check(" +
            integer_max(info) + ", " + (info.is_signed ? integer_min(info) : "0") +
            ") { return 42; } return 0; }");
    }
}

TEST(OperatorsTest, IntegerAdditionOverflowTrapsAtEveryWidth) {
    for (const auto &info : core_types)
        if (info.is_integer)
            traps(runtime_expression(std::string(info.name), integer_max(info), "+", "1"));
}

TEST(OperatorsTest, IntegerSubtractionOverflowTrapsAtEveryWidth) {
    for (const auto &info : core_types)
        if (info.is_integer)
            traps(runtime_expression(std::string(info.name),
                                     info.is_signed ? integer_min(info) : "0", "-", "1"));
}

TEST(OperatorsTest, IntegerMultiplicationOverflowTrapsAtEveryWidth) {
    for (const auto &info : core_types)
        if (info.is_integer)
            traps(runtime_expression(std::string(info.name), integer_max(info), "*", "2"));
}

TEST(OperatorsTest, SignedMinimumNegationAndDivisionOverflowTrap) {
    for (const auto &info : core_types) {
        if (!info.is_integer || !info.is_signed)
            continue;
        const std::string type(info.name);
        traps("def negate(x: " + type + ") -> " + type +
              " { return -x; } "
              "def main() -> i32 { negate(" +
              integer_min(info) + "); return 42; }");
        traps(runtime_expression(type, integer_min(info), "/", "-1"));
        traps(runtime_expression(type, integer_min(info), "%", "-1"));
    }
}

TEST(OperatorsTest, ZeroDivisorsTrapForEveryNumericType) {
    for (const auto &info : core_types) {
        if (info.id == CoreType::Void || info.id == CoreType::Bool)
            continue;
        const std::string type(info.name);
        traps(runtime_expression(type, info.is_integer ? "1" : "1.0", "/",
                                 info.is_integer ? "0" : "0.0"));
        if (info.is_integer)
            traps(runtime_expression(type, "1", "%", "0"));
        else
            traps(runtime_expression(type, "1.0", "/", "-0.0"));
    }
}

TEST(OperatorsTest, NonFiniteFloatingResultsTrap) {
    for (const std::string type : {"f32", "f64"}) {
        const std::string large = type == "f32" ? "3e38" : "1e308";
        traps(runtime_expression(type, large, "+", large));
        traps(runtime_expression(type, "-" + large, "-", large));
        traps(runtime_expression(type, large, "*", large));
        traps(runtime_expression(type, large, "/", "0.1"));
    }
}

TEST(OperatorsTest, BooleanTruthTablesAndShortCircuitSkipTrappingCalls) {
    returns_42(R"(
        def danger() -> bool { def x: i32 = 1; return x / 0 == 0; }
        def main() -> i32 {
            def t: bool = true; def f: bool = false;
            if !(t && t) || (t && f) || (f && t) || (f && f) { return 0; }
            if !(t || t) || !(t || f) || !(f || t) || (f || f) { return 0; }
            if t == f || t != t || !t || !!f { return 0; }
            if f && danger() { return 0; }
            if !(t || danger()) { return 0; }
            if (t || danger()) && (f || (t && !f)) { return 42; }
            return 0;
        }
    )");
    for (const std::string expression : {"true && danger()", "false || danger()"})
        traps("def danger() -> bool { def x: i32 = 1; return x / 0 == 0; } "
              "def main() -> i32 { " +
              expression + "; return 42; }");
}

TEST(OperatorsTest, NestedOperatorsWorkInLoopConditionsStoresAndCallArguments) {
    returns_42(R"(
        def add(a: i32, b: i32) -> i32 { return a + b; }
        def main() -> i32 {
            def mut count: i32 = 0;
            while count < 3 && !(count == 4 || count > 10) {
                if count % 2 == 0 || count == 1 { count = count + 1; }
            }
            def mut value: bool = false;
            value = count == 3 && true;
            if value && add((10 + 4) * 2, 28 / 2) == 42 { return 42; }
            return 0;
        }
    )");
}

TEST(OperatorsTest, FloatingRoundingSubnormalsAndSignedZeroPreserveExactBits) {
    struct Case {
        std::string type, value, expression;
        unsigned bits;
        uint64_t expected;
    };
    for (const auto &test : std::vector<Case>{
             {"f32", "16777216.0", "(a + 1.0) - a", 32, 0},
             {"f64", "9007199254740992.0", "(a + 1.0) - a", 64, 0},
             {"f32", "0.0", "-a", 32, 0x80000000},
             {"f64", "0.0", "-a", 64, 0x8000000000000000},
             {"f32", "2.802596928649634e-45", "a / 2.0", 32, 1},
             {"f64", "9.8813129168249309e-324", "a / 2.0", 64, 1},
             {"f32", "1.401298464324817e-45", "-a / 2.0", 32, 0x80000000},
             {"f64", "4.9406564584124654e-324", "-a / 2.0", 64, 0x8000000000000000}}) {
        SCOPED_TRACE(test.type + " " + test.expression);
        mlir::MLIRContext context;
        auto result = compile_source("def calculate(a: " + test.type + ") -> " + test.type +
                                         " { return " + test.expression +
                                         "; } "
                                         "def value() -> " +
                                         test.type + " { return calculate(" + test.value + "); }",
                                     "float-operators.gloin", context);
        ASSERT_TRUE(result.success());
        // Test-only bitcast adapter; no bitcast syntax is added to the language.
        mlir::OpBuilder builder(&context);
        auto loc = builder.getUnknownLoc();
        builder.setInsertionPointToEnd(result.module->getBody());
        auto main = builder.create<mlir::func::FuncOp>(
            loc, "main", builder.getFunctionType({}, {builder.getI32Type()}));
        builder.setInsertionPointToStart(main.addEntryBlock());
        auto value = builder.create<mlir::func::CallOp>(
            loc, result.module->lookupSymbol<mlir::func::FuncOp>("value"), mlir::ValueRange{});
        auto bits = builder.create<mlir::arith::BitcastOp>(loc, builder.getIntegerType(test.bits),
                                                           value.getResult(0));
        auto expected = builder.create<mlir::arith::ConstantOp>(
            loc, builder.getIntegerAttr(builder.getIntegerType(test.bits),
                                        llvm::APInt(test.bits, test.expected)));
        auto equal = builder.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, bits,
                                                         expected);
        auto yes = builder.create<mlir::arith::ConstantIntOp>(loc, 42, 32);
        auto no = builder.create<mlir::arith::ConstantIntOp>(loc, 0, 32);
        auto answer = builder.create<mlir::arith::SelectOp>(loc, equal, yes, no);
        builder.create<mlir::func::ReturnOp>(loc, answer.getResult());
        auto executed = execute(*result.module);
        ASSERT_TRUE(static_cast<bool>(executed)) << llvm::toString(executed.takeError());
        EXPECT_EQ(*executed, 42);
    }
}

TEST(OperatorsTest, InstrumentedCallsProveShortCircuitAndOnceOnlyLeftToRightEvaluation) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def probe(tag: i32, answer: bool) -> bool { return answer; }
        def numeric(tag: i32) -> i32 { return tag; }
        def read_trace() -> i32 { return 0; }
        def add(a: i32, b: i32) -> i32 { return a + b; }
        def main() -> i32 {
            false && probe(9, true);
            true || probe(9, false);
            probe(1, false) && probe(9, true);
            probe(2, true) || probe(9, false);
            probe(3, true) && probe(4, true);
            def value: i32 = numeric(5) + numeric(6) * numeric(7);
            def args: i32 = add(numeric(8), numeric(9));
            if value == 47 && args == 17 && read_trace() == 123456789 { return 42; }
            return 0;
        }
    )",
                                 "effects.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(result.success());
    // Test instrumentation makes call order observable without introducing
    // language globals or references. Source call/control-flow IR is untouched.
    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();
    builder.setInsertionPointToStart(result.module->getBody());
    auto trace = builder.create<mlir::LLVM::GlobalOp>(loc, builder.getI32Type(), false,
                                                      mlir::LLVM::Linkage::Internal, "test.trace",
                                                      builder.getI32IntegerAttr(0));
    for (const std::string name : {"probe", "numeric", "read_trace"}) {
        auto function = result.module->lookupSymbol<mlir::func::FuncOp>(name);
        builder.setInsertionPointToStart(&function.getBody().front());
        auto address = builder.create<mlir::LLVM::AddressOfOp>(loc, trace);
        auto prior = builder.create<mlir::LLVM::LoadOp>(loc, builder.getI32Type(), address);
        if (name == "read_trace") {
            function.getBody().front().getTerminator()->setOperand(0, prior);
        } else {
            auto ten = builder.create<mlir::arith::ConstantIntOp>(loc, 10, 32);
            auto shifted = builder.create<mlir::arith::MulIOp>(loc, prior, ten);
            auto next = builder.create<mlir::arith::AddIOp>(loc, shifted, function.getArgument(0));
            builder.create<mlir::LLVM::StoreOp>(loc, next, address);
        }
    }
    auto executed = execute(*result.module);
    ASSERT_TRUE(static_cast<bool>(executed)) << llvm::toString(executed.takeError());
    EXPECT_EQ(*executed, 42);
}
