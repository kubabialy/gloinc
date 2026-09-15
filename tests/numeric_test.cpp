#include "codegen.h"
#include "compiler.h"
#include "mlir/IR/Verifier.h"
#include "numeric.h"
#include "parser.h"
#include "sema.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
void rejects(const std::string &source, const std::string &message = "",
             DiagnosticStage stage = DiagnosticStage::Semantic) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "numeric.gloin", context);
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.module);
    EXPECT_EQ(result.failed_stage, stage);
    ASSERT_FALSE(result.diagnostics->all().empty());
    bool found = message.empty();
    for (const auto &error : result.diagnostics->all()) {
        if (error.message.find(message) != std::string::npos)
            found = true;
        ASSERT_NE(error.span.source, nullptr);
        EXPECT_EQ(error.span.source->name, "numeric.gloin");
    }
    EXPECT_TRUE(found) << message;
}
void verifies(const std::string &source) {
    SCOPED_TRACE(source);
    mlir::MLIRContext context;
    auto result = compile_source(source, "numeric.gloin", context);
    std::ostringstream errors;
    result.diagnostics->render(errors);
    ASSERT_TRUE(result.success()) << errors.str();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
}
void integer_value(const std::string &type, const std::string &literal, unsigned bits,
                   uint64_t expected) {
    for (const std::string binding : {"mut ", "const "}) {
        SCOPED_TRACE(type + " " + literal + " " + binding);
        mlir::MLIRContext context;
        auto result = compile_source("def f() -> " + type + " { def " + binding + "x: " + type +
                                         " = " + literal + "; return x; }",
                                     "numeric.gloin", context);
        std::ostringstream errors;
        result.diagnostics->render(errors);
        ASSERT_TRUE(result.success()) << errors.str();
        EXPECT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
        int count = 0;
        result.module->walk([&](mlir::arith::ConstantOp operation) {
            auto value = llvm::dyn_cast<mlir::IntegerAttr>(operation.getValue());
            ASSERT_TRUE(value);
            EXPECT_EQ(value.getType().getIntOrFloatBitWidth(), bits);
            EXPECT_EQ(value.getValue().getZExtValue(), expected);
            ++count;
        });
        EXPECT_EQ(count, 1);
        result.module->walk([&](mlir::LLVM::LoadOp operation) {
            EXPECT_TRUE(operation.getType().isInteger(bits));
        });
    }
}
} // namespace

TEST(NumericTest, ParserPreservesFullNumericSpellingWithoutHostConversion) {
    for (const std::string spelling :
         {"18446744073709551615", "999999999999999999999999999999", "0xFFFFFFFFFFFFFFFF", "1e10000",
          "1e-10000", "1.0000000596046447753906250000000001"}) {
        GloinParser parser{Lexer(spelling)};
        auto expression = parser.parse_expression(0);
        ASSERT_NE(expression, nullptr) << spelling;
        EXPECT_FALSE(parser.has_error());
        EXPECT_EQ(expression->to_string(), spelling);
        EXPECT_TRUE(parser.at_end());
    }
}

TEST(NumericTest, EveryIntegerWidthAcceptsItsExactBoundaries) {
    for (const auto &type : core_types) {
        if (!type.is_integer)
            continue;
        uint64_t maximum = type.is_signed    ? (uint64_t{1} << (type.bits - 1)) - 1
                           : type.bits == 64 ? std::numeric_limits<uint64_t>::max()
                                             : (uint64_t{1} << type.bits) - 1;
        integer_value(std::string(type.name), std::to_string(maximum), type.bits, maximum);
        integer_value(std::string(type.name), "0", type.bits, 0);
        if (type.is_signed) {
            uint64_t magnitude = uint64_t{1} << (type.bits - 1);
            integer_value(std::string(type.name), "-" + std::to_string(magnitude), type.bits,
                          magnitude);
        }
    }
    integer_value("int", "2147483647", 32, 2147483647);
    integer_value("usize", "18446744073709551615", 64, std::numeric_limits<uint64_t>::max());
}

