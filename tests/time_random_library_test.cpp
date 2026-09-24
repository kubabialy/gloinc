#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include "time_runtime_internal.h"
#include <cerrno>
#include <filesystem>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
namespace {
struct Script {
    uint64_t ticks = 0;
    uint64_t step = 0;
    int32_t code = 0;
    int32_t error = 0;
    unsigned calls = 0;
};
int32_t clock_read(void *data, uint64_t *ticks, int32_t *error) {
    auto &s = *static_cast<Script *>(data);
    *ticks = s.ticks;
    s.ticks += s.step;
    *error = s.error;
    ++s.calls;
    return s.code;
}
class TimeRandomLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source("import \"@time\"; import \"@random\"; import \"@status\"; import \"@std\"; "
                      "import \"@arena\"; def main() -> i32 { " +
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
TEST_F(TimeRandomLibraryTest, DurationUnitsBoundariesAndCheckedArithmetic) {
    expect_run(invoke({program(R"(
        def d: time.Duration = time.Duration { nanoseconds: 1500999 };
        if d.whole_microseconds() != 1500 || d.whole_milliseconds() != 1 { return 1; }
        def seconds: time.DurationResult = time.Duration.from_milliseconds(1500);
        if seconds.status != status.OK || seconds.value.seconds_f64_rounded() != 1.5 { return 2; }
        if time.Duration.from_seconds(2).value.nanoseconds != 2000000000 { return 3; }
        if time.Duration.from_milliseconds(18446744073709).status != status.OK || time.Duration.from_milliseconds(18446744073710).status != status.OVERFLOW { return 4; }
        if time.Duration.from_seconds(18446744073).status != status.OK || time.Duration.from_seconds(18446744074).status != status.OVERFLOW { return 5; }
        def largest: time.Duration = time.Duration { nanoseconds: 18446744073709551615 };
        def zero: time.Duration = time.Duration { nanoseconds: 0 };
        def one: time.Duration = time.Duration { nanoseconds: 1 };
        if largest.checked_add(zero).value.nanoseconds != 18446744073709551615 { return 6; }
        if largest.checked_add(one).status != status.OVERFLOW || largest.checked_add(one).value.nanoseconds != 0 { return 7; }
        if zero.checked_sub(one).status != status.OVERFLOW || zero.checked_sub(one).value.nanoseconds != 0 { return 8; }
        if largest.checked_sub(largest).value.nanoseconds != 0 { return 9; }
        def approximate: f64 = largest.seconds_f64_rounded();
        if approximate < 18446744073.0 || approximate > 18446744074.0 { return 10; }
        return 0;)")}),
               0);
}
TEST_F(TimeRandomLibraryTest, ElapsedIsExplicitAndRejectsBackwardsTicks) {
    expect_run(invoke({program(R"(
        def a: time.Instant = time.Instant { ticks_ns: 7 }; def b: time.Instant = time.Instant { ticks_ns: 12 };
        if time.elapsed(a,b).status != status.OK || time.elapsed(a,b).value.nanoseconds != 5 { return 1; }
        if time.elapsed(a,a).status != status.OK || time.elapsed(a,a).value.nanoseconds != 0 { return 2; }
        if time.elapsed(b,a).status != status.INVALID || time.elapsed(b,a).value.nanoseconds != 0 { return 3; }
        def z: time.Instant = time.Instant { ticks_ns: 0 }; def m: time.Instant = time.Instant { ticks_ns: 18446744073709551615 };
        if time.elapsed(z,m).value.nanoseconds != 18446744073709551615 { return 4; }
        def first: time.InstantResult = time.monotonic_now(); def last: time.InstantResult = time.monotonic_now();
        if first.status != status.OK || last.status != status.OK || first.os_error != 0 || last.os_error != 0 { return 5; }
        if time.elapsed(first.value,last.value).status != status.OK { return 6; }
        return 0;)")}),
               0);
}
TEST_F(TimeRandomLibraryTest, SeededVectorsAndIndependentValueCopies) {
    expect_run(invoke({program(R"(
        def mut rng: random.SplitMix64 = random.SplitMix64.seeded(0);
        if random.SPLITMIX64_VERSION != 1 || rng.next_u64() != 16294208416658607535 { return 1; }
        if rng.next_u64() != 7960286522194355700 || rng.next_u64() != 487617019471545679 { return 2; }
        def mut copy: random.SplitMix64 = rng;
        for def mut i: u64 = 0; i < 1000; i = i + 1 { if rng.next_u64() != copy.next_u64() { return 3; } }
        def mut forty_two: random.SplitMix64 = random.SplitMix64.seeded(42);
        if forty_two.next_u64() != 13679457532755275413 || forty_two.next_u64() != 2949826092126892291 { return 4; }
        def mut all: random.SplitMix64 = random.SplitMix64.seeded(18446744073709551615);
        if all.next_u64() != 16490336266968443936 { return 5; }
        return 0;)")}),
               0);
}
TEST_F(TimeRandomLibraryTest, BoundedSamplingRejectsTailAndNeverAdvancesOnInvalidBound) {
    expect_run(invoke({program(R"(
        def mut zero: random.SplitMix64 = random.SplitMix64.seeded(0);
        def bad: random.U64Result = zero.below(0);
        if bad.status != status.INVALID || bad.value != 0 || zero.next_u64() != 16294208416658607535 { return 1; }
        def mut single: random.SplitMix64 = random.SplitMix64.seeded(0);
        if single.below(1).value != 0 || single.next_u64() != 7960286522194355700 { return 2; }
        def mut tail: random.SplitMix64 = random.SplitMix64.seeded(7046029254386353131);
        if tail.below(18446744073709551615).value != 16294208416658607535 || tail.next_u64() != 7960286522194355700 { return 3; }
        def mut rejection: random.SplitMix64 = random.SplitMix64.seeded(3);
        if rejection.below(9223372036854775809).value != 3694763184872335752 || rejection.next_u64() != 11307387092600937729 { return 4; }
        def mut rng: random.SplitMix64 = random.SplitMix64.seeded(42);
        for def mut bound: u64 = 1; bound <= 256; bound = bound + 1 {
            for def mut i: u64 = 0; i < 100; i = i + 1 {
                def draw: random.U64Result = rng.below(bound);
                if draw.status != status.OK || draw.value >= bound { return 5; }
            }
        }
        return 0;)")}),
               0);
}
TEST_F(TimeRandomLibraryTest, UnitFloatsIncludeZeroExcludeOneAndConsumeOneWord) {
    expect_run(invoke({program(R"(
        def mut low: random.SplitMix64 = random.SplitMix64.seeded(7046029254386353131);
        if low.unit_f64() != 0.0 || low.next_u64() != 16294208416658607535 { return 1; }
        def mut high: random.SplitMix64 = random.SplitMix64.seeded(3558559446808474027);
        if high.unit_f64() != 0.9999999999999999 { return 2; }
        def mut a: random.SplitMix64 = random.SplitMix64.seeded(42); def mut b: random.SplitMix64 = a;
        for def mut i: u64 = 0; i < 10000; i = i + 1 {
            def sample: f64 = a.unit_f64();
            def expected: f64 = std.f64_from_u64_exact(b.next_u64() / 2048).value / 9007199254740992.0;
            if sample < 0.0 || sample >= 1.0 || sample != expected { return 3; }
        }
        return 0;)")}),
               0);
}
TEST_F(TimeRandomLibraryTest, JitClockInjectionIsScopedAndRepeatedInvocationsRestoreHostState) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(import "@time"; import "@status";
        def main() -> i32 { def a: time.InstantResult = time.monotonic_now(); def b: time.InstantResult = time.monotonic_now();
        if a.status != status.OK || b.status != status.OK { return 1; }
        if time.elapsed(a.value,b.value).value.nanoseconds != 25 { return 2; } return 42; })",
                                   "clock.gloin", context, CompilationMode::Executable,
                                   CompilationOutput::HighLevel, library().string());
    ASSERT_TRUE(compiled.success());
    Script outer{777};
    GloinClockSource host{clock_read, &outer};
    gloin::time::ClockScope scope(&host);
    ASSERT_TRUE(scope.valid());
    for (uint64_t base : {100u, 900u}) {
        Script fake{base, 25};
        GloinClockSource clock{clock_read, &fake};
        auto result = JitRunner::run(*compiled.module, {}, &clock);
        EXPECT_TRUE(result.success());
        EXPECT_EQ(result.value, 42);
        EXPECT_EQ(fake.calls, 2u);
        uint64_t restored = 0;
        int32_t error = 99;
        EXPECT_EQ(gloin_time_monotonic(&restored, &error), 0);
        EXPECT_EQ(restored, 777u);
    }
    EXPECT_EQ(outer.calls, 2u);
}
TEST_F(TimeRandomLibraryTest, DefaultJitUsesOsClockAndInvalidProviderDoesNotExecute) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        R"(import "@time"; def main() -> i32 { return time.monotonic_now().status; })",
        "clock.gloin", context, CompilationMode::Executable, CompilationOutput::HighLevel,
        library().string());
    ASSERT_TRUE(compiled.success());
    Script fake{999, 0, 4, EIO};
    GloinClockSource host{clock_read, &fake};
    gloin::time::ClockScope outer(&host);
    ASSERT_TRUE(outer.valid());
    auto result = JitRunner::run(*compiled.module);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(result.value, 0);
    EXPECT_EQ(fake.calls, 0u);
    GloinClockSource invalid{nullptr, &fake};
    auto bad = JitRunner::run(*compiled.module, {}, &invalid);
    EXPECT_FALSE(bad.success());
    EXPECT_EQ(fake.calls, 0u);
    uint64_t ticks = 0;
    int32_t error = 0;
    EXPECT_EQ(gloin_time_monotonic(&ticks, &error), 4);
    EXPECT_EQ(error, EIO);
    EXPECT_EQ(fake.calls, 1u);
}
TEST_F(TimeRandomLibraryTest, InjectedFailuresZeroPayloadAndBackwardsReadingsStayRecoverable) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        R"(import "@time"; def main() -> i32 { def r: time.InstantResult = time.monotonic_now();
        if r.value.ticks_ns != 0 { return 99; } return r.status; })",
        "clock.gloin", context, CompilationMode::Executable, CompilationOutput::HighLevel,
        library().string());
    ASSERT_TRUE(compiled.success());
    for (int32_t code : {2, 3, 4, 999}) {
        Script fake{123, 0, code, EIO};
        GloinClockSource clock{clock_read, &fake};
        auto result = JitRunner::run(*compiled.module, {}, &clock);
        EXPECT_TRUE(result.success());
        EXPECT_EQ(result.value, code == 999 ? 2 : code);
        EXPECT_EQ(fake.calls, 1u);
    }
    auto backwards = compile_source(
        R"(import "@time"; def main() -> i32 { def a: time.Instant = time.monotonic_now().value;
        def b: time.Instant = time.monotonic_now().value; return time.elapsed(a,b).status; })",
        "backwards.gloin", context, CompilationMode::Executable, CompilationOutput::HighLevel,
        library().string());
    ASSERT_TRUE(backwards.success());
    for (uint64_t step : {uint64_t(0), UINT64_MAX - 9}) {
        Script fake{100, step};
        GloinClockSource clock{clock_read, &fake};
        auto result = JitRunner::run(*backwards.module, {}, &clock);
        EXPECT_TRUE(result.success());
        EXPECT_EQ(result.value, step ? 2 : 0);
        EXPECT_EQ(fake.calls, 2u);
    }
}
TEST_F(TimeRandomLibraryTest, PublicTypesMutabilityAndPrivatePrimitivesAreEnforced) {
    for (std::string body :
         {"def rng: random.SplitMix64 = random.SplitMix64.seeded(0); rng.next_u64();",
          "def mut rng: random.SplitMix64 = random.SplitMix64.seeded(0); rng.state = 1;",
          "time.elapsed(time.Duration { nanoseconds: 0 }, time.Instant { ticks_ns: 0 });",
          "random.SplitMix64.seeded(-1);", "def mut state: u64 = 0; __random_splitmix64(&state);",
          "time.monotonic_now(1);", "def x: i32 = 1; time.Duration.from_seconds(x);"})
        expect_error(invoke({"--check", program(body + "return 0;")}), 1, "error:");
    auto root = source("import \"@random\"; def main() -> i32 { random.probe(); return 0; }");
    for (std::string body :
         {"def mut s: i32 = 0; __random_splitmix64(&s);", "__random_splitmix64();",
          "def mut ns: u64 = 0; def mut e: i32 = 0; __time_monotonic(&ns,&e);"}) {
        source("def pub probe() -> void {" + body + "}", "random.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", root}), 1, "error:");
    }
    root = source("import \"@time\"; def main() -> i32 { time.probe(); return 0; }");
    for (std::string body :
         {"def mut s: u64 = 0; __random_splitmix64(&s);",
          "def mut ns: i64 = 0; def mut e: i32 = 0; __time_monotonic(&ns,&e);"}) {
        source("def pub probe() -> void {" + body + "}", "time.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", root}), 1, "error:");
    }
}
TEST_F(TimeRandomLibraryTest, SourceLibrariesRemainReplaceableAndModuleIdentityIsCanonical) {
    auto root = source("import \"@random\"; def main() -> i32 { return random.probe(); }");
    source("def pub probe() -> i32 { def mut state: u64 = 0; def word: u64 = "
           "__random_splitmix64(&state); if word == "
           "16294208416658607535 { return 42; } return 1; }",
           "random.gloin");
    expect_run(invoke({"--stdlib-dir", directory, root}), 42);
    source("def pub probe() -> i32 { return 43; }", "random.gloin");
    expect_run(invoke({"--stdlib-dir", directory, root}), 43);
    source("def pub probe() -> i32 { def mut s: u64 = 0; __random_splitmix64(&s); return 0; }",
           "random.gloin");
    root = source("import \"./random\"; def main() -> i32 { return random.probe(); }");
    expect_error(invoke({"--check", root}), 1, "error:");
}
TEST_F(TimeRandomLibraryTest, NativeNamesCannotCollideAndMalformedAbisFailBeforeExecution) {
    expect_run(
        invoke({source(R"(import "@random"; def gloin_random_splitmix64() -> i32 { return 42; }
        def main() -> i32 { def mut rng: random.SplitMix64 = random.SplitMix64.seeded(0);
        if rng.next_u64() != 16294208416658607535 { return 1; } return gloin_random_splitmix64(); })")}),
        42);
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (std::string decl : {"llvm.func @gloin_random_splitmix64(!llvm.ptr) -> i32",
                             "llvm.func @gloin_random_splitmix64(i64) -> i64",
                             "llvm.func @gloin_time_monotonic(!llvm.ptr) -> i32",
                             "llvm.func @gloin_time_monotonic(!llvm.ptr, !llvm.ptr, ...) -> i32"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + decl +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}
TEST_F(TimeRandomLibraryTest, ExternalLlvmUsesSeededStateAndTheDefaultClock) {
    auto ir = invoke(
        {"--emit-llvm", program(R"(def mut rng: random.SplitMix64 = random.SplitMix64.seeded(42);
        if rng.next_u64() != 13679457532755275413 { return 1; }
        def a: time.InstantResult = time.monotonic_now(); def b: time.InstantResult = time.monotonic_now();
        if a.status != status.OK || b.status != status.OK || time.elapsed(a.value,b.value).status != status.OK { return 2; } return 42;)")});
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
TEST_F(TimeRandomLibraryTest, PackagedSimulationHasReproducibleInputsAndCheckedCliBounds) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "simulation_lab.gloin";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example = std::filesystem::path(binary).parent_path() /
                  "../share/gloinc/examples/simulation_lab.gloin";
    const std::string prefix = "seed=42\nsamples=1000\ninside=768\ndice_sum=3525\npi=3.072000\nabs_"
                               "error=0.069593\nelapsed_ns=";
    for (int run = 0; run < 2; ++run) {
        auto result = invoke({example.string(), "--", "42", "1000"});
        ASSERT_EQ(result.status, 0) << result.err;
        EXPECT_TRUE(result.err.empty());
        ASSERT_TRUE(result.out.starts_with(prefix)) << result.out;
        auto number = result.out.substr(prefix.size());
        ASSERT_GT(number.size(), 1u);
        EXPECT_EQ(number.back(), '\n');
        number.pop_back();
        EXPECT_TRUE(number.find_first_not_of("0123456789") == std::string::npos);
    }
    for (auto count : {"0", "1000001", "-1", "bad"})
        expect_error(invoke({example.string(), "--", "42", count}), 2, "");
    expect_error(invoke({example.string(), "--", "18446744073709551616", "10"}), 2, "invalid seed");
    expect_error(invoke({example.string(), "--", "42"}), 2, "usage:");
}
TEST_F(TimeRandomLibraryTest, SimulationElapsedReportUsesTheInjectedClockExactly) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "simulation_lab.gloin";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example = std::filesystem::path(binary).parent_path() /
                  "../share/gloinc/examples/simulation_lab.gloin";
    mlir::MLIRContext context;
    auto compiled = compile_source(read(example.string()), example.string(), context,
                                   CompilationMode::Executable, CompilationOutput::HighLevel,
                                   library().string());
    ASSERT_TRUE(compiled.success());
    Script fake{100, 5000000};
    GloinClockSource clock{clock_read, &fake};
    testing::internal::CaptureStdout();
    auto result = JitRunner::run(*compiled.module, {"simulation_lab.gloin", "42", "1000"}, &clock);
    auto output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(result.success());
    EXPECT_EQ(result.value, 0);
    EXPECT_EQ(fake.calls, 2u);
    EXPECT_EQ(output, "seed=42\nsamples=1000\ninside=768\ndice_sum=3525\npi=3.072000\nabs_error=0."
                      "069593\nelapsed_ns=5000000\n");
}
TEST_F(TimeRandomLibraryTest, GuideProgramsExecuteVerbatim) {
    auto guide = std::filesystem::path(gloin_test::strings_guide).parent_path() / "time-random.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        guide =
            std::filesystem::path(binary).parent_path() / "../share/doc/gloinc/docs/time-random.md";
    auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(doc.substr(pos, end - pos))}), 0,
                   blocks == 0 ? "elapsed=2 ms\n" : "13679457532755275413\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}
