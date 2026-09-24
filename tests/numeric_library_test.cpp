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
class NumericLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source(
            "import \"@std\"; import \"@status\"; import \"@arena\"; import \"@strings\"; "
            "def same(r: std.FormatResult, s: string) -> bool { return r.status == status.OK && "
            "strings.equal(r.value,s); } "
            "def main() -> i32 { def mut memory: arena.GeneralArena = arena.GeneralArena.create(); "
            "defer memory.free(); " +
            body + " }");
    }
    std::filesystem::path library() {
        const char *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
};
} // namespace
TEST_F(NumericLibraryTest, PublicParsersReturnConcreteTypedResults) {
    expect_run(invoke({program(R"(
        def a: std.IntResult = std.parse_i32("-2147483648");
        def b: std.I64Result = std.parse_i64("-9223372036854775808");
        def c: std.U64Result = std.parse_u64("+18446744073709551615");
        def d: std.F32Result = std.parse_f32(".125e1");
        def e: std.F64Result = std.parse_f64("+1.25E+0");
        def f: std.BoolResult = std.parse_bool("true");
        if a.status != status.OK || a.value != -2147483648 { return 1; }
        if b.status != status.OK || b.value != -9223372036854775808 { return 2; }
        if c.status != status.OK || c.value != 18446744073709551615 { return 3; }
        if d.status != status.OK || d.value != 1.25 || e.status != status.OK || e.value != 1.25 { return 4; }
        if f.status != status.OK || !f.value || std.parse_bool("false").value { return 5; }
        if std.INEXACT != status.INEXACT || std.UNDERFLOW != status.UNDERFLOW || status.INEXACT != 8 || status.UNDERFLOW != 9 { return 6; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, ParseFailuresHaveNoPartialPayloadAndLegacyNamesRemain) {
    expect_run(invoke({program(R"(
        def a: std.I64Result = std.parse_i64("999999999999999999999x");
        def b: std.U64Result = std.parse_u64("-0");
        def c: std.F32Result = std.parse_f32("1e999");
        def d: std.F64Result = std.parse_f64("-1e-999");
        def e: std.BoolResult = std.parse_bool("TRUE");
        if a.status != status.INVALID || a.value != 0 || b.status != status.INVALID || b.value != 0 { return 1; }
        if c.status != status.OVERFLOW || c.value != 0.0 || d.status != status.UNDERFLOW || d.value != 0.0 { return 2; }
        if e.status != status.INVALID || e.value || std.parse_f64("1\0").status != status.INVALID { return 3; }
        if std.to_int("42").value != std.parse_i32("42").value || !strings.equal(std.to_string(&memory,-42),"-42") { return 4; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, EveryFormatterAndSignedZeroRoundTrip) {
    expect_run(invoke({program(R"(
        if !same(std.format_i32(&memory,-2147483648),"-2147483648") { return 1; }
        if !same(std.format_i64(&memory,-9223372036854775808),"-9223372036854775808") { return 2; }
        if !same(std.format_u64(&memory,18446744073709551615),"18446744073709551615") { return 3; }
        if !same(std.format_f32(&memory,1.25),"1.25") || !same(std.format_f64(&memory,1.25),"1.25") { return 4; }
        if !same(std.format_f32_fixed(&memory,2.5,0),"2") || !same(std.format_f64_fixed(&memory,3.5,0),"4") { return 5; }
        if !same(std.format_f64_fixed(&memory,-0.125,2),"-0.12") { return 6; }
        def z: std.F64Result = std.parse_f64("-0e99999");
        def small: std.F32Result = std.f32_from_f64_exact(z.value);
        if !same(std.format_f64(&memory,z.value),"-0") || !same(std.format_f32_fixed(&memory,small.value,2),"-0.00") { return 7; }
        if !strings.equal(std.format_bool(true),"true") || !strings.equal(std.format_bool(false),"false") { return 8; }
        def saved: std.FormatResult = std.format_f64_fixed(&memory,1.25,18);
        def ignored: std.FormatResult = std.format_f64(&memory,99.0);
        if !same(saved,"1.250000000000000000") { return 9; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, AllFifteenNamedConversionsExecuteWithTheirDeclaredTypes) {
    expect_run(invoke({program(R"(
        if std.i64_from_i32(-42).value != -42 || std.i32_from_i64(42).value != 42 { return 1; }
        if std.u64_from_i64(42).value != 42 || std.i64_from_u64(42).value != 42 { return 2; }
        if std.f64_from_i64_exact(42).value != 42.0 || std.f64_from_i64_rounded(42).value != 42.0 { return 3; }
        if std.f64_from_u64_exact(42).value != 42.0 || std.f64_from_u64_rounded(42).value != 42.0 { return 4; }
        if std.i64_from_f64_exact(42.0).value != 42 || std.i64_from_f64_trunc(-42.9).value != -42 { return 5; }
        if std.u64_from_f64_exact(42.0).value != 42 || std.u64_from_f64_trunc(42.9).value != 42 { return 6; }
        if std.f64_from_f32(1.25).value != 1.25 || std.f32_from_f64_exact(1.25).value != 1.25 || std.f32_from_f64_rounded(1.25).value != 1.25 { return 7; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, ConversionFailuresAndExplicitRoundingChoices) {
    expect_run(invoke({program(R"(
        if std.i32_from_i64(2147483648).status != status.OVERFLOW || std.u64_from_i64(-1).status != status.OVERFLOW || std.i64_from_u64(18446744073709551615).status != status.OVERFLOW { return 1; }
        def exact: std.F64Result = std.f64_from_i64_exact(9007199254740993);
        def rounded: std.F64Result = std.f64_from_i64_rounded(9007199254740993);
        if exact.status != status.INEXACT || exact.value != 0.0 || rounded.status != status.OK || rounded.value != 9007199254740992.0 { return 2; }
        if std.i64_from_f64_exact(1.5).status != status.INEXACT || std.i64_from_f64_exact(9223372036854775808.0).status != status.OVERFLOW { return 3; }
        if std.u64_from_f64_trunc(-0.5).status != status.OK || std.u64_from_f64_exact(-0.5).status != status.INEXACT || std.u64_from_f64_exact(-1.5).status != status.OVERFLOW { return 4; }
        if std.f32_from_f64_exact(0.1).status != status.INEXACT || std.f32_from_f64_rounded(0.1).status != status.OK { return 5; }
        def tiny: std.F64Result = std.parse_f64("1e-300");
        def huge: std.F64Result = std.parse_f64("1e300");
        if std.f32_from_f64_rounded(tiny.value).status != status.UNDERFLOW || std.f32_from_f64_rounded(huge.value).status != status.OVERFLOW { return 6; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, AllocationFailureAndInvalidPrecisionAreRecoverable) {
    // Retain actual source APIs; deterministically fail all byte allocations.
    for (auto name : {"std.gloin", "status.gloin", "strings.gloin", "arena.gloin"})
        source(read((library() / name).string()), name);
    auto arena = read((library() / "arena.gloin").string());
    const std::string call = "__arena_general_alloc(self.state, size, 1)";
    size_t pos = 0;
    while ((pos = arena.find(call, pos)) != std::string::npos) {
        arena.replace(pos, call.size(), "null");
        pos += 4;
    }
    source(arena, "arena.gloin");
    std::string body;
    for (std::string call :
         {"format_i32(&memory,1)", "format_i64(&memory,1)", "format_u64(&memory,1)",
          "format_f32(&memory,1.0)", "format_f64(&memory,1.0)", "format_f32_fixed(&memory,1.0,18)",
          "format_f64_fixed(&memory,1.0,18)"})
        body += "{ def r: std.FormatResult = std." + call +
                "; if r.status != status.NO_MEMORY || !strings.is_empty(r.value) { return 1; } }";
    body += R"(memory.free();
        if std.format_f32_fixed(&memory,1.0,19).status != status.INVALID || std.format_f64_fixed(&memory,1.0,4294967295).status != status.INVALID { return 2; }
        if std.parse_f64("1.25").value != 1.25 || std.f64_from_i64_exact(42).value != 42.0 || !strings.equal(std.format_bool(true),"true") { return 3; }
        return 0;)";
    expect_run(invoke({"--stdlib-dir", directory, program(body)}), 0);
}
TEST_F(NumericLibraryTest, NamedConversionsDoNotEnableImplicitCastsAndSignaturesAreChecked) {
    for (const std::string body :
         {"std.parse_f64(1);", "std.parse_bool(true);", "std.format_bool(1);",
          "def n: i64 = 42; std.format_i32(&memory,n);",
          "def n: f64 = 1.0; std.format_f32(&memory,n);",
          "def p: i32 = 2; std.format_f64_fixed(&memory,1.0,p);",
          "def n: i64 = 42; def f: f64 = n;", "def n: f64 = 1.0; def f: f32 = n;",
          "def n: i64 = 42; std.i64_from_i32(n);", "def n: f32 = 1.0; std.i64_from_f64_exact(n);",
          "std.__std_parse_f64(\"1\");", "__std_parse_f64(\"1\");"}) {
        const auto file = program(body + "return 0;");
        for (std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}
TEST_F(NumericLibraryTest, PrivateNumericPrimitivesValidateReferencesAndModuleIdentity) {
    const auto file = source("import \"@std\"; def main() -> i32 { std.probe(); return 0; }");
    for (std::string body : {"def mut v: f32 = 0.0; __std_parse_f64(\"1\",&v);",
                             "def v: f64 = 0.0; __std_parse_f64(\"1\",&v);",
                             "def mut v: bool = false; __std_parse_bool(\"true\",&v);",
                             "def mut v: i64 = 0; __std_i64_from_f64(1.0,&v);",
                             "def mut v: i64 = 0; __std_i64_from_f64(1.0,true,&v);",
                             "def mut v: u64 = 0; __std_format_f64(1.0,null,&v);"}) {
        source("def pub probe() -> void {" + body + "}", "std.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "error:");
    }
    source("def __std_parse_f64() -> void {}", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "Cannot redeclare");
    source("def pub probe() -> void { def mut v: f64 = 0.0; __std_parse_f64(\"1\",&v); }",
           "strings.gloin");
    auto other = source("import \"@strings\"; def main() -> i32 { strings.probe(); return 0; }",
                        "other.gloin");
    expect_error(invoke({"--stdlib-dir", directory, "--check", other}), 1, "Undefined variable");
}
TEST_F(NumericLibraryTest, RuntimeSymbolsDoNotCollideWithUserFunctions) {
    auto file = source(R"(import "@std";
        def gloin_std_parse_f64() -> i32 { return 41; }
        def gloin_std_f64_from_i64() -> i32 { return 1; }
        def main() -> i32 {
            def parsed: std.F64Result = std.parse_f64("42");
            def number: std.F64Result = std.f64_from_i64_exact(42);
            if parsed.value != number.value { return 1; }
            return gloin_std_parse_f64() + gloin_std_f64_from_i64();
        })");
    expect_run(invoke({file}), 42);
}
TEST_F(NumericLibraryTest, FloatAndIntegerAbiExecuteWithExternalRuntime) {
    auto ir = invoke({"--emit-llvm", program(R"(
        def parsed: std.F32Result = std.parse_f32("1.25");
        def wide: std.F64Result = std.f64_from_f32(parsed.value);
        if !same(std.format_f64_fixed(&memory,wide.value,2),"1.25") { return 1; }
        def n: std.I64Result = std.i64_from_f64_trunc(wide.value);
        def u: std.U64Result = std.parse_u64("18446744073709551615");
        if !same(std.format_u64(&memory,u.value),"18446744073709551615") || !std.parse_bool("true").value { return 2; }
        return std.i32_from_i64(n.value).value + 41;)")});
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
        if (const auto *existing = std::getenv("DYLD_INSERT_LIBRARIES"); existing && *existing)
            preload += std::string(":") + existing;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
TEST_F(NumericLibraryTest, PackagedExampleAndGuideProgramsRunAsWritten) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "numbers_lab.gloin";
    auto guide = std::filesystem::path(gloin_test::strings_guide).parent_path() / "numbers.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI")) {
        auto root = std::filesystem::path(binary).parent_path();
        example = root / "../share/gloinc/examples/numbers_lab.gloin";
        guide = root / "../share/doc/gloinc/docs/numbers.md";
    }
    expect_run(invoke({example.string()}, source("1.25\n2.75\n3.5\n", "input.txt")), 0,
               "count=3\nmean=2.50\n");
    expect_run(invoke({example.string()}, source("", "empty.txt")), 0, "count=0\n");
    expect_run(invoke({example.string()}, source("1.25\ninvalid\n", "bad.txt")), 2,
               "invalid number\n");
    const auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(doc.substr(pos, end - pos))}), 0,
                   blocks == 0 ? "1.25\n" : "42\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}

TEST_F(NumericLibraryTest, JitRejectsIncorrectNumericRuntimeAbis) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const std::string declaration :
         {"llvm.func @gloin_std_parse_f64(!llvm.ptr, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_parse_bool(!llvm.ptr, i64, !llvm.ptr, ...) -> i32",
          "llvm.func @gloin_std_format_f32(f64, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_format_f64_fixed(f64, i64, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_i64_from_i32(i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_f64_from_i64(i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_i64_from_f64(f32, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_std_f32_from_f64(f64, i32, !llvm.ptr) -> f32",
          "llvm.func @gloin_std_parse_f64(%a: !llvm.ptr, %b: i64, %c: !llvm.ptr) -> i32 { %v = "
          "llvm.mlir.constant(0 : i32) : i32 llvm.return %v : i32 }"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + declaration +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}