TEST(NumericTest, EveryIntegerWidthRejectsAdjacentOutOfRangeValues) {
    for (const auto &type : core_types) {
        if (!type.is_integer)
            continue;
        std::string upper =
            type.bits == 64 && !type.is_signed
                ? "18446744073709551616"
                : std::to_string(uint64_t{1} << (type.bits - (type.is_signed ? 1 : 0)));
        for (const std::string binding : {"mut ", "const "}) {
            rejects("def f() -> void { def " + binding + "x: " + std::string(type.name) + " = " +
                        upper + "; }",
                    "out of range");
            if (type.is_signed) {
                auto lower = "-" + std::to_string((uint64_t{1} << (type.bits - 1)) + 1);
                rejects("def f() -> void { def " + binding + "x: " + std::string(type.name) +
                            " = " + lower + "; }",
                        "out of range");
            } else {
                for (const std::string negative : {"-1", "-0"})
                    rejects("def f() -> void { def " + binding + "x: " + std::string(type.name) +
                                " = " + negative + "; }",
                            "unsigned type");
            }
        }
    }
}

TEST(NumericTest, IntegerBasesHaveIdenticalMagnitudeAndRangeRules) {
    for (const std::string spelling : {"42", "042", "0x2A", "0X2a", "0b101010", "0B101010"})
        integer_value("u8", spelling, 8, 42);
    integer_value("u64", "0xffffffffffffffff", 64, std::numeric_limits<uint64_t>::max());
    integer_value("u64", "0b" + std::string(64, '1'), 64, std::numeric_limits<uint64_t>::max());
    integer_value("i64", "-0x8000000000000000", 64, uint64_t{1} << 63);
    integer_value("i8", "-0b10000000", 8, 128);
    for (const std::string value : {"0x100", "0b100000000"})
        rejects("def f() -> u8 { return " + value + "; }", "out of range");
    for (const std::string value :
         std::vector<std::string>{"0x10000000000000000", "0b" + std::string(65, '1')})
        rejects("def const X: u64 = " + value + ";", "out of range");
}

TEST(NumericTest, DirectAstLiteralConversionsRequireFullConsumption) {
    for (const std::string spelling : {"", "0x", "12oops", "0b102", "+1", "-1", "1_000", "1 "}) {
        std::string error;
        EXPECT_FALSE(parse_numeric_literal(spelling, false, CoreType::U64, false, error))
            << spelling;
        EXPECT_FALSE(error.empty());
    }
    for (const std::string spelling :
         {"", "1.", ".5", "1e", "1.0oops", "nan", "inf", "0x1p2", "+1.0", "1.0 "}) {
        std::string error;
        EXPECT_FALSE(parse_numeric_literal(spelling, true, CoreType::F64, false, error))
            << spelling;
        EXPECT_FALSE(error.empty());
    }
    auto parsed = GloinParser(Lexer("def f() -> i32 { return 1; }")).parse_checked_program();
    auto *function = dynamic_cast<FunctionDefinition *>(parsed.program.front().get());
    auto *statement = dynamic_cast<ReturnStatement *>(function->body->statements.front().get());
    statement->return_value = std::make_unique<IntegerLiteral>("12oops");
    Sema sema;
    EXPECT_EQ(sema.check_for_codegen(std::move(parsed.program)), nullptr);
}

TEST(NumericTest, ContextReachesDeclarationsAssignmentsArgumentsAndReturns) {
    verifies(R"(
        def const LIMIT: u64 = 18446744073709551615;
        def f(value: u8) -> u8 { return value; }
        def g() -> i64 { return -9223372036854775808; }
        def h() -> u8 { def mut x: u8; x = 255; return f(255); }
        def d(x: f64) -> f64 { return x; }
        def e() -> f64 { def mut x: f64 = 1e300; x = 2e300; return d(3e300); }
    )");
    rejects("def f(x: u8) -> u8 { return x; } def g() -> u8 { return f(256); }", "out of range");
    rejects("def f() -> void { def mut x: i8; x = 128; }", "out of range");
    rejects("def f() -> i8 { return 128; }", "out of range");
}

