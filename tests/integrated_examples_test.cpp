#include "arena_runtime_internal.h"
#include "compiler.h"
#include "jit_runner.h"
#include "support/cli_fixture.h"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
namespace {
std::string literal(const std::string &value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"')
            result += '\\';
        result += c;
    }
    return result + '"';
}
struct Backing {
    std::unordered_map<void *, size_t> live;
    size_t bytes = 0, peak = 0, blocks = 0, fail_block = SIZE_MAX;
    bool failed = false;
    static void *allocate(void *context, size_t n) noexcept {
        auto &s = *static_cast<Backing *>(context);
        // Arena state is small; native backing blocks are at least 64 KiB.
        if (n >= 65536 && s.blocks++ == s.fail_block) {
            s.failed = true;
            return nullptr;
        }
        if (n > 512 * 1024 - s.bytes) {
            s.failed = true;
            return nullptr;
        }
        auto *p = std::malloc(n);
        if (p) {
            s.live.emplace(p, n);
            s.bytes += n;
            s.peak = std::max(s.peak, s.bytes);
        }
        return p;
    }
    static void release(void *context, void *p) noexcept {
        auto &s = *static_cast<Backing *>(context);
        auto found = s.live.find(p);
        EXPECT_NE(found, s.live.end());
        if (found != s.live.end()) {
            s.bytes -= found->second;
            s.live.erase(found);
        }
        std::free(p);
    }
};
size_t descriptors() {
    size_t count = 0;
    for ([[maybe_unused]] const auto &file : std::filesystem::directory_iterator("/dev/fd"))
        ++count;
    return count;
}
class IntegratedExamplesTest : public gloin_test::CliFixture {
  protected:
    std::filesystem::path examples() {
        if (const char *p = std::getenv("GLOIN_TEST_CLI"))
            return std::filesystem::path(p).parent_path() / "../share/gloinc/examples";
        return std::filesystem::path(gloin_test::strings_example).parent_path();
    }
    std::filesystem::path library() {
        if (const char *p = std::getenv("GLOIN_TEST_CLI"))
            return std::filesystem::path(p).parent_path() / "../share/gloinc/stdlib";
        return std::filesystem::path(gloin_test::gloinc).parent_path() / "stdlib";
    }
    std::string example(const std::string &name) {
        return (examples() / (name + ".gloin")).string();
    }
    std::string import(const std::string &name) {
        auto path = std::filesystem::relative(example(name), directory).generic_string();
        if (!path.starts_with("."))
            path = "./" + path;
        return "import " + literal(path) + "; ";
    }
    std::string wrapper(const std::string &name, const std::string &call) {
        return import(name) + "def main() -> i32 { return " + name + "." + call + "; }";
    }
    gloin_test::ProcessResult config(const std::string &text) {
        return invoke({example("config_reader"), "--", source(text, "settings.conf")});
    }
    gloin_test::ProcessResult statistics(const std::string &text,
                                         const std::string &columns = "1,2") {
        auto path = directory + "/report-" + std::to_string(calls) + ".txt";
        auto result =
            invoke({example("statistics_tool"), "--", source(text, "data.txt"), path, columns});
        if (result.status != 0)
            EXPECT_FALSE(std::filesystem::exists(path));
        return result;
    }
    void copy_modules() {
        for (const auto &p : std::filesystem::directory_iterator(library()))
            if (p.path().extension() == ".gloin")
                std::filesystem::copy_file(p.path(),
                                           std::filesystem::path(directory) / p.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
    }
    void replace(std::string &s, const std::string &from, const std::string &to) {
        auto pos = s.find(from);
        ASSERT_NE(pos, std::string::npos);
        s.replace(pos, from.size(), to);
    }
    void external(const std::string &program, const std::string &expected) {
        auto ir = invoke({"--emit-llvm", source(program)});
        ASSERT_EQ(ir.status, 0) << ir.err;
        const auto input = source(ir.out, "program.mlir");
        const char *runtime = std::getenv("GLOIN_TEST_ARENA_RUNTIME");
        std::vector<std::string> args{gloin_test::mlir_runner,
                                      input,
                                      "-e",
                                      "main",
                                      "-entry-point-result=i32",
                                      std::string("--shared-libs=") +
                                          (runtime ? runtime : gloin_test::arena_runtime)};
#ifdef __APPLE__
        if (auto *asan = dlsym(RTLD_DEFAULT, "__asan_init")) {
            Dl_info info{};
            ASSERT_NE(dladdr(asan, &info), 0);
            std::string preload = info.dli_fname;
            if (const char *p = std::getenv("DYLD_INSERT_LIBRARIES"); p && *p)
                preload += std::string(":") + p;
            args.insert(args.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
            args.insert(args.begin(), "/usr/bin/env");
        }
#endif
        std::vector<llvm::StringRef> argv;
        for (const auto &arg : args)
            argv.push_back(arg);
        const std::string prefix = directory + "/external-" + std::to_string(calls++);
        const std::string out = prefix + ".out", err = prefix + ".err";
        const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(), out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(args.front(), argv, std::nullopt, redirects, 10), 0);
        EXPECT_EQ(read(err), "");
        auto output = read(out);
        if (expected.ends_with("elapsed_ns=")) {
            ASSERT_TRUE(output.starts_with(expected)) << output;
            auto suffix = output.substr(expected.size());
            ASSERT_TRUE(suffix.ends_with("\n0\n")) << suffix;
            suffix.resize(suffix.size() - 3);
            EXPECT_FALSE(suffix.empty());
            EXPECT_EQ(suffix.find_first_not_of("0123456789"), std::string::npos);
        } else
            EXPECT_EQ(output, expected + "0\n");
    }
    void embedded(const std::string &program, Backing &backing, int expected,
                  const std::string &stdlib = "", const GloinClockSource *clock = nullptr) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, directory + "/embedded.gloin", context,
                                       CompilationMode::Executable, CompilationOutput::HighLevel,
                                       stdlib.empty() ? library().string() : stdlib);
        ASSERT_TRUE(compiled.success());
        const auto fds = descriptors();
        testing::internal::CaptureStdout();
        testing::internal::CaptureStderr();
        ExecutionResult result;
        {
            gloin::arena::AllocatorScope allocator({&backing, Backing::allocate, Backing::release});
            result = JitRunner::run(*compiled.module, {}, clock);
        }
        auto err = testing::internal::GetCapturedStderr();
        auto out = testing::internal::GetCapturedStdout();
        ASSERT_TRUE(result.success()) << err;
        EXPECT_EQ(result.value, expected) << err << out;
        EXPECT_TRUE(backing.live.empty());
        EXPECT_EQ(backing.bytes, 0u);
        EXPECT_EQ(descriptors(), fds);
    }
};
const std::string report =
    "column,count,min,max,mean\n1,3,0.000000,6.000000,3.000000\n2,3,0.000000,8.000000,4.000000\n";
} // namespace
TEST_F(IntegratedExamplesTest, ConfigCopiesRetainedLabelAndAcceptsDocumentedInput) {
    expect_run(config("\r\n # comment\r\nlabel = café\r\nseed = 18446744073709551615\r\n# "
                      "overwrite scratch\r\nsamples = 1000000"),
               0, "label=café\nseed=18446744073709551615\nsamples=1000000\n");
    expect_run(
        invoke({example("config_reader"), "--", (examples() / "data/simulation.conf").string()}), 0,
        "label=quarter circle\nseed=42\nsamples=1000\n");
}
TEST_F(IntegratedExamplesTest, ConfigRejectsUnknownDuplicateMissingAndMalformedKeys) {
    for (const std::string text : {"", "label=x\nseed=1\n", "label=x\nseed=1\nsamples=1\nseed=2\n",
                                   "name=x\n", "label=x=y\n", "label\n"}) {
        SCOPED_TRACE(text);
        expect_error(config(text), 2, "config: invalid input or I/O failure at line ");
    }
}
TEST_F(IntegratedExamplesTest, ConfigChecksNumericLabelAndLineBounds) {
    for (const std::string value : {"0", "1000001", "18446744073709551616", "-1", "1x"})
        expect_error(config("label=x\nseed=0\nsamples=" + value), 2, "line 3\n");
    expect_error(config("label=x\nseed=18446744073709551616\nsamples=1"), 2, "line 2\n");
    expect_error(config("label=" + std::string(129, 'x') + "\nseed=0\nsamples=1"), 2, "line 1\n");
    expect_error(config(std::string("label=x\0y", 9) + "\nseed=0\nsamples=1"), 2, "line 1\n");
    expect_error(config(std::string(1025, '#') + "\n"), 2, "line 1\n");
    expect_run(
        config(std::string(1024, '#') + "\nlabel=" + std::string(128, 'x') + "\nseed=0\nsamples=1"),
        0, "label=" + std::string(128, 'x') + "\nseed=0\nsamples=1\n");
}
TEST_F(IntegratedExamplesTest, StatisticsSelectsMultipleColumnsInRequestedOrder) {
    const auto target = directory + "/report.txt";
    expect_run(invoke({example("statistics_tool"), "--",
                       (examples() / "data/measurements.txt").string(), target, "1,2"}),
               0);
    EXPECT_EQ(read(target), report);
    const auto data = source("a,-2,10\r\nb, 1 ,20\r\nc,4,30", "data.txt");
    expect_run(invoke({example("statistics_tool"), "--", data, directory + "/ordered.txt", "2, 1"}),
               0);
    EXPECT_EQ(read(directory + "/ordered.txt"),
              "column,count,min,max,mean\n2,3,10.000000,30.000000,20.000000\n1,3,-2.000000,4."
              "000000,1.000000\n");
    std::string generated;
    int64_t sum = 0;
    for (int i = 0; i < 1000; ++i) {
        int value = (i * 17) % 101 - 50;
        sum += value;
        generated += "label," + std::to_string(value) + "," + std::to_string(2 * value + 7) + "\n";
    }
    auto generated_path = source(generated, "oracle.txt");
    auto result_path = directory + "/oracle-report.txt";
    expect_run(invoke({example("statistics_tool"), "--", generated_path, result_path, "2,1"}), 0);
    std::ostringstream expected;
    expected << std::fixed << std::setprecision(6)
             << "column,count,min,max,mean\n2,1000,-93.000000,107.000000,"
             << (2.0 * sum / 1000.0 + 7.0) << "\n1,1000,-50.000000,50.000000,"
             << (double(sum) / 1000.0) << "\n";
    EXPECT_EQ(read(result_path), expected.str());
}
TEST_F(IntegratedExamplesTest, StatisticsRejectsInvalidColumnSelections) {
    for (const std::string columns : {"", "1,1", "-1", "64", "18446744073709551616", "1,",
                                      "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16"})
        expect_error(statistics("x,1,2\n", columns), 2, "statistics:");
}
TEST_F(IntegratedExamplesTest, StatisticsRejectsMalformedRowsBeforeCreatingOutput) {
    for (const std::string text :
         {"", "\n", "x,1\n", "x,1,2\ny,3\n", "x,NaN,2\n", "x,1e13,2\n", "x,\"1\",2\n", "x,,2\n"})
        expect_error(statistics(text), 2, "statistics:");
    expect_error(statistics(std::string("x,1,2\0\n", 7)), 2, "statistics:");
    expect_error(statistics(std::string(4097, 'x') + ",1,2\n"), 2, "line too long");
}
TEST_F(IntegratedExamplesTest, StatisticsAcceptsExactFieldLineAndMagnitudeLimits) {
    const auto input = source(std::string(4094, 'x') + ",1\n", "limit.txt");
    expect_run(invoke({example("statistics_tool"), "--", input, directory + "/line.txt", "1"}), 0);
    std::string row, columns;
    for (int i = 0; i < 64; ++i) {
        if (i)
            row += ",";
        row += "1";
        if (i < 16) {
            if (i)
                columns += ",";
            columns += std::to_string(i);
        }
    }
    expect_run(statistics(row + "\n", columns), 0);
    expect_error(statistics(row + ",1\n", "0"), 2, "too many fields");
    const auto extremes = source("-1000000000000\n1000000000000\n", "extremes.txt");
    expect_run(
        invoke({example("statistics_tool"), "--", extremes, directory + "/extremes.out", "0"}), 0);
    EXPECT_EQ(
        read(directory + "/extremes.out"),
        "column,count,min,max,mean\n0,2,-1000000000000.000000,1000000000000.000000,0.000000\n");
}
TEST_F(IntegratedExamplesTest, ExistingDestinationsAndInputsArePreservedAndCliErrorsAreExplicit) {
    const auto input = source("x,1,2\n", "input.txt"), target = source("keep", "output.txt");
    expect_error(invoke({example("statistics_tool"), "--", input, target, "1,2"}), 3,
                 "cannot create new output");
    EXPECT_EQ(read(target), "keep");
    expect_error(invoke({example("statistics_tool"), "--", input, input, "1,2"}), 3,
                 "cannot create new output");
    EXPECT_EQ(read(input), "x,1,2\n");
    expect_error(invoke({example("statistics_tool"), "--", directory + "/missing", target, "0"}), 2,
                 "cannot open input");
    expect_error(invoke({example("config_reader"), "--", directory + "/missing"}), 2, "line 0\n");
    for (const auto *name : {"config_reader", "statistics_tool"})
        expect_error(invoke({example(name)}), 2, "usage:");
}
TEST_F(IntegratedExamplesTest, PublicConfigAndSimulationComposeAcrossModules) {
    auto config = source("label=kept\nseed=42\nsamples=1000\n", "simulation.conf");
    auto program = import("config_reader") + import("simulation_lab") +
                   "import \"@arena\"; import \"@strings\"; def main() -> i32 { def mut owner: "
                   "arena.GeneralArena = arena.GeneralArena.create(); defer owner.free(); def "
                   "parsed: config_reader.ConfigResult = config_reader.load(&owner," +
                   literal(config) +
                   "); if parsed.status != 0 { return 1; } def copied: config_reader.Config = "
                   "parsed.value; if !strings.equal(copied.label,\"kept\") { return 2; } return "
                   "simulation_lab.simulate(copied.seed,copied.samples); }";
    auto result = invoke({source(program)});
    EXPECT_EQ(result.status, 0) << result.err;
    EXPECT_TRUE(result.out.starts_with("seed=42\nsamples=1000\ninside=768\ndice_sum=3525\npi=3."
                                       "072000\nabs_error=0.069593\nelapsed_ns="))
        << result.out;
}
TEST_F(IntegratedExamplesTest, AllThreeProgramsExecuteThroughExternalLlvm) {
    auto config = source("label=external\nseed=42\nsamples=1000\n", "config.txt");
    external(wrapper("config_reader", "execute(" + literal(config) + ")"),
             "label=external\nseed=42\nsamples=1000\n");
    const auto input = (examples() / "data/measurements.txt").string(),
               output = directory + "/external-report.txt";
    external(wrapper("statistics_tool",
                     "execute(" + literal(input) + "," + literal(output) + ",\"1,2\")"),
             "");
    EXPECT_EQ(read(output), report);
    external(wrapper("simulation_lab", "simulate(42,1000)"),
             "seed=42\nsamples=1000\ninside=768\ndice_sum=3525\npi=3.072000\nabs_error=0."
             "069593\nelapsed_ns=");
}
TEST_F(IntegratedExamplesTest, LongStreamingRunsReuseActualBackingBlocksAndReleaseAllResources) {
    std::string data;
    for (int i = 0; i < 100000; ++i)
        data += "row,-2,4\n";
    auto input = source(data, "large.txt"), output = directory + "/large-report.txt";
    Backing stats;
    embedded(wrapper("statistics_tool",
                     "execute(" + literal(input) + "," + literal(output) + ",\"1,2\")"),
             stats, 0);
    EXPECT_FALSE(stats.failed);
    EXPECT_LE(stats.blocks, 2u);
    EXPECT_LT(stats.peak, 140000u);
    EXPECT_EQ(read(output), "column,count,min,max,mean\n1,100000,-2.000000,-2.000000,-2.000000\n2,"
                            "100000,4.000000,4.000000,4.000000\n");
    std::string config = "label=retained\nseed=42\nsamples=1\n";
    for (int i = 0; i < 100000; ++i)
        config += "# reuse scratch storage\n";
    Backing reader;
    embedded(wrapper("config_reader", "execute(" + literal(source(config, "long.conf")) + ")"),
             reader, 0);
    EXPECT_FALSE(reader.failed);
    EXPECT_LE(reader.blocks, 2u);
    EXPECT_LT(reader.peak, 140000u);
}
TEST_F(IntegratedExamplesTest, SimulationStorageDoesNotGrowWithSampleCount) {
    Backing small, large;
    embedded(wrapper("simulation_lab", "simulate(42,1)"), small, 0);
    embedded(wrapper("simulation_lab", "simulate(42,100000)"), large, 0);
    EXPECT_FALSE(large.failed);
    EXPECT_EQ(small.blocks, large.blocks);
    EXPECT_EQ(small.peak, large.peak);
}
TEST_F(IntegratedExamplesTest, BackingAllocationFailuresUnwindFilesAndArenas) {
    const auto input = (examples() / "data/measurements.txt").string();
    const auto config = (examples() / "data/simulation.conf").string();
    for (size_t fail : {0u, 1u}) {
        Backing a;
        a.fail_block = fail;
        embedded(wrapper("config_reader", "execute(" + literal(config) + ")"), a, 2);
        EXPECT_TRUE(a.failed);
        Backing b;
        b.fail_block = fail;
        const auto output = directory + "/absent-" + std::to_string(fail);
        embedded(wrapper("statistics_tool",
                         "execute(" + literal(input) + "," + literal(output) + ",\"1,2\")"),
                 b, fail == 0 ? 3 : 2);
        EXPECT_TRUE(b.failed);
        EXPECT_FALSE(std::filesystem::exists(output));
    }
    Backing c;
    c.fail_block = 0;
    embedded(wrapper("simulation_lab", "simulate(42,10)"), c, 3);
    EXPECT_TRUE(c.failed);
}
TEST_F(IntegratedExamplesTest, InvalidDataAndRepeatedInvocationsDoNotLeakResources) {
    const auto bad = source("x,broken,2\n", "bad.txt");
    const auto config = source("label=kept\nseed=bad\nsamples=1\n", "bad.conf");
    for (int repeat = 0; repeat < 4; ++repeat) {
        Backing a, b;
        embedded(wrapper("config_reader", "execute(" + literal(config) + ")"), a, 2);
        embedded(wrapper("statistics_tool", "execute(" + literal(bad) + "," +
                                                literal(directory + "/absent") + ",\"1,2\")"),
                 b, 2);
        EXPECT_FALSE(a.failed);
        EXPECT_FALSE(b.failed);
    }
    EXPECT_FALSE(std::filesystem::exists(directory + "/absent"));
}
TEST_F(IntegratedExamplesTest, ReportWriteFlushAndCloseFailuresAreObserved) {
    copy_modules();
    const auto original = read((library() / "io.gloin").string());
    const auto input = (examples() / "data/measurements.txt").string();
    for (const std::string operation : {"write", "flush", "close"}) {
        std::string io = original;
        if (operation == "write")
            replace(io, "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult {",
                    "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult { if "
                    "strings.equal(text,\"column,count,min,max,mean\") { return WriteResult { "
                    "status: status.IO_ERROR, os_error: 5, written: 0 }; }");
        if (operation == "flush")
            replace(io, "def pub flush(self: &Stream) -> IoResult {",
                    "def pub flush(self: &Stream) -> IoResult { if self.standard == 0 && "
                    "!self.readable { return IoResult { status: status.IO_ERROR, os_error: 5 }; }");
        if (operation == "close")
            replace(io, "def code: i32 = __io_close(handle, &error);",
                    "def mut code: i32 = __io_close(handle, &error); if !self.readable { code = "
                    "status.IO_ERROR; }");
        source(io, "io.gloin");
        Backing backing;
        embedded(wrapper("statistics_tool", "execute(" + literal(input) + "," +
                                                literal(directory + "/" + operation + ".out") +
                                                ",\"1,2\")"),
                 backing, 3, directory);
    }
}
TEST_F(IntegratedExamplesTest, InputCloseFailuresPreventSuccessAndOutputCreation) {
    copy_modules();
    auto io = read((library() / "io.gloin").string());
    replace(io, "def code: i32 = __io_close(handle, &error);",
            "__io_close(handle, &error); def code: i32 = status.IO_ERROR;");
    source(io, "io.gloin");
    Backing a, b;
    auto output = directory + "/absent";
    embedded(wrapper("config_reader",
                     "execute(" + literal((examples() / "data/simulation.conf").string()) + ")"),
             a, 2, directory);
    embedded(wrapper("statistics_tool",
                     "execute(" + literal((examples() / "data/measurements.txt").string()) + "," +
                         literal(output) + ",\"1,2\")"),
             b, 2, directory);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(IntegratedExamplesTest, StandardOutputAndClockFailuresReleaseProgramStorage) {
    copy_modules();
    auto io = read((library() / "io.gloin").string());
    replace(
        io, "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult {",
        "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult { if self.standard "
        "== 2 { return WriteResult { status: status.IO_ERROR, os_error: 5, written: 0 }; }");
    source(io, "io.gloin");
    Backing config, simulation;
    embedded(wrapper("config_reader",
                     "execute(" + literal((examples() / "data/simulation.conf").string()) + ")"),
             config, 3, directory);
    embedded(wrapper("simulation_lab", "simulate(42,10)"), simulation, 3, directory);
    struct Clock {
        unsigned calls = 0;
        int mode = 0;
    };
    for (int mode = 0; mode < 3; ++mode) {
        Clock state{0, mode};
        GloinClockSource clock{[](void *data, uint64_t *ticks, int32_t *error) -> int32_t {
                                   auto &s = *static_cast<Clock *>(data);
                                   unsigned call = s.calls++;
                                   *ticks = call == 0 ? 100 : 50;
                                   *error = 5;
                                   // Fail first/second read, or supply backwards ticks; no sleeps.
                                   return s.mode == int(call) ? 2 : 0;
                               },
                               &state};
        Backing backing;
        embedded(wrapper("simulation_lab", "simulate(42,10)"), backing, 2, "", &clock);
        EXPECT_EQ(state.calls, mode == 0 ? 1u : 2u);
    }
}
