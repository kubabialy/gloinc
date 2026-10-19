#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
namespace {
class MathLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source(R"(
        import "@math"; import "@status"; import "@std"; import "@strings"; import "@arena";
        def close(a: f64, b: f64) -> bool {
            return match math.abs_f64(a-b) {
                case value(distance) => distance < 0.000000000001;
                case error(problem) => false;
            };
        }
        def success<T>(outcome: result<T>, expected: T) -> bool {
            return match outcome {
                case value(found) => found == expected;
                case error(problem) => false;
            };
        }
        def failure<T>(outcome: result<T>, expected: string) -> bool {
            return match outcome {
                case value(found) => false;
                case error(problem) => strings.equal(problem.message, expected);
            };
        }
        def near(outcome: result<f64>, expected: f64) -> bool {
            return match outcome {
                case value(found) => close(found, expected);
                case error(problem) => false;
            };
        }
        def formatted(outcome: result<f64>, memory: &arena.GeneralArena) -> string {
            if outcome.erroneous { return "ERROR"; }
            def text: result<string> = std.format_f64(memory,outcome.value);
            if text.erroneous { return "ERROR"; }
            return text.value;
        }
        def main() -> i32 {
    )" + body + " }");
    }
    std::filesystem::path library() {
        const char *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
};
} // namespace
TEST_F(MathLibraryTest, TypedMinMaxClampAndSignedAbsoluteBoundaries) {
    std::string body;
    for (std::string t : {"i32", "i64", "u64", "f32", "f64"}) {
        const std::string a = t[0] == 'f' ? "2.0" : "2", b = t[0] == 'f' ? "3.0" : "3",
                          c = t[0] == 'f' ? "4.0" : "4";
        body += "if math.min_" + t + "(" + a + "," + b + ") != " + a + " || math.max_" + t + "(" +
                a + "," + b + ") != " + b + " { return 1; }";
        body += "if !success<" + t + ">(math.clamp_" + t + "(" + a + "," + b + "," + c + ")," + b +
                ") { return 2; }";
        body += "if !success<" + t + ">(math.clamp_" + t + "(" + c + "," + a + "," + b + ")," + b +
                ") || !success<" + t + ">(math.clamp_" + t + "(" + b + "," + a + "," + c + ")," +
                b + ") { return 3; }";
        body += "if !failure<" + t + ">(math.clamp_" + t + "(" + a + "," + c + "," + b +
                "),\"math clamp bounds are reversed\") { return 4; }";
    }
    body += R"(
        if !failure<i32>(math.abs_i32(-2147483648), "math absolute value overflow") { return 5; }
        if !failure<i64>(math.abs_i64(-9223372036854775808), "math absolute value overflow") { return 6; }
        if !success<i32>(math.abs_i32(-2147483647),2147483647)
            || !success<i64>(math.abs_i64(-9223372036854775807),9223372036854775807) { return 7; }
        if math.min_u64(18446744073709551615,0) != 0 || math.max_u64(18446744073709551615,0) != 18446744073709551615 { return 8; }
        return 0;)";
    expect_run(invoke({program(body)}), 0);
}
TEST_F(MathLibraryTest, UnaryNegationOfValuesWorksInTypedInitializers) {
    expect_run(invoke({program(R"(
        def some_val: int = 42; def foo: int = -some_val;
        def restored: int = -foo;
        def big: i64 = 9000000000; def negative: i64 = -big;
        def number: f64 = 1.25; def flipped: f64 = -number;
        def small: f32 = 2.5; def flipped_small: f32 = -small;
        if foo != -42 || restored != 42 || negative != -9000000000 { return 1; }
        if flipped != -1.25 || flipped_small != -2.5 || !success<i32>(math.abs_i32(foo),some_val) { return 2; }
        return 0;)")}),
               0);
}
TEST_F(MathLibraryTest, EveryFloatingFunctionExecutesAtBothWidths) {
    const std::pair<const char *, const char *> calls[] = {
        {"abs(-2.0)", "2.0"},      {"floor(-1.25)", "-2.0"},  {"ceil(-1.25)", "-1.0"},
        {"trunc(-1.75)", "-1.0"},  {"round(-2.5)", "-3.0"},   {"sqrt(4.0)", "2.0"},
        {"exp(0.0)", "1.0"},       {"log(1.0)", "0.0"},       {"log10(100.0)", "2.0"},
        {"sin(0.0)", "0.0"},       {"cos(0.0)", "1.0"},       {"tan(0.0)", "0.0"},
        {"pow(-2.0,3.0)", "-8.0"}, {"atan2(0.0,1.0)", "0.0"}, {"hypot(3.0,4.0)", "5.0"}};
    for (std::string t : {"f32", "f64"}) {
        std::string body;
        for (auto [call, want] : calls) {
            std::string invocation = call;
            invocation.insert(invocation.find('('), "_" + t);
            body += "if !success<" + t + ">(math." + invocation + "," + want + ") { return 1; }";
        }
        expect_run(invoke({program(body + "return 0;")}), 0);
    }
}
TEST_F(MathLibraryTest, FloatingDomainsOverflowAndUnderflowAreRecoverable) {
    for (std::string t : {"f32", "f64"}) {
        std::string body;
        for (std::string call :
             {"sqrt(-1.0)", "log(0.0)", "log10(-1.0)", "pow(-2.0,0.5)", "pow(0.0,-1.0)"}) {
            call.insert(call.find('('), "_" + t);
            body += "if !failure<" + t + ">(math." + call + ",\"math domain error\") { return 1; }";
        }
        body += "if !failure<" + t + ">(math.exp_" + t +
                "(10000.0),\"math result overflow\") || "
                "!failure<" +
                t + ">(math.exp_" + t + "(-10000.0),\"math result underflow\") { return 2; }";
        body += "if !success<" + t + ">(math.pow_" + t + "(0.0,0.0),1.0) { return 3; } return 0;";
        expect_run(invoke({program(body)}), 0);
    }
}
TEST_F(MathLibraryTest, SignedZeroSelectionRoundingAndFloatConstants) {
    expect_run(invoke({program(R"(
        def mut memory: arena.GeneralArena = arena.GeneralArena.create(); defer memory.free();
        def parsed: result<f64> = std.parse_f64("-0"); if parsed.erroneous { return 1; } def z: f64 = parsed.value;
        def min: result<string> = std.format_f64(&memory,math.min_f64(z,0.0)); if min.erroneous { return 1; } if !strings.equal(min.value,"-0") { return 1; }
        def max: result<string> = std.format_f64(&memory,math.max_f64(0.0,z)); if max.erroneous { return 2; } if !strings.equal(max.value,"0") { return 2; }
        if !strings.equal(formatted(math.clamp_f64(z,0.0,1.0),&memory),"-0") { return 3; }
        if !strings.equal(formatted(math.sqrt_f64(z),&memory),"-0") { return 4; }
        if !strings.equal(formatted(math.abs_f64(z),&memory),"0") { return 5; }
        if !strings.equal(formatted(math.round_f64(-0.25),&memory),"-0") { return 6; }
        if !success<f32>(math.round_f32(2.5),3.0) || !success<f64>(math.round_f64(-2.5),-3.0) { return 7; }
        def pi: f32 = math.PI_F32; def tau: f32 = math.TAU_F32;
        if pi < 3.14159 || pi > 3.14160 || tau != pi * 2.0 { return 8; }
        if !close(math.PI_F64,3.141592653589793) || math.TAU_F64 != math.PI_F64 * 2.0 { return 9; }
        if !near(math.atan2_f64(z,z),-math.PI_F64) { return 10; }
        return 0;)")}),
               0);
}
TEST_F(MathLibraryTest, IndependentGeometryAndStatisticsReferences) {
    expect_run(invoke({program(R"(
        if !near(math.sqrt_f64(2.0),1.4142135623730951) || !near(math.log_f64(2.0),0.6931471805599453) { return 1; }
        if !near(math.exp_f64(1.0),2.718281828459045) || !near(math.sin_f64(1.0),0.8414709848078965) { return 2; }
        if !near(math.cos_f64(1.0),0.5403023058681397) || !near(math.tan_f64(1.0),1.5574077246549023) { return 3; }
        def mut i: i32 = -100;
        while i <= 100 {
            def x: f64 = std.f64_from_i64_rounded(std.i64_from_i32(i)) / 16.0;
            def sine: result<f64> = math.sin_f64(x);
            if sine.erroneous { return 4; }
            def cosine: result<f64> = math.cos_f64(x);
            if cosine.erroneous { return 4; }
            def s: f64 = sine.value; def c: f64 = cosine.value;
            if !close(s*s+c*c,1.0) { return 4; }
            i = i + 1;
        }
        def parsed: result<f64> = std.parse_f64("1e300"); if parsed.erroneous { return 5; } def huge: f64 = parsed.value;
        def length: result<f64> = math.hypot_f64(huge,huge);
        if length.erroneous { return 5; }
        if !close(length.value/huge,1.4142135623730951) { return 5; }
        def small32: result<f32> = math.pow_f32(2.0,-149.0);
        if small32.erroneous { return 6; }
        if small32.value == 0.0 { return 6; }
        def small64: result<f64> = math.pow_f64(2.0,-1074.0);
        if small64.erroneous { return 7; }
        if small64.value == 0.0 { return 7; }
        return 0;)")}),
               0);
}
TEST_F(MathLibraryTest, PublicSignaturesAndPrivatePrimitivesRejectInvalidUse) {
    for (std::string body :
         {"math.sqrt_f64(1);", "def x: f32 = 1.0; math.sin_f64(x);", "math.clamp_i32(1,2);",
          "math.pow_f32(true,1.0);", "def x: f32 = math.PI_F64;", "__math_unary_f64(5,4.0);",
          "math.__math_unary_f64(5,4.0);"}) {
        // Both integer literals and integer variables require explicit conversion.
        if (body == "math.sqrt_f64(1);")
            body = "def x: i32 = 1; math.sqrt_f64(x);";
        expect_error(invoke({"--check", program(body + "return 0;")}), 1, "error:");
    }
    auto root = source("import \"@math\"; def main() -> i32 { math.probe(); return 0; }");
    for (std::string body : {"def mut v: f64 = 0.0; __math_unary_f64(5,4.0,&v,0);",
                             "def mut v: f32 = 0.0; __math_unary_f64(5,4.0,&v);",
                             "def mut v: f64 = 0.0; __math_binary_f64(0,2.0,&v);",
                             "def mut v: f32 = 0.0; __math_binary_f32(true,2.0,3.0,&v);"}) {
        source("def pub probe() -> void {" + body + "}", "math.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", root}), 1, "error:");
    }
    source("def pub probe() -> void { def mut v: f64 = 0.0; __math_unary_f64(5,4.0,&v); }",
           "fs.gloin");
    root = source("import \"@fs\"; def main() -> i32 { fs.probe(); return 0; }");
    expect_error(invoke({"--stdlib-dir", directory, "--check", root}), 1, "error:");
}

TEST_F(MathLibraryTest, CheckedPublicResultsCannotBeIgnoredOrReadWithoutProof) {
    for (std::string body :
         {"math.sqrt_f64(4.0);", "def r: result<f64> = math.sqrt_f64(4.0); return 0;",
          "def r: result<f64> = math.sqrt_f64(4.0); if r.value == 2.0 { return 0; }",
          "def r: result<f64> = math.sqrt_f64(-1.0); if r.erroneous { def x: f64 = r.value; }",
          "def r: result<f64> = math.sqrt_f64(4.0); if r.erroneous { return 1; } "
          "std.println(r.error.message);",
          "def r: math.F64Result = math.sqrt_f64(4.0);"}) {
        expect_error(invoke({"--check", program(body + "return 0;")}), 1, "error:");
    }
}

TEST_F(MathLibraryTest, MigratedResultsKeepGloinIrTypesAndExecuteInNativeModes) {
    auto file = program(R"(
        if !success<i32>(math.clamp_i32(42,0,100),42) { return 1; }
        if !success<f64>(math.sqrt_f64(4.0),2.0) { return 2; }
        if !failure<f64>(math.log_f64(0.0),"math domain error") { return 3; }
        if !failure<f32>(math.exp_f32(10000.0),"math result overflow") { return 4; }
        if !failure<f64>(math.exp_f64(-10000.0),"math result underflow") { return 5; }
        def payload: result<f64> = math.hypot_f64(3.0,4.0);
        if payload.erroneous { return 6; }
        if payload.value != 5.0 || 4.0 >= payload.value { return 7; }
        return 42;
    )");
    auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation : {"!gloin.result<f64>", "!gloin.error", "gloin.result_success",
                                  "gloin.result_failure", "gloin.error_literal", "gloin.match"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({file}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/math-results";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        std::string message;
        bool failed = false;
        const auto code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10,
                                                    0, &message, &failed);
        EXPECT_FALSE(failed) << message;
        EXPECT_EQ(code, 42) << message;
    }
}
TEST_F(MathLibraryTest, PublicSourceIsReplaceableAndInvalidPrivateSelectorsAreChecked) {
    auto root = source("import \"@math\"; def main() -> i32 { return math.probe(); }");
    source("def pub probe() -> i32 { def mut v: f64 = 99.0; def c: i32 = "
           "__math_unary_f64(999,4.0,&v); if c != 2 || v != 0.0 { return 1; } return 42; }",
           "math.gloin");
    expect_run(invoke({"--stdlib-dir", directory, root}), 42);
    source("def pub probe() -> i32 { return 43; }", "math.gloin");
    expect_run(invoke({"--stdlib-dir", directory, root}), 43);
    // Ordinary math needs only status.gloin, no arena/std or injected implementation.
    source(read((library() / "math.gloin").string()), "math.gloin");
    source(read((library() / "status.gloin").string()), "status.gloin");
    root = source(R"(import "@math"; def main() -> i32 {
        def value: result<f64> = math.hypot_f64(3.0,4.0);
        if value.erroneous { return 1; }
        if value.value == 5.0 { return 42; } return 1; })");
    expect_run(invoke({"--stdlib-dir", directory, root}), 42);
}
TEST_F(MathLibraryTest, NativeNamesCannotCollideAndMalformedAbisFailBeforeExecution) {
    expect_run(invoke({source(R"(import "@math"; def gloin_math_unary_f64() -> i32 { return 42; }
        def main() -> i32 { def value: result<f64> = math.sqrt_f64(4.0); if value.erroneous { return 1; } if value.value != 2.0 { return 1; } return gloin_math_unary_f64(); })")}),
               42);
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (std::string decl :
         {"llvm.func @gloin_math_unary_f64(i32, f32, !llvm.ptr) -> i32",
          "llvm.func @gloin_math_unary_f32(i32, f32, !llvm.ptr) -> i64",
          "llvm.func @gloin_math_binary_f64(i32, f64, !llvm.ptr) -> i32",
          "llvm.func @gloin_math_binary_f32(i32, f32, f32, !llvm.ptr, ...) -> i32"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + decl +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}
TEST_F(MathLibraryTest, ExternalLlvmExecutionUsesAllFourTypedMathAbis) {
    auto ir = invoke({"--emit-llvm", program(R"(
        if !success<f32>(math.sqrt_f32(4.0),2.0) || !success<f64>(math.sqrt_f64(9.0),3.0) { return 1; }
        if !success<f32>(math.hypot_f32(3.0,4.0),5.0) || !success<f64>(math.pow_f64(2.0,3.0),8.0) { return 2; }
        if !failure<f64>(math.log_f64(0.0),"math domain error") { return 3; } return 42;)")});
    ASSERT_EQ(ir.status, 0) << ir.err;
    const char *runtime = std::getenv("GLOIN_TEST_ARENA_RUNTIME");
    gloin_test::ToolCommand runner{
        gloin_test::mlir_runner,
        {std::string("--shared-libs=") + (runtime ? runtime : gloin_test::arena_runtime)}};
#ifdef __APPLE__
    if (auto *asan = dlsym(RTLD_DEFAULT, "__asan_init")) {
        Dl_info info{};
        ASSERT_NE(dladdr(asan, &info), 0);
        std::string preload = info.dli_fname;
        if (const char *p = std::getenv("DYLD_INSERT_LIBRARIES"); p && *p)
            preload += std::string(":") + p;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
TEST_F(MathLibraryTest, GeometryStatisticsExampleAndInvalidInputExecuteFromPackages) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "math_lab.gloin";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example =
            std::filesystem::path(binary).parent_path() / "../share/gloinc/examples/math_lab.gloin";
    expect_run(invoke({example.string()}, source("3,4\n0,0\n6,8\n", "points.txt")), 0,
               "count=3\nmin=0.000\nmax=10.000\nmean=5.000\nstddev=4.082\nlast_angle=0.927\n");
    expect_error(invoke({example.string()}, source("1,nope\n", "bad.txt")), 2, "invalid point");
    expect_error(invoke({example.string()}, source("", "empty.txt")), 2, "expected x,y records");
    expect_error(invoke({example.string()}, source("1000001,0\n", "large.txt")), 2,
                 "invalid point");
}
TEST_F(MathLibraryTest, GuideProgramsExecuteVerbatim) {
    auto guide = std::filesystem::path(gloin_test::strings_guide).parent_path() / "math.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        guide = std::filesystem::path(binary).parent_path() / "../share/doc/gloinc/docs/math.md";
    auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(doc.substr(pos, end - pos))}), 0,
                   blocks == 0 ? "length=5\n" : "negation and rounding: ok\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}