TEST(NumericTest, TypedOperandsAnchorBothSidesAndNestedLiteralArithmetic) {
    verifies(R"(
        def f(x: i64) -> i64 { return (1 + 2) + x; }
        def g(x: i64) -> bool { return 9223372036854775807 == x; }
        def h(x: i8) -> bool { return x == (1 + 2); }
        def a() -> i64 { return (1 + 2) * (3 + 4); }
        def value() -> u64 { return 18446744073709551615; }
        def b() -> bool { return 18446744073709551615 == value(); }
        def const X: f64 = 1e300;
        def const Y: f64 = 2.0 * X;
        def const Z: bool = 1e300 == X;
    )");
    rejects("def f(x: i8) -> i8 { return 128 + x; }", "out of range");
    rejects("def f(x: i32) -> i64 { def y: i64 = x + 1; return y; }", "Type mismatch");
    rejects("def f(x: i32, y: i64) -> i64 { return x + y; }", "Type mismatch");
}

TEST(NumericTest, DefaultLiteralsDoNotInferWiderOrDifferentCategories) {
    verifies("def f() -> void { 2147483647; 1.0; }");
    rejects("def f() -> void { 2147483648; }", "out of range");
    rejects("def f() -> void { 1e100; }", "out of range");
    rejects("def f() -> void { def x: f32 = 1; }", "Integer literal requires");
    rejects("def f() -> void { def x: i32 = 1.0; }", "Floating literal requires");
    rejects("def f() -> void { def x: f64 = 1.0 + 2; }", "Integer literal requires");
    rejects("def f(x: bool) -> void { def y: i8 = x; }", "Type mismatch");
}

TEST(NumericTest, TypedConversionsAndCastSyntaxRemainRejected) {
    for (const std::string source :
         {"def f(x: i8) -> void { def y: i64 = x; }", "def f(x: i64) -> void { def y: i8 = x; }",
          "def f(x: u32) -> void { def y: i32 = x; }", "def f(x: f32) -> void { def y: f64 = x; }"})
        rejects(source, "Type mismatch");
    for (const std::string cast : {"i32(x)", "x as i32", "f64(x)"})
        rejects("def f(x: i64) -> i32 { return " + cast + "; }", "", DiagnosticStage::Parsing);
    rejects("def f() -> i128 { return 1; }", "unsupported core type");
    rejects("def f() -> u128 { return 1; }", "unsupported core type");
    rejects("def f() -> f128 { return 1.0; }", "unsupported core type");
}

TEST(NumericTest, FloatRangeAndSubnormalRulesMatchForVariablesAndConstants) {
    for (const std::string binding : {"mut ", "const "}) {
        for (const auto &[type, value] :
             std::vector<std::pair<std::string, std::string>>{{"f32", "3.4028234663852886e38"},
                                                              {"f32", "1.401298464324817e-45"},
                                                              {"f64", "1.7976931348623157e308"},
                                                              {"f64", "4.9406564584124654e-324"},
                                                              {"f64", "-0.0"}})
            verifies("def f() -> " + type + " { def " + binding + "x: " + type + " = " + value +
                     "; return x; }");
        for (const auto &[type, value] : std::vector<std::pair<std::string, std::string>>{
                 {"f32", "3.5e38"}, {"f32", "1e-100"}, {"f64", "1.8e308"}, {"f64", "1e-10000"}})
            rejects("def f() -> " + type + " { def " + binding + "x: " + type + " = " + value +
                    "; return x; }");
    }
}

