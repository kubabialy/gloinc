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
            R"(def same(r: result<string>, s: string) -> bool {
                if r.erroneous { return false; }
                return strings.equal(r.value,s);
            }
            def succeeds<T>(r: result<T>, expected: T) -> bool {
                if r.erroneous { return false; }
                return r.value == expected;
            }
            def fails<T>(r: result<T>, code: i32) -> bool {
                if r.erroneous { return r.error.code == code && r.error.os_error == 0; }
                return false;
            })"
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
    expect_run(invoke({program(R"(if !succeeds<i32>(std.parse_i32("-2147483648"), -2147483648)
    || !succeeds<i64>(std.parse_i64("-9223372036854775808"), -9223372036854775808)
    || !succeeds<u64>(std.parse_u64("+18446744073709551615"), 18446744073709551615)
    || !succeeds<f32>(std.parse_f32(".125e1"), 1.25)
    || !succeeds<f64>(std.parse_f64("+1.25E+0"), 1.25)
    || !succeeds<bool>(std.parse_bool("true"), true)
    || !succeeds<bool>(std.parse_bool("false"), false) { return 1; }
if std.INEXACT != status.INEXACT || std.UNDERFLOW != status.UNDERFLOW
    || status.INEXACT != 8 || status.UNDERFLOW != 9 { return 2; }
return 0;
)")}),
               0);
}
TEST_F(NumericLibraryTest, ParseFailuresHaveNoPartialPayloadAndLegacyNamesRemain) {
    expect_run(
        invoke({program(R"(if !fails<i64>(std.parse_i64("999999999999999999999x"), status.INVALID)
    || !fails<u64>(std.parse_u64("-0"), status.INVALID)
    || !fails<f32>(std.parse_f32("1e999"), status.OVERFLOW)
    || !fails<f64>(std.parse_f64("-1e-999"), status.UNDERFLOW)
    || !fails<bool>(std.parse_bool("TRUE"), status.INVALID)
    || !fails<f64>(std.parse_f64("1\0"), status.INVALID) { return 1; }
if !succeeds<i32>(std.to_int("42"),42) || !succeeds<i32>(std.parse_i32("42"),42)
    || !same(std.to_string(&memory,-42),"-42") { return 2; }
return 0;
)")}),
        0);
}
TEST_F(NumericLibraryTest, EveryFormatterAndSignedZeroRoundTrip) {
    expect_run(invoke({program(R"(
        if !same(std.format_i32(&memory,-2147483648),"-2147483648") { return 1; }
        if !same(std.format_i64(&memory,-9223372036854775808),"-9223372036854775808") { return 2; }
        if !same(std.format_u64(&memory,18446744073709551615),"18446744073709551615") { return 3; }
        def port: u16 = 30000;
        def largest: u16 = 65535;
        if !same(std.format_u16(&memory,port),"30000") ||
        !same(std.format_u16(&memory,largest),"65535") { return 10; }
        def small_signed: i8 = -128;
        def signed16: i16 = -32768;
        def unsigned8: u8 = 255;
        def unsigned32: u32 = 4294967295;
        if !same(std.format_i8(&memory,small_signed),"-128") { return 13; }
        if !same(std.format_i16(&memory,signed16),"-32768") { return 18; }
        if !same(std.format_u8(&memory,unsigned8),"255") { return 19; }
        if !same(std.format_u32(&memory,unsigned32),"4294967295") { return 20; }
        def port_text: result<string> = std.to_string_u16(&memory, port);
        if port_text.erroneous { return 11; }
        if !strings.equal(port_text.value,"30000") { return 12; }
        def text8: result<string> = std.to_string_i8(&memory,small_signed);
        if text8.erroneous { return 14; }
        if !strings.equal(text8.value,"-128") { return 15; }
        def text32: result<string> = std.to_string_u32(&memory,unsigned32);
        if text32.erroneous { return 16; }
        if !strings.equal(text32.value,"4294967295") { return 17; }
        if !same(std.format_f32(&memory,1.25),"1.25") || !same(std.format_f64(&memory,1.25),"1.25") { return 4; }
        if !same(std.format_f32_fixed(&memory,2.5,0),"2") || !same(std.format_f64_fixed(&memory,3.5,0),"4") { return 5; }
        if !same(std.format_f64_fixed(&memory,-0.125,2),"-0.12") { return 6; }
        def z: result<f64> = std.parse_f64("-0e99999"); if z.erroneous { return 97; }
        def small: result<f32> = std.f32_from_f64_exact(z.value); if small.erroneous { return 97; }
        if !same(std.format_f64(&memory,z.value),"-0") || !same(std.format_f32_fixed(&memory,small.value,2),"-0.00") { return 7; }
        if !strings.equal(std.format_bool(true),"true") || !strings.equal(std.format_bool(false),"false") { return 8; }
        def saved: result<string> = std.format_f64_fixed(&memory,1.25,18); if saved.erroneous { return 97; }
        def ignored: result<string> = std.format_f64(&memory,99.0); if ignored.erroneous { return 97; }
        if !strings.equal(saved.value,"1.250000000000000000") { return 9; }
        return 0;)")}),
               0);
}
TEST_F(NumericLibraryTest, AllFifteenNamedConversionsExecuteWithTheirDeclaredTypes) {
    expect_run(
        invoke({program(
            R"(if std.i64_from_i32(-42) != -42 || !succeeds<i32>(std.i32_from_i64(42),42) { return 1; }
if !succeeds<u64>(std.u64_from_i64(42),42) || !succeeds<i64>(std.i64_from_u64(42),42) { return 2; }
if !succeeds<f64>(std.f64_from_i64_exact(42),42.0) || std.f64_from_i64_rounded(42) != 42.0 { return 3; }
if !succeeds<f64>(std.f64_from_u64_exact(42),42.0) || std.f64_from_u64_rounded(42) != 42.0 { return 4; }
if !succeeds<i64>(std.i64_from_f64_exact(42.0),42) || !succeeds<i64>(std.i64_from_f64_trunc(-42.9),-42) { return 5; }
if !succeeds<u64>(std.u64_from_f64_exact(42.0),42) || !succeeds<u64>(std.u64_from_f64_trunc(42.9),42) { return 6; }
if !succeeds<f64>(std.f64_from_f32(1.25),1.25) || !succeeds<f32>(std.f32_from_f64_exact(1.25),1.25)
    || !succeeds<f32>(std.f32_from_f64_rounded(1.25),1.25) { return 7; }
return 0;
)")}),
        0);
}
TEST_F(NumericLibraryTest, ExactWideningUsesGIRAndScalarValuesAcrossExecutionModes) {
    const auto file = source(R"(
        import "@std";
        import "@status";
        def main() -> i32 {
            // Parsing keeps inputs dynamic even when LLVM inlines the wrapper.
            def inputs: [string; 6] = {"-2147483648", "-42", "-1", "0", "1", "2147483647"};
            def expected: [i64; 6] = {-2147483648, -42, -1, 0, 1, 2147483647};
            for def mut i: u64 = 0; i < 6; i = i + 1 {
                def parsed: result<i32> = std.parse_i32(inputs[i]);
                if parsed.erroneous { return 1; }
                def wide: i64 = std.i64_from_i32(parsed.value);
                if wide != expected[i] { return 2; }
                def narrow: result<i32> = std.i32_from_i64(wide); if narrow.erroneous { return 3; }
                if narrow.value != parsed.value { return 3; }
            }
            def overflow: result<i32> = std.i32_from_i64(2147483648); if !overflow.erroneous { return 4; }
            if overflow.error.code != status.OVERFLOW { return 4; }
            def inexact: result<f64> = std.f64_from_i64_exact(9007199254740993); if !inexact.erroneous { return 5; } if inexact.error.code != status.INEXACT { return 5; }
            if std.f64_from_i64_rounded(9007199254740993) != 9007199254740992.0 { return 6; }
            return 0;
        }
    )");
    auto high = invoke({"--emit-ir", file});
    ASSERT_EQ(high.status, 0) << high.err;
    EXPECT_NE(high.out.find("gloin.exact_int_widen"), std::string::npos);
    EXPECT_EQ(high.out.find("gloin_std_i64_from_i32"), std::string::npos);
    auto low = invoke({"--emit-llvm", file});
    ASSERT_EQ(low.status, 0) << low.err;
    EXPECT_NE(low.out.find("llvm.sext"), std::string::npos);
    EXPECT_EQ(low.out.find("gloin_std_i64_from_i32"), std::string::npos);
    EXPECT_NE(low.out.find("gloin_std_i32_from_i64"), std::string::npos);
    EXPECT_NE(low.out.find("gloin_std_f64_from_i64"), std::string::npos);
    expect_run(invoke({"--jit", file}), 0);
    for (const std::string mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto executable = directory + "/widen" + mode;
        auto built = invoke_raw({mode, "-o", executable, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 0);
    }
}

TEST_F(NumericLibraryTest, ExactWideningHonorsPrimitiveIdentityAndForwardedOutput) {
    source(R"(def pub convert(value: i32, output: &i64) -> i32 {
        return __std_i64_from_i32(value, output);
    })",
           "std.gloin");
    const auto file = source(R"(import "@std";
        def i64_from_i32(value: i32) -> i64 { return 99; }
        def gloin_std_i64_from_i32() -> i32 { return 42; }
        def main() -> i32 {
            def mut output: i64 = 77;
            if std.convert(-2147483648, &output) != 0 || output != -2147483648 { return 1; }
            if i64_from_i32(1) != 99 { return 2; }
            return gloin_std_i64_from_i32();
        })");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
}

TEST_F(NumericLibraryTest, ConversionFailuresAndExplicitRoundingChoices) {
    expect_run(invoke({program(R"(if !fails<i32>(std.i32_from_i64(2147483648),status.OVERFLOW)
    || !fails<u64>(std.u64_from_i64(-1),status.OVERFLOW)
    || !fails<i64>(std.i64_from_u64(18446744073709551615),status.OVERFLOW) { return 1; }
if !fails<f64>(std.f64_from_i64_exact(9007199254740993),status.INEXACT)
    || std.f64_from_i64_rounded(9007199254740993) != 9007199254740992.0 { return 2; }
if !fails<i64>(std.i64_from_f64_exact(1.5),status.INEXACT)
    || !fails<i64>(std.i64_from_f64_exact(9223372036854775808.0),status.OVERFLOW) { return 3; }
if !succeeds<u64>(std.u64_from_f64_trunc(-0.5),0)
    || !fails<u64>(std.u64_from_f64_exact(-0.5),status.INEXACT)
    || !fails<u64>(std.u64_from_f64_exact(-1.5),status.OVERFLOW) { return 4; }
if !fails<f32>(std.f32_from_f64_exact(0.1),status.INEXACT)
    || !succeeds<f32>(std.f32_from_f64_rounded(0.5),0.5) { return 5; }
def tiny: result<f64> = std.parse_f64("1e-300"); if tiny.erroneous { return 6; }
def huge: result<f64> = std.parse_f64("1e300"); if huge.erroneous { return 6; }
if !fails<f32>(std.f32_from_f64_rounded(tiny.value),status.UNDERFLOW)
    || !fails<f32>(std.f32_from_f64_rounded(huge.value),status.OVERFLOW) { return 6; }
return 0;
)")}),
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
    for (std::string call : {"format_i32(&memory,1)",
                             "format_i64(&memory,1)",
                             "format_u64(&memory,1)",
                             "format_u16(&memory,1)",
                             "format_i8(&memory,1)",
                             "format_i16(&memory,1)",
                             "format_u8(&memory,1)",
                             "format_u32(&memory,1)",
                             "format_f32(&memory,1.0)",
                             "format_f64(&memory,1.0)",
                             "format_f32_fixed(&memory,1.0,18)",
                             "format_f64_fixed(&memory,1.0,18)",
                             "to_string(&memory,1)",
                             "to_string_i8(&memory,1)",
                             "to_string_i16(&memory,1)",
                             "to_string_i32(&memory,1)",
                             "to_string_i64(&memory,1)",
                             "to_string_u8(&memory,1)",
                             "to_string_u16(&memory,1)",
                             "to_string_u32(&memory,1)",
                             "to_string_u64(&memory,1)"})
        body += "{ def r: result<string> = std." + call +
                "; if !r.erroneous { return 1; } if r.error.code != status.NO_MEMORY || "
                "r.error.os_error != 0 { return 1; } }";
    body += R"(def port: u16 = 30000;
        def checked: result<string> = std.to_string_u16(&memory,port);
        if !checked.erroneous { return 4; }
        memory.free();
        if !fails<string>(std.format_f32_fixed(&memory,1.0,19),status.INVALID) || !fails<string>(std.format_f64_fixed(&memory,1.0,4294967295),status.INVALID) { return 2; }
        if !succeeds<f64>(std.parse_f64("1.25"),1.25) || !succeeds<f64>(std.f64_from_i64_exact(42),42.0) || !strings.equal(std.format_bool(true),"true") { return 3; }
        return 0;)";
    expect_run(invoke({"--stdlib-dir", directory, program(body)}), 0);
}
TEST_F(NumericLibraryTest, NamedConversionsDoNotEnableImplicitCastsAndSignaturesAreChecked) {
    for (const std::string body :
         {"std.parse_f64(1);", "std.parse_bool(true);", "std.format_bool(1);",
          "def n: i64 = 42; std.format_i32(&memory,n);",
          "def n: i32 = 42; std.format_u16(&memory,n);",
          "def n: i32 = 42; std.format_u8(&memory,n);",
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
    for (std::string body :
         {"def mut v: f32 = 0.0; __std_parse_f64(\"1\",&v);",
          "def v: i64 = 0; __std_i64_from_i32(1,&v);",
          "def mut v: i32 = 0; __std_i64_from_i32(1,&v);", "__std_i64_from_i32(1,null);",
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
            def parsed: result<f64> = std.parse_f64("42"); if parsed.erroneous { return 1; }
            def number: result<f64> = std.f64_from_i64_exact(42); if number.erroneous { return 1; }
            if parsed.value != number.value { return 1; }
            return gloin_std_parse_f64() + gloin_std_f64_from_i64();
        })");
    expect_run(invoke({file}), 42);
}
TEST_F(NumericLibraryTest, FloatAndIntegerAbiExecuteWithExternalRuntime) {
    auto ir = invoke(
        {"--emit-llvm",
         program(
             R"(def parsed: result<f32> = std.parse_f32("1.25"); if parsed.erroneous { return 1; }
def wide: result<f64> = std.f64_from_f32(parsed.value); if wide.erroneous { return 1; }
if !same(std.format_f64_fixed(&memory,wide.value,2),"1.25") { return 1; }
def n: result<i64> = std.i64_from_f64_trunc(wide.value); if n.erroneous { return 2; }
def u: result<u64> = std.parse_u64("18446744073709551615"); if u.erroneous { return 2; }
if !same(std.format_u64(&memory,u.value),"18446744073709551615") || !succeeds<bool>(std.parse_bool("true"),true) { return 2; }
def narrowed: result<i32> = std.i32_from_i64(n.value); if narrowed.erroneous { return 3; }
return narrowed.value + 41;
)")});
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

TEST_F(NumericLibraryTest, BuiltinResultsRejectUncheckedAccessAndRemovedWrappers) {
    for (const std::string body :
         {"std.parse_u64(\"42\");", "std.format_f64(&memory,1.0);", "std.i32_from_i64(42);",
          "def r: result<f64> = std.parse_f64(\"1\"); def v: f64 = r.value;",
          "def r: result<i64> = std.i64_from_u64(18446744073709551615); if r.erroneous { def v: "
          "i64 = r.value; }",
          "def r: std.I64Result = std.parse_i64(\"1\");",
          "def r: std.U64Result = std.parse_u64(\"1\");",
          "def r: std.F32Result = std.parse_f32(\"1\");",
          "def r: std.F64Result = std.parse_f64(\"1\");",
          "def r: std.BoolResult = std.parse_bool(\"true\");",
          "def r: std.FormatResult = std.format_i32(&memory,1);",
          "def n: i64 = std.i64_from_i32(42).value;"}) {
        const auto file = program(body + " return 0;");
        for (const auto *mode : {"--check", "--emit-ir", "--emit-llvm", "--jit"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(NumericLibraryTest, ScalarRoundingAndErrorCodesSurviveGIRJitAndNativeOutput) {
    const auto file = program(R"(
        if std.f64_from_i64_rounded(-9223372036854775808) != -9223372036854775808.0
            || std.f64_from_i64_rounded(9223372036854775807) != 9223372036854775808.0
            || std.f64_from_u64_rounded(18446744073709551615) != 18446744073709551616.0
            || std.f64_from_u64_rounded(9007199254740993) != 9007199254740992.0
            || std.f64_from_u64_rounded(9007199254740995) != 9007199254740996.0 { return 1; }
        def exact: result<f64> = std.f64_from_u64_exact(9007199254740993);
        if !exact.erroneous { return 2; }
        if exact.error.code != status.INEXACT || exact.error.os_error != 0
            || !strings.equal(exact.error.message,"numeric conversion loses precision") { return 3; }
        def underflow: result<f64> = std.parse_f64("1e-999");
        if !underflow.erroneous { return 4; }
        if underflow.error.code != status.UNDERFLOW
            || !strings.equal(underflow.error.message,"decimal f64 underflows to zero") { return 5; }
        def code: i32 = match std.parse_i32_checked("bad") {
            case value(number) => 0;
            case error(problem) => problem.code;
        };
        if code != status.INVALID { return 6; }
        memory.free();
        def precision: result<string> = std.format_f64_fixed(&memory,1.0,19);
        if !precision.erroneous { return 7; }
        if precision.error.code != status.INVALID || precision.error.os_error != 0
            || !strings.equal(precision.error.message,"precision exceeds 18 digits") { return 8; }
        return 42;
    )");
    const auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation :
         {"gloin.result_success", "gloin.result_failure", "gloin.error_code", "gloin.match"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({file}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/numeric-results";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}