TEST(NumericTest, ConstantArithmeticChecksAllIntegerWidthsAndSignedness) {
    for (const auto &type : core_types) {
        if (!type.is_integer)
            continue;
        std::string name(type.name);
        uint64_t maximum = type.is_signed    ? (uint64_t{1} << (type.bits - 1)) - 1
                           : type.bits == 64 ? std::numeric_limits<uint64_t>::max()
                                             : (uint64_t{1} << type.bits) - 1;
        rejects("def const X: " + name + " = " + std::to_string(maximum) + " + 1;",
                "Integer overflow");
        rejects("def const X: " + name + " = " + std::to_string(maximum) + " * 2;",
                "Integer overflow");
        rejects("def const X: " + name + " = 1 / 0;", "by zero");
        if (!type.is_signed)
            rejects("def const X: " + name + " = 0 - 1;", "Integer overflow");
        else {
            std::string minimum = "-" + std::to_string(uint64_t{1} << (type.bits - 1));
            rejects("def const X: " + name + " = " + minimum + " - 1;", "Integer overflow");
            rejects("def const X: " + name + " = " + minimum + " / -1;", "Integer overflow");
        }
    }
    verifies(
        "def const BIG: u64 = 18446744073709551615; def const HALF: u64 = BIG / 2; "
        "def const CHECK: bool = BIG > 9223372036854775807; def f() -> bool { return CHECK; }");
    verifies("def const X: f64 = 1e300 / 2.0; def f() -> f64 { return X; }");
    rejects("def const X: f64 = 1e300 * 1e300;", "Non-finite result");
}

TEST(NumericTest, FloatBitPatternsSurviveExternalExecution) {
    struct Case {
        std::string type, literal;
        unsigned bits;
        uint64_t expected;
    };
    for (const auto &test :
         std::vector<Case>{{"f32", "1.000000059604644775390625", 32, 0x3f800000},
                           {"f32", "1.0000000596046447753906250000000001", 32, 0x3f800001},
                           {"f32", "1.401298464324817e-45", 32, 1},
                           {"f32", "-0.0", 32, 0x80000000},
                           {"f64", "1.00000000000000011102230246251565404236316680908203125", 64,
                            0x3ff0000000000000},
                           {"f64", "1.000000000000000111022302462515654042363166809082031251", 64,
                            0x3ff0000000000001},
                           {"f64", "4.9406564584124654e-324", 64, 1},
                           {"f64", "-0.0", 64, 0x8000000000000000}}) {
        SCOPED_TRACE(test.type + " " + test.literal);
        mlir::MLIRContext context;
        auto result =
            compile_source("def value() -> " + test.type + " { return " + test.literal + "; }",
                           "float.gloin", context);
        ASSERT_TRUE(result.success());
        // A test-only IR adapter observes exact returned bits without requiring
        // Gloin's still-pending runtime floating comparisons (SPEC-015).
        mlir::OpBuilder builder(&context);
        builder.setInsertionPointToEnd(result.module->getBody());
        auto location = builder.getUnknownLoc();
        auto main = builder.create<mlir::func::FuncOp>(
            location, "main", builder.getFunctionType({}, {builder.getI32Type()}));
        builder.setInsertionPointToStart(main.addEntryBlock());
        auto value = builder
                         .create<mlir::func::CallOp>(
                             location, result.module->lookupSymbol<mlir::func::FuncOp>("value"),
                             mlir::ValueRange{})
                         .getResult(0);
        auto bits = builder.create<mlir::arith::BitcastOp>(
            location, builder.getIntegerType(test.bits), value);
        auto expected = builder.create<mlir::arith::ConstantOp>(
            location, builder.getIntegerAttr(builder.getIntegerType(test.bits),
                                             llvm::APInt(test.bits, test.expected)));
        auto equal = builder.create<mlir::arith::CmpIOp>(location, mlir::arith::CmpIPredicate::eq,
                                                         bits, expected);
        auto yes = builder.create<mlir::arith::ConstantIntOp>(location, 42, 32);
        auto no = builder.create<mlir::arith::ConstantIntOp>(location, 0, 32);
        auto answer = builder.create<mlir::arith::SelectOp>(location, equal, yes, no);
        builder.create<mlir::func::ReturnOp>(location, answer.getResult());
        ASSERT_TRUE(mlir::succeeded(mlir::verify(*result.module)));
        std::string source;
        llvm::raw_string_ostream stream(source);
        result.module->print(stream);
        auto executed = gloin_test::run_external_mlir(source, {gloin_test::mlir_opt, {}},
                                                      {gloin_test::mlir_runner, {}});
        ASSERT_TRUE(static_cast<bool>(executed)) << llvm::toString(executed.takeError());
        EXPECT_EQ(*executed, 42);
    }
}
