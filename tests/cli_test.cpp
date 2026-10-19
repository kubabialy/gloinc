#include "codegen.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/JSON.h"
#include "llvm/Object/ObjectFile.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include <future>
#include <set>

namespace {
class CliTest : public gloin_test::CliFixture {
  protected:
    llvm::json::Object timings(const gloin_test::ProcessResult &result) {
        const std::string prefix = "gloinc-timings: ";
        const auto position = result.err.find(prefix);
        if (position == std::string::npos) {
            ADD_FAILURE() << result.err;
            return {};
        }
        EXPECT_EQ(position, result.err.rfind(prefix));
        auto parsed = llvm::json::parse(result.err.substr(position + prefix.size()));
        if (!parsed) {
            ADD_FAILURE() << llvm::toString(parsed.takeError());
            return {};
        }
        auto *report = parsed->getAsObject();
        if (!report) {
            ADD_FAILURE() << "Expected timing object";
            return {};
        }
        EXPECT_EQ(report->getInteger("schema_version"), 1);
        EXPECT_EQ(report->getString("llvm_version"), LLVM_VERSION_STRING);
        EXPECT_GT(report->getInteger("peak_rss_bytes").value_or(0), 0);
        int64_t measured = 0;
        auto *stages = report->getArray("stages");
        if (!stages) {
            ADD_FAILURE() << "Missing stages";
            return {};
        }
        for (const auto &value : *stages) {
            auto *stage = value.getAsObject();
            if (!stage) {
                ADD_FAILURE() << "Expected stage object";
                return {};
            }
            EXPECT_GE(stage->getInteger("nanoseconds").value_or(-1), 0);
            EXPECT_GT(stage->getInteger("calls").value_or(0), 0);
            measured += stage->getInteger("nanoseconds").value_or(0);
        }
        EXPECT_GE(report->getInteger("unattributed_ns").value_or(-1), 0);
        EXPECT_EQ(report->getInteger("total_ns").value_or(-1),
                  measured + report->getInteger("unattributed_ns").value_or(-1));
        return std::move(*report);
    }
    bool has_stage(const llvm::json::Object &report, llvm::StringRef name) {
        auto *stages = report.getArray("stages");
        if (!stages) return false;
        for (const auto &value : *stages)
            if (auto *stage = value.getAsObject(); stage && stage->getString("name") == name)
                return true;
        return false;
    }
};
} // namespace

TEST_F(CliTest, CompilationTimingsPreserveInspectionOutputAndReportBoundaries) {
    const auto file = source("def helper(value: i32) -> i32 { return value + 2; } "
                             "def main() -> i32 { return helper(40); }");
    for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm"}) {
        SCOPED_TRACE(mode);
        auto plain = invoke_raw({mode, file});
        auto timed = invoke_raw({"--time-compilation", mode, file});
        ASSERT_EQ(plain.status, 0) << plain.err;
        ASSERT_EQ(timed.status, 0) << timed.err;
        EXPECT_TRUE(plain.err.empty());
        EXPECT_EQ(plain.out, timed.out);
        auto report = timings(timed);
        EXPECT_EQ(report.getBoolean("compilation_succeeded"), true);
        for (const char *stage : {"input_setup", "parsing", "module_loading", "semantic_analysis",
                                  "gir_generation", "ir_verification"})
            EXPECT_TRUE(has_stage(report, stage)) << stage;
        EXPECT_EQ(has_stage(report, "lowering"), mode == "--emit-llvm");
        EXPECT_EQ(has_stage(report, "ir_output"), mode != "--check");
        EXPECT_FALSE(has_stage(report, "llvm_optimization"));
        auto *counts = report.getArray("counts");
        ASSERT_NE(counts, nullptr);
        ASSERT_FALSE(counts->empty());
        auto *gir = counts->front().getAsObject();
        ASSERT_NE(gir, nullptr);
        EXPECT_EQ(gir->getString("name"), "gir");
        EXPECT_EQ(gir->getInteger("function_definitions"), 2);
        EXPECT_GT(gir->getInteger("operations").value_or(0), 2);
    }
}

TEST_F(CliTest, CompilationTimingsSeparateOptimizationEmissionAndLinking) {
    const auto file = source("def main() -> i32 { return 42; }");
    for (const std::string optimization : {"-O0", "-O2"}) {
        for (const bool object : {false, true}) {
            SCOPED_TRACE(optimization + (object ? " object" : " executable"));
            const auto output = directory + (object ? "/timed.o" : "/timed");
            auto result = invoke_raw({"--time-compilation", optimization,
                                       object ? "--emit-object" : "--emit-exe", "-o", output, file});
            ASSERT_EQ(result.status, 0) << result.err;
            EXPECT_TRUE(result.out.empty());
            auto report = timings(result);
            EXPECT_EQ(report.getBoolean("compilation_succeeded"), true);
            for (const char *stage : {"lowering", "native_target_setup", "llvm_translation",
                                      "object_emission"})
                EXPECT_TRUE(has_stage(report, stage)) << stage;
            EXPECT_EQ(has_stage(report, "llvm_optimization"), optimization == "-O2");
            EXPECT_EQ(has_stage(report, "linking"), !object);
            if (!object) {
                std::string message;
                bool failed = false;
                EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 10, 0,
                                                    &message, &failed), 42) << message;
                EXPECT_FALSE(failed);
            }
        }
    }
}

TEST_F(CliTest, CompilationTimingsReportFailuresWithoutContinuingPipeline) {
    for (const auto &[text, last_stage] : std::vector<std::pair<std::string, std::string>>{
             {"def main( {", "parsing"},
             {"import \"./absent\";", "module_loading"},
             {"def main() -> i32 { return missing; }", "semantic_analysis"}}) {
        auto result = invoke_raw({"--time-compilation", "--check", source(text)});
        expect_error(result, 1, "error:");
        auto report = timings(result);
        EXPECT_EQ(report.getBoolean("compilation_succeeded"), false);
        EXPECT_TRUE(has_stage(report, last_stage));
        EXPECT_FALSE(has_stage(report, "gir_generation"));
        EXPECT_FALSE(has_stage(report, "linking"));
    }
    auto failed_output = invoke_raw({"--time-compilation", "-o", directory + "/absent/out",
                                     source("def main() -> i32 { return 0; }")});
    expect_error(failed_output, 1, "Cannot create output");
    auto report = timings(failed_output);
    EXPECT_EQ(report.getBoolean("compilation_succeeded"), false);
    EXPECT_TRUE(has_stage(report, "object_emission"));
    EXPECT_FALSE(has_stage(report, "linking"));
    auto duplicate = invoke_raw({"--time-compilation", "--time-compilation", "missing.gloin"});
    expect_error(duplicate, 2, "may appear only once");
    EXPECT_EQ(duplicate.err.find("gloinc-timings:"), std::string::npos);
}

TEST_F(CliTest, CompilationTimingsKeepJitOutputAndProgramExitStatus) {
    const auto file = source("import \"@std\"; def main() -> i32 { "
                             "std.println(\"timed program\"); return 17; }");
    auto result = invoke_raw({"--time-compilation", "--jit", file});
    ASSERT_EQ(result.status, 17) << result.err;
    EXPECT_EQ(result.out, "timed program\n");
    auto report = timings(result);
    EXPECT_EQ(report.getBoolean("compilation_succeeded"), true);
    EXPECT_EQ(report.getString("mode"), "jit");
    EXPECT_TRUE(has_stage(report, "lowering"));
    EXPECT_TRUE(has_stage(report, "jit_preparation"));
    EXPECT_FALSE(has_stage(report, "object_emission"));
}

TEST_F(CliTest, ReachabilityPrunesExecutablesAndJitButPreservesObjectExports) {
    source("def pub used() -> i32 { return 42; } "
           "def pub unused_export() -> i32 { return 99; }", "helpers.gloin");
    const auto file = source("import \"./helpers\"; "
                             "def unused_root() -> i32 { return helpers.unused_export(); } "
                             "def main() -> i32 { return helpers.used(); }");
    for (const std::string mode : {"--emit-ir", "--emit-llvm", "--emit-object",
                                    "--emit-exe", "--jit"}) {
        for (const std::string optimization : {"-O0", "-O2"}) {
            const bool native = mode == "--emit-object" || mode == "--emit-exe";
            if (!native && optimization == "-O2") continue;
            SCOPED_TRACE(mode + optimization);
            const auto output = directory + "/output";
            std::vector<std::string> args{"--time-compilation", mode};
            if (native) args.insert(args.end(), {optimization, "-o", output});
            args.push_back(file);
            auto result = invoke_raw(args);
            ASSERT_EQ(result.status, mode == "--jit" ? 42 : 0) << result.err;
            auto report = timings(result);
            const bool pruning = mode == "--emit-exe" || mode == "--jit";
            EXPECT_EQ(has_stage(report, "gir_reachability"), pruning);
            auto *counts = report.getArray("counts");
            ASSERT_NE(counts, nullptr);
            bool found_reachable = false;
            for (const auto &value : *counts) {
                auto *count = value.getAsObject();
                ASSERT_NE(count, nullptr);
                if (count->getString("name") == "gir")
                    EXPECT_EQ(count->getInteger("function_definitions"), 4);
                if (count->getString("name") == "reachable_gir") {
                    found_reachable = true;
                    EXPECT_EQ(count->getInteger("function_definitions"), 2);
                }
            }
            EXPECT_EQ(found_reachable, pruning);
            if (mode == "--emit-exe") {
                EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 10), 42);
            } else if (mode == "--emit-object") {
                auto object = llvm::object::ObjectFile::createObjectFile(output);
                ASSERT_TRUE(static_cast<bool>(object)) << llvm::toString(object.takeError());
                bool exported = false;
                for (const auto &symbol : object->getBinary()->symbols()) {
                    auto name = symbol.getName();
                    ASSERT_TRUE(static_cast<bool>(name)) << llvm::toString(name.takeError());
                    if (*name == "unused_root" || *name == "_unused_root") exported = true;
                }
                EXPECT_TRUE(exported);
            } else if (mode != "--jit") {
                EXPECT_NE(result.out.find("@unused_root"), std::string::npos);
            }
        }
    }
}

TEST_F(CliTest, ReachabilityStillChecksUnusedImportedBodiesAndObjectEntry) {
    source("def pub good() -> i32 { return 42; } "
           "def unused() -> i32 { return missing_value; }", "bad.gloin");
    const auto file = source("import \"./bad\"; def main() -> i32 { return bad.good(); }");
    for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--jit",
                                    "--emit-object", "--emit-exe"}) {
        std::vector<std::string> args{mode};
        if (mode == "--emit-object" || mode == "--emit-exe")
            args.insert(args.end(), {"-o", directory + "/invalid"});
        args.push_back(file);
        expect_error(invoke_raw(args), 1, "missing_value");
    }
    const auto helper = source("def helper() -> i32 { return 42; }", "helper.gloin");
    expect_success(invoke_raw({"--check", helper}), "");
    expect_error(invoke_raw({"--emit-object", "-o", directory + "/helper.o", helper}),
                 1, "main() -> i32");
}

TEST_F(CliTest, HelpAndVersionAreStandaloneSuccessfulCommands) {
    for (const std::string option : {"--help", "-h"}) {
        auto result = invoke({option});
        EXPECT_EQ(result.status, 0);
        EXPECT_TRUE(result.err.empty());
        for (const std::string word : {"Usage:", "--jit", "--run", "--check", "--emit-ir", "--emit-llvm",
                                       "--emit-object", "--emit-exe", "Exit status:"})
            EXPECT_NE(result.out.find(word), std::string::npos);
    }
    for (const std::string option : {"--version", "-V"})
        expect_success(invoke({option}), "gloinc 0.2.0 (LLVM/MLIR " LLVM_VERSION_STRING ")\n");
}

TEST_F(CliTest, NativeExecutableRunsWithoutJitAndReceivesArguments) {
    const auto file = source("import \"@process\"; import \"@std\"; "
                             "def main() -> i32 { std.println(\"native output\"); "
                             "if process.arg_count() == 2 { return 23; } return 1; }");
    const auto executable = directory + "/native program";
    expect_success(invoke_raw({"-o", executable, file}), "");
    const auto out = directory + "/native.stdout";
    const auto err = directory + "/native.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const std::vector<llvm::StringRef> arguments{executable, "flag"};
    const int status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt, redirects, 10,
                                                 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(status, 23) << message << read(err);
    EXPECT_EQ(read(out), "native output\n");
    EXPECT_TRUE(read(err).empty());
}

TEST_F(CliTest, OptimizedNativeOutputPreservesCheckedArrayBounds) {
    const auto valid = source(R"(
        def main() -> i32 {
            def mut values: [i32; 3] = {10, 20, 12};
            def first: *i32 = &values[0];
            return *(first + 1) + values[2];
        }
    )", "valid.gloin");
    const auto optimized = directory + "/optimized";
    expect_success(invoke_raw({"-O2", "-o", optimized, valid}), "");
    std::string message;
    bool launch_failed = false;
    EXPECT_EQ(llvm::sys::ExecuteAndWait(optimized, {optimized}, std::nullopt, {}, 10, 0,
                                        &message, &launch_failed), 32) << message;
    EXPECT_FALSE(launch_failed);

    const auto invalid = source("def main() -> i32 { def a: [i32; 2] = {1, 2}; return a[2]; }",
                                "invalid.gloin");
    const auto trapping = directory + "/trapping";
    expect_success(invoke_raw({"-O2", "-o", trapping, invalid}), "");
    EXPECT_LT(llvm::sys::ExecuteAndWait(trapping, {trapping}, std::nullopt, {}, 10, 0,
                                        &message, &launch_failed), 0) << message;
    EXPECT_FALSE(launch_failed);
    const auto null_offset = source(
        "def main() -> i32 { def p: *i32 = null; def q: *i32 = p + 0; return 1; }",
        "null-offset.gloin");
    expect_success(invoke_raw({"-O2", "-o", trapping, null_offset}), "");
    EXPECT_LT(llvm::sys::ExecuteAndWait(trapping, {trapping}, std::nullopt, {}, 10, 0,
                                        &message, &launch_failed), 0) << message;
    EXPECT_FALSE(launch_failed);
    expect_error(invoke_raw({"-O2", "--jit", valid}), 2, "require native output");
    expect_error(invoke_raw({"-O2", "-O0", valid}), 2, "exactly one optimization");
}

TEST_F(CliTest, NativeObjectsUseDirectCallRelocationsAndPreserveRecursion) {
    const auto file = source(R"(
        import "@std";
        def fib(n: i32) -> i32 {
            if n < 2 { return n; }
            return fib(n - 1) + fib(n - 2);
        }
        def main() -> i32 {
            std.println("native calls");
            return fib(9);
        }
    )");
    expect_run(invoke_raw({"--jit", file}), 34, "native calls\n");
    for (const std::string optimization : {"-O0", "-O2"}) {
        SCOPED_TRACE(optimization);
        const auto output = directory + "/calls.o";
        expect_success(invoke_raw({optimization, "--emit-object", "-o", output, file}), "");
        auto object = llvm::object::ObjectFile::createObjectFile(output);
        ASSERT_TRUE(static_cast<bool>(object)) << llvm::toString(object.takeError());
        std::set<std::string> direct_calls;
        for (const auto &section : object->getBinary()->sections()) {
            for (const auto &relocation : section.relocations()) {
                llvm::SmallString<32> type;
                relocation.getTypeName(type);
                if (type != "ARM64_RELOC_BRANCH26" && type != "R_AARCH64_CALL26" &&
                    type != "R_X86_64_PLT32")
                    continue;
                const auto symbol = relocation.getSymbol();
                if (symbol == object->getBinary()->symbol_end()) continue;
                auto name = symbol->getName();
                ASSERT_TRUE(static_cast<bool>(name)) << llvm::toString(name.takeError());
                name->consume_front("_"); // Mach-O symbol prefix.
                direct_calls.insert(name->str());
            }
        }
        // These external calls cannot be inlined. Large-model JIT emission
        // instead materializes their addresses and calls through registers.
        EXPECT_TRUE(direct_calls.contains("gloin_process_arguments_push"));
        EXPECT_TRUE(direct_calls.contains("gloin_process_arguments_pop"));

        const auto executable = directory + "/calls";
        expect_success(invoke_raw({optimization, "-o", executable, file}), "");
        const auto out = directory + "/calls.out";
        const auto err = directory + "/calls.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt,
                                            redirects, 10), 34);
        EXPECT_EQ(read(out), "native calls\n");
        EXPECT_TRUE(read(err).empty());
    }
}

TEST_F(CliTest, NativeCheckedIntegerBoundariesAtEveryWidthAndOptimizationLevel) {
    for (const auto &info : core_types) {
        if (!info.is_integer) continue;
        SCOPED_TRACE(std::string(info.name));
        const auto zero = llvm::APInt(info.bits, 0);
        const auto one = llvm::APInt(info.bits, 1);
        const auto two = llvm::APInt(info.bits, 2);
        const auto high = info.is_signed ? llvm::APInt::getSignedMaxValue(info.bits)
                                         : llvm::APInt::getMaxValue(info.bits);
        const auto low = info.is_signed ? llvm::APInt::getSignedMinValue(info.bits) : zero;
        struct Case { unsigned operation; llvm::APInt a, b, value; bool trap; };
        std::vector<Case> cases;
        auto add = [&](unsigned operation, llvm::APInt a, llvm::APInt b) {
            bool trap = false;
            auto value = zero;
            if (operation == 0 || operation >= 5)
                value = info.is_signed ? a.sadd_ov(b, trap) : a.uadd_ov(b, trap);
            else if (operation == 1)
                value = info.is_signed ? a.ssub_ov(b, trap) : a.usub_ov(b, trap);
            else if (operation == 2)
                value = info.is_signed ? a.smul_ov(b, trap) : a.umul_ov(b, trap);
            else {
                trap = b.isZero() || (info.is_signed && a.isMinSignedValue() && b.isAllOnes());
                if (!trap && operation == 3) value = info.is_signed ? a.sdiv(b) : a.udiv(b);
                if (!trap && operation == 4) value = info.is_signed ? a.srem(b) : a.urem(b);
            }
            if (operation == 6) value = zero;
            cases.push_back({operation, a, b, value, trap});
        };
        for (unsigned operation = 0; operation < 5; ++operation) {
            add(operation, high, zero);
            add(operation, high, one);
            add(operation, low, one);
            add(operation, high, two);
            add(operation, low, two);
            if (info.is_signed) {
                add(operation, low, -one);
                add(operation, high, -one);
                add(operation, -two, -two);
            }
        }
        // Unused results and zero multiplication must not discard an overflow
        // side effect, including after native O2 inlining and simplification.
        for (unsigned operation : {5u, 6u}) {
            add(operation, high, one);
            add(operation, high, zero);
        }
        auto decimal = [&](const llvm::APInt &value) {
            llvm::SmallString<32> text;
            value.toString(text, 10, info.is_signed);
            return text.str().str();
        };
        std::string operands_a, operands_b, expected, operations;
        for (const auto &test : cases) {
            if (!operands_a.empty()) {
                operands_a += ","; operands_b += ","; expected += ","; operations += ",";
            }
            operands_a += decimal(test.a);
            operands_b += decimal(test.b);
            expected += decimal(test.value);
            operations += std::to_string(test.operation);
        }
        const std::string type(info.name);
        const auto count = std::to_string(cases.size());
        // argc indexes runtime data: LLVM cannot fold the tested operands to
        // constants, even after inlining. APInt above is the independent oracle.
        const auto file = source(
            "import \"@process\"; def main() -> i32 { "
            "def a: [" + type + ";" + count + "] = {" + operands_a + "};"
            "def b: [" + type + ";" + count + "] = {" + operands_b + "};"
            "def expected: [" + type + ";" + count + "] = {" + expected + "};"
            "def operations: [i32;" + count + "] = {" + operations + "};"
            "def index: u64 = process.arg_count() - 1;"
            "def operation: i32 = operations[index];"
            "def mut value: " + type + " = 0;"
            "if operation == 0 { value = a[index] + b[index]; }"
            "if operation == 1 { value = a[index] - b[index]; }"
            "if operation == 2 { value = a[index] * b[index]; }"
            "if operation == 3 { value = a[index] / b[index]; }"
            "if operation == 4 { value = a[index] % b[index]; }"
            "if operation == 5 { a[index] + b[index]; value = expected[index]; }"
            "if operation == 6 { value = (a[index] + b[index]) * 0; }"
            "if value == expected[index] { return 42; } return 1; }");
        const auto executable = directory + "/boundaries";
        for (const std::string optimization : {"-O0", "-O2"}) {
            SCOPED_TRACE(optimization);
            auto compiled = invoke_raw({optimization, "-o", executable, file});
            ASSERT_EQ(compiled.status, 0) << compiled.err << compiled.message;
            ASSERT_FALSE(compiled.launch_failed);
            std::vector<llvm::StringRef> arguments{executable};
            for (size_t index = 0; index < cases.size(); ++index) {
                SCOPED_TRACE(index);
                std::string message;
                bool launch_failed = false;
                const auto status = llvm::sys::ExecuteAndWait(
                    executable, arguments, std::nullopt, {}, 10, 0, &message, &launch_failed);
                ASSERT_FALSE(launch_failed) << message;
                if (cases[index].trap) {
                    EXPECT_LT(status, 0) << message;
                    EXPECT_EQ(message.find("timed out"), std::string::npos) << message;
                    EXPECT_TRUE(message.find("Trace") != std::string::npos ||
                                message.find("Illegal instruction") != std::string::npos) << message;
                } else {
                    EXPECT_EQ(status, 42) << message;
                }
                arguments.push_back("case");
            }
        }
    }
}

TEST_F(CliTest, GIRRangeProofsPreserveTrapsAcrossAliasingAndControlFlow) {
    const auto file = source(R"(
        import "@process";
        def set(index: &u64, value: u64) -> void { *index = value; }
        def main() -> i32 {
            def mode: u64 = process.arg_count();
            def mut values: [u8; 4] = {42, 42, 42, 42};
            def mut view: [u8] = values[..];
            def mut index: u64 = 0;
            def four: u64 = 4;
            def limit: u8 = 64;
            def expected: u8 = 42;
            if mode == 1 {
                while index + 1 <= view.len {
                    if view[index] != expected { return 1; }
                    index = index + 1;
                }
                return 42;
            }
            if index >= view.len { return 2; }
            if mode == 2 { set(&index, view.len); }
            if mode == 3 { index = view.len; }
            if mode == 4 {
                if view.len < four { return 3; }
                view = view[..1];
                index = 3;
            }
            if mode == 5 {
                if view[0] >= limit { return 4; }
                view[0] = 255;
                def byte: u8 = view[0] * 4;
                if byte == 0 { return 5; }
            }
            if mode == 6 {
                // The successful sum equals len, so index+4 is out of bounds.
                if index + 4 <= view.len {
                    if view[index + 4] == expected { return 6; }
                }
            }
            if mode == 7 {
                def negative: i64 = -1;
                if view[negative] == expected { return 7; }
            }
            if mode == 8 {
                def mut byte: u8 = 63;
                if byte >= 64 { return 8; }
                byte = 255;
                def product: u8 = byte * 4;
                if product == 0 { return 8; }
            }
            if mode == 9 {
                // A loop's first safe iteration cannot justify its backedge.
                while index < 8 {
                    if view[index] != expected { return 9; }
                    index = index + 1;
                }
            }
            if view[index] == expected { return 0; }
            return 1;
        }
    )");
    const auto executable = directory + "/range-proof-traps";
    for (const std::string mode : {"--run", "-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        if (mode != "--run")
            expect_success(invoke_raw({mode, "-o", executable, file}), "");
        for (unsigned count = 1; count <= 9; ++count) {
            SCOPED_TRACE(count);
            gloin_test::ProcessResult result;
            if (mode == "--run") {
                std::vector<std::string> arguments{"--run", file, "--"};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result = invoke_raw(arguments);
            } else {
                std::vector<llvm::StringRef> arguments{executable};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt,
                                                          {}, 10, 0, &result.message,
                                                          &result.launch_failed);
            }
            ASSERT_FALSE(result.launch_failed) << result.message;
            if (count == 1) EXPECT_EQ(result.status, 42) << result.err << result.message;
            else {
                EXPECT_LT(result.status, 0) << result.err << result.message;
                EXPECT_EQ(result.message.find("timed out"), std::string::npos);
                EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                            result.message.find("Illegal instruction") != std::string::npos)
                    << result.message;
            }
        }
    }
}

TEST_F(CliTest, OptimizedLargeCallChainKeepsBoundedInliningAndCorrectResults) {
    std::string text = "import \"@process\"; def mix(input: u64) -> u64 { def mut value: u64 = input;";
    for (unsigned step = 1; step <= 8; ++step)
        text += "value = (value * 17 + " + std::to_string(step) + ") % 1000003;";
    text += "return value; } def main() -> i32 { def mut value: u64 = process.arg_count();";
    uint64_t expected = 1;
    for (unsigned call = 0; call < 1024; ++call) {
        text += "value = mix(value);";
        for (unsigned step = 1; step <= 8; ++step)
            expected = (expected * 17 + step) % 1000003;
    }
    text += "if value == " + std::to_string(expected) + " { return 0; } return 1; }";
    const auto file = source(text);
    const auto executable = directory + "/chain";
    auto compiled = invoke_raw({"--time-compilation", "-O2", "-o", executable, file});
    ASSERT_EQ(compiled.status, 0) << compiled.err << compiled.message;
    ASSERT_FALSE(compiled.launch_failed);
    auto report = timings(compiled);
    auto *counts = report.getArray("counts");
    ASSERT_NE(counts, nullptr);
    bool found = false;
    for (const auto &entry : *counts) {
        auto *count = entry.getAsObject();
        if (!count || count->getString("name") != "optimized_llvm_ir") continue;
        found = true;
        // A generous, target-independent bound catches full expansion of the
        // 8,192-step dependency chain without imposing a wall-clock CI limit.
        EXPECT_LT(count->getInteger("instructions").value_or(12000), 12000);
    }
    EXPECT_TRUE(found);
    std::string message;
    bool failed = false;
    EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10, 0,
                                        &message, &failed), 0) << message;
    EXPECT_FALSE(failed);
}

TEST_F(CliTest, NativeObjectAndFailurePaths) {
    const auto file = source("def main() -> i32 { return 42; }");
    const auto object = directory + "/program.o";
    expect_success(invoke({"--emit-object", "-o", object, file}), "");
    auto buffer = llvm::MemoryBuffer::getFile(object);
    ASSERT_TRUE(buffer);
    EXPECT_GT((*buffer)->getBufferSize(), 4u);
#ifdef __linux__
    EXPECT_EQ((*buffer)->getBuffer().substr(0, 4), "\x7f" "ELF");
#else
    EXPECT_EQ((*buffer)->getBuffer().substr(0, 4), "\xcf\xfa\xed\xfe");
#endif
    expect_error(invoke({"--emit-object", file}), 2, "-o PATH");
    expect_error(invoke_raw({"--jit", "-o", object, file}), 2, "-o PATH");
    expect_error(invoke({"--run", "-o", object, file}), 2, "-o PATH");
    expect_error(invoke({"--emit-exe", "-o", file, file}), 2, "must differ");
    expect_error(invoke({"--emit-exe", "-o", directory + "/./program.gloin", file}), 2,
                 "must differ");
    const auto invalid = source("def main() -> i32 { return missing; }", "invalid.gloin");
    const auto absent = directory + "/absent";
    expect_error(invoke_raw({invalid}), 1, "missing");
    expect_error(invoke({"--emit-exe", "-o", absent, invalid}), 1, "missing");
    EXPECT_FALSE(llvm::sys::fs::exists(absent));
    const auto collision = source("def gloin_process_arguments_push() -> i32 { return 0; } "
                                  "def main() -> i32 { return 0; }",
                                  "collision.gloin");
    expect_error(invoke({"--emit-exe", "-o", absent, collision}), 1,
                 "conflicts with native process runtime");
}

TEST_F(CliTest, NativeExecutableLinksLocalModulesAndArenaRuntime) {
    const auto executable = directory + "/module-lab";
    expect_success(invoke({"--emit-exe", "-o", executable, gloin_test::module_example}), "");
    const auto out = directory + "/module.stdout";
    const auto err = directory + "/module.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int status = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                 10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(status, 0) << message << read(err);
    EXPECT_EQ(read(out), "module lab: ok\n");
    EXPECT_TRUE(read(err).empty());
}

TEST_F(CliTest, UsageErrorsDoNotReadOrExecuteInput) {
    for (const std::vector<std::string> arguments :
         std::vector<std::vector<std::string>>{{},
                                               {"--check"},
                                               {"--"},
                                               {"--unknown"},
                                               {"-"},
                                               {""},
                                               {"a", "b"},
                                               {"--run", "--run", "missing"},
                                               {"--jit", "--jit", "missing"},
                                               {"--jit", "--run", "missing"},
                                               {"--run", "--check", "missing"},
                                               {"--emit-ir", "--emit-llvm", "missing"},
                                               {"--help", "missing"},
                                               {"--version", "--help"},
                                               {"--check=anything", "missing"}})
        expect_error(invoke(arguments), 2, "Usage:");
}

TEST_F(CliTest, JitAndRunAliasesExecuteSource) {
    auto first = source("def main() -> i32 { return 17; }", "first.gloin");
    expect_run(invoke_raw({"--jit", first}), 17);
    expect_run(invoke_raw({"--run", gloin_test::core_example}), 42);
}

TEST_F(CliTest, EveryI32ResultUsesHostExitStatusWithoutPrinting) {
    for (const std::string value :
         {"0", "-1", "2147483647", "-2147483648", "1", "2", "256", "257"}) {
        auto file = source("def main() -> i32 { return " + value + "; }");
        expect_run(invoke({file}), std::stoi(value));
    }
}

TEST_F(CliTest, CheckAllowsHelperModulesAndNeverExecutes) {
    for (const std::string text :
         {"def helper(x: i32) -> i32 { return x + 1; }", "def main() -> i32 { return 1 / 0; }",
          "def main() -> i32 { for ;; {} return 0; }", ""}) {
        auto file = source(text);
        expect_success(invoke({"--check", file}), "");
    }
}

TEST_F(CliTest, HighLevelInspectionPrintsVerifiedIRWithSourceLocations) {
    auto file = source("def main() -> i32 { return 42; }");
    auto result = invoke({"--emit-ir", file});
    ASSERT_EQ(result.status, 0) << result.err;
    EXPECT_TRUE(result.err.empty());
    EXPECT_NE(result.out.find(file), std::string::npos);
    mlir::MLIRContext context;
    CodeGen load_dialects(context);
    auto module = mlir::parseSourceString<mlir::ModuleOp>(result.out, &context);
    ASSERT_TRUE(module);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    EXPECT_TRUE(module->lookupSymbol<mlir::func::FuncOp>("main"));
}

TEST_F(CliTest, LLVMInspectionPrintsOnlyVerifiedLLVMOperations) {
    auto file = source("def helper(x: i32) -> i32 { if x < 0 { return 0; } return x + 1; }");
    auto result = invoke({"--emit-llvm", file});
    ASSERT_EQ(result.status, 0) << result.err;
    EXPECT_TRUE(result.err.empty());
    EXPECT_NE(result.out.find(file), std::string::npos);
    mlir::MLIRContext context;
    CodeGen load_dialects(context);
    auto module = mlir::parseSourceString<mlir::ModuleOp>(result.out, &context);
    ASSERT_TRUE(module);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*module)));
    EXPECT_TRUE(module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("helper"));
    module->walk([](mlir::Operation *op) {
        EXPECT_TRUE(mlir::isa<mlir::ModuleOp>(op) || op->getName().getDialectNamespace() == "llvm");
    });
}

TEST_F(CliTest, InspectionDoesNotExecuteTrappingCode) {
    auto file = source("def main() -> i32 { return 1 / 0; }");
    for (const std::string option : {"--emit-ir", "--emit-llvm"}) {
        auto result = invoke({option, file});
        EXPECT_EQ(result.status, 0) << result.err;
        EXPECT_TRUE(result.err.empty());
        EXPECT_NE(result.out.find("module"), std::string::npos);
        EXPECT_NE(result.out.find(option == "--emit-ir" ? "gloin.checked_int_binary"
                                                      : "llvm.intr.trap"),
                  std::string::npos);
    }
}

TEST_F(CliTest, CompilerErrorsKeepSourceLocationsAndNoPartialOutput) {
    for (const std::string text :
         {"def main() -> i32 {\nreturn 0x;\n}", "def main() -> i32 {\nreturn 42\n}",
          "def main() -> i32 {\nreturn missing;\n}", "def main() -> i32 {\nreturn true;\n}",
          "import \"missing\";", "def main() -> i32 { defer f(); return 0; }"}) {
        const auto file = source(text);
        for (const std::string option : {"--run", "--check", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({option, file}), 1, file + ":");
    }
}

TEST_F(CliTest, RunRequiresMainAndEveryModeRejectsAnInvalidMain) {
    auto file = source("def helper() -> i32 { return 42; }");
    expect_error(invoke({file}), 1, "main");
    file = source("def main() -> void {}");
    for (const std::string option : {"--run", "--check", "--emit-ir", "--emit-llvm"})
        expect_error(invoke({option, file}), 1, "main");
}

TEST_F(CliTest, MissingUnreadableAndNonRegularFilesFail) {
    expect_error(invoke({directory + "/missing.gloin"}), 1, "cannot read");
    expect_error(invoke({directory}), 1, "not a regular file");
    const auto file = source("def main() -> i32 { return 42; }");
    ASSERT_FALSE(llvm::sys::fs::setPermissions(file, llvm::sys::fs::perms::no_perms));
    const auto result = invoke({file});
    EXPECT_FALSE(llvm::sys::fs::setPermissions(file, llvm::sys::fs::perms::owner_all));
    expect_error(result, 1, "cannot read");
}

TEST_F(CliTest, PathsWithSpacesAndOptionTerminatorsAreHandledLiterally) {
    const auto file = source("def main() -> i32 { return 42; }", "-quoted ' ; $() file");
    expect_run(invoke({"--", file}), 42);
    expect_run(invoke({file, "--run"}), 42);
    expect_error(invoke({"--", "--not-a-file"}), 1, "cannot read");
    expect_error(invoke({"--not-a-file"}), 2, "option");
    auto link = directory + "/linked.gloin";
    ASSERT_FALSE(llvm::sys::fs::create_link(file, link));
    expect_run(invoke({link}), 42);
}

TEST_F(CliTest, ReadsAllBytesAndRejectsInvalidEncoding) {
    auto text = std::string("def main() -> i32 { return 42; }");
    text.push_back('\0');
    auto file = source(text);
    expect_error(invoke({file}), 1, "error:");
    text = "def main() -> i32 { return 42; } // ";
    text.push_back(static_cast<char>(0xff));
    file = source(text);
    expect_error(invoke({file}), 1, "UTF-8");
}

TEST_F(CliTest, RepeatedFileRunsProduceIdenticalResults) {
    auto file =
        source("def main() -> i32 { def mut x: i32 = 0; while x < 3 { x = x + 1; } return x; }");
    for (unsigned i = 0; i < 3; ++i)
        expect_run(invoke({file}), 3);
}

TEST_F(CliTest, ConcurrentInvocationsHaveIndependentOutput) {
    auto first = source("def main() -> i32 { return 17; }", "first.gloin");
    auto second = source("def main() -> i32 { return 93; }", "second.gloin");
    auto a = std::async(std::launch::async, [&] { return invoke({first}); });
    auto b = std::async(std::launch::async, [&] { return invoke({second}); });
    expect_run(a.get(), 17);
    expect_run(b.get(), 93);
}

TEST_F(CliTest, RuntimeTrapsCannotMasqueradeAsProgramResults) {
    for (const std::string text : {"def main() -> i32 { return 1 / 0; }",
                                   "def main() -> i32 { def x: f64 = 1.0 / 0.0; return 42; }"}) {
        auto file = source(text);
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty());
        EXPECT_TRUE(result.err.empty()) << result.err;
        EXPECT_EQ(result.message.find("timed out"), std::string::npos);
        EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                    result.message.find("Illegal instruction") != std::string::npos)
            << result.message;
    }
}

TEST_F(CliTest, GIRAdjacentFloatChecksPreserveRoundingDiscardedFailuresAndEffects) {
    const auto file = source(R"(
        import "@process";
        import "@std";
        def calculate32(a: f32, b: f32, total: f32) -> f32 { return total + a * b; }
        def calculate64(a: f64, b: f64, total: f64) -> f64 { return total + a * b; }
        def discarded32(a: f32, b: f32, total: f32) -> void { total + a * b; }
        def discarded64(a: f64, b: f64, total: f64) -> void { total + a * b; }
        def observe(value: f64) -> f64 { std.print("effect;"); return value; }
        def intervening_call(a: f64, b: f64, total: f64) -> f64 {
            return a * b + observe(total);
        }
        def inverse(a: f64, b: f64) -> f64 { return 1.0 / (a * b); }
        def conditional(a: f64, b: f64, skip: bool) -> f64 {
            def product: f64 = a * b;
            if skip { return 0.0; }
            return 1.0 + product;
        }
        def main() -> i32 {
            def mode: u64 = process.arg_count();
            if mode == 1 {
                // Contracting either expression into FMA changes its zero result.
                if calculate32(1.00000011920928955078125, 1.00000011920928955078125,
                               -1.0000002384185791015625) != 0.0 { return 1; }
                if calculate64(1.0000000000000002, 1.0000000000000002,
                               -1.0000000000000004) != 0.0 { return 2; }
                if calculate32(3.0, 2.0, 1.0) != 7.0 { return 3; }
                if calculate64(3.0, 2.0, 1.0) != 7.0 { return 4; }
                discarded32(1.0, 2.0, 3.0);
                discarded64(1.0, 2.0, 3.0);
            }
            if mode == 2 { discarded32(3e38, 2.0, 0.0); }
            if mode == 3 { discarded64(1e308, 2.0, 0.0); }
            if mode == 4 { discarded32(3e38, 1.0, 3e38); }
            if mode == 5 { discarded64(1e308, 1.0, 1e308); }
            if mode == 6 { intervening_call(1e308, 2.0, 0.0); }
            if mode == 7 { inverse(1e308, 2.0); }
            if mode == 8 { conditional(1e308, 2.0, true); }
            if mode == 9 {
                if intervening_call(2.0, 3.0, 1.0) != 7.0 { return 5; }
            }
            std.print("ok;");
            return 42;
        }
    )");
    const auto executable = directory + "/float-checks";
    const auto output = directory + "/native.out";
    const auto errors = directory + "/native.err";
    for (const std::string mode : {"--run", "-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        if (mode != "--run") {
            auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err << built.message;
        }
        for (unsigned count = 1; count <= 9; ++count) {
            SCOPED_TRACE(count);
            gloin_test::ProcessResult result{};
            if (mode == "--run") {
                std::vector<std::string> arguments{"--run", file, "--"};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result = invoke_raw(arguments);
            } else {
                std::vector<llvm::StringRef> arguments{executable};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output, errors};
                ASSERT_FALSE(llvm::sys::fs::remove(output));
                ASSERT_FALSE(llvm::sys::fs::remove(errors));
                result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt,
                    redirects, 10, 0, &result.message, &result.launch_failed);
                result.out = read(output);
                result.err = read(errors);
            }
            ASSERT_FALSE(result.launch_failed) << result.message;
            if (count == 1 || count == 9) {
                EXPECT_EQ(result.status, 42) << result.err << result.message;
                EXPECT_EQ(result.out, count == 1 ? "ok;" : "effect;ok;");
            } else {
                EXPECT_LT(result.status, 0) << result.err << result.message;
                EXPECT_TRUE(result.out.empty()) << result.out;
                EXPECT_EQ(result.message.find("timed out"), std::string::npos);
                EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                            result.message.find("Illegal instruction") != std::string::npos) << result.message;
            }
            EXPECT_TRUE(result.err.empty()) << result.err;
        }
    }
}

TEST_F(CliTest, GIRLocalReductionsPreserveRoundingFailuresEmptyLoopsAndAliases) {
    const auto file = source(R"(
        import "@process";
        import "@std";
        def dot64(x: f64, y: f64, z: f64, w: f64, n: i64) -> f64 {
            def a: [f64; 3] = {x, z, 1.0};
            def b: [f64; 3] = {y, w, 1.0};
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 1 { total = total + a[i] * b[i]; }
            return total;
        }
        def dot32(x: f32, y: f32, z: f32, w: f32, n: i64) -> f32 {
            def a: [f32; 3] = {x, z, 1.0};
            def b: [f32; 3] = {y, w, 1.0};
            def p: [const f32] = a[..];
            def r: [const f32] = b[..];
            def mut total: f32 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 1 { total = total + p[i] * r[i]; }
            return total;
        }
        def overlap(n: u64) -> f64 {
            def a: [f64; 4] = {1.0, 2.0, 3.0, 4.0};
            def p: [const f64] = a[..3];
            def tail: [const f64] = a[..];
            def r: [const f64] = tail[1..];
            def mut total: f64 = 0.0;
            for def mut i: u64 = 0; i < n; i = i + 1 { total = total + p[i] * r[i]; }
            return total;
        }
        def effects() -> void {
            def a: [f64; 2] = {1.0, 1e308};
            def b: [f64; 2] = {2.0, 2.0};
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < 2; i = i + 1 {
                std.print("load;");
                total = total + a[i] * b[i];
                std.print("ok;");
            }
        }
        def main() -> i32 {
            def mode: u64 = process.arg_count();
            if mode == 1 {
                if dot64(-1.0000000000000004, 1.0, 1.0000000000000002, 1.0000000000000002, 2) != 0.0 { return 1; }
                if dot32(-1.0000002384185791015625, 1.0, 1.00000011920928955078125, 1.00000011920928955078125, 2) != 0.0 { return 2; }
                if dot64(1.0, 2.0, 3.0, 4.0, 3) != 15.0 { return 3; }
                if dot32(1.0, 2.0, 3.0, 4.0, 3) != 15.0 { return 4; }
                if overlap(3) != 20.0 { return 5; }
                if overlap(0) != 0.0 { return 6; }
                if dot64(1e308, 2.0, 1.0, 1.0, 0) != 0.0 { return 7; }
                if dot32(3e38, 2.0, 1.0, 1.0, -1) != 0.0 { return 8; }
            }
            if mode == 2 { dot64(1e308, 2.0, 1.0, 1.0, 3); }
            if mode == 3 { dot64(1e308, 1.0, 1e308, 1.0, 3); }
            if mode == 4 { dot32(3e38, 2.0, 1.0, 1.0, 3); }
            if mode == 5 { dot32(3e38, 1.0, 3e38, 1.0, 3); }
            if mode == 6 { dot64(1.0, 1.0, 1.0, 1.0, 4); }
            if mode == 7 { overlap(4); }
            if mode == 8 { effects(); }
            std.print("done;");
            return 42;
        }
    )");
    const auto executable = directory + "/reductions";
    const auto output = directory + "/native.out";
    const auto errors = directory + "/native.err";
    for (const std::string mode : {"--run", "-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        if (mode != "--run") {
            auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err << built.message;
        }
        for (unsigned count = 1; count <= 8; ++count) {
            SCOPED_TRACE(count);
            gloin_test::ProcessResult result{};
            if (mode == "--run") {
                std::vector<std::string> arguments{"--run", file, "--"};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result = invoke_raw(arguments);
            } else {
                std::vector<llvm::StringRef> arguments{executable};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output, errors};
                ASSERT_FALSE(llvm::sys::fs::remove(output));
                ASSERT_FALSE(llvm::sys::fs::remove(errors));
                result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt,
                    redirects, 10, 0, &result.message, &result.launch_failed);
                result.out = read(output);
                result.err = read(errors);
            }
            ASSERT_FALSE(result.launch_failed) << result.message;
            if (count == 1) {
                EXPECT_EQ(result.status, 42) << result.err << result.message;
                EXPECT_EQ(result.out, "done;");
            } else {
                EXPECT_LT(result.status, 0) << result.err << result.message;
                EXPECT_EQ(result.out, count == 8 ? "load;ok;load;" : "");
                EXPECT_EQ(result.message.find("timed out"), std::string::npos);
                EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                            result.message.find("Illegal instruction") != std::string::npos) << result.message;
            }
            EXPECT_TRUE(result.err.empty()) << result.err;
        }
    }
}

TEST_F(CliTest, GIRVersionedLoopsPreserveBoundsTrapOrderAndOrderedFloatResults) {
    const auto file = source(R"(
        import "@process";
        import "@std";
        def dot(a: [const f64], b: [const f64], n: i64) -> f64 {
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 1 {
                std.print("load;");
                total = total + a[i] * b[i];
                std.print("ok;");
            }
            return total;
        }
        def main() -> i32 {
            def mode: u64 = process.arg_count();
            def mut a: [f64; 3] = {1.0, 2.0, 3.0};
            def mut b: [f64; 3] = {1.0, 1.0, 1.0};
            if mode == 1 { if dot(a[..], b[..], 3) == 6.0 { return 42; } }
            if mode == 2 { dot(a[..], b[..], 4); }
            if mode == 3 { dot(a[..], b[..1], 3); }
            if mode == 4 { dot(a[..1], b[..], 3); }
            if mode == 5 {
                if dot(a[..0], b[..0], 0) == 0.0 { return 42; }
            }
            if mode == 6 {
                if dot(a[..0], b[..0], -1) == 0.0 { return 42; }
            }
            if mode == 7 {
                a[0] = 1e308;
                b[0] = 2.0;
                dot(a[..], b[..], 3);
            }
            if mode == 8 {
                a[0] = 1e308;
                a[1] = 1e308;
                a[2] = -1e308;
                dot(a[..], b[..], 3);
            }
            if mode == 9 {
                a[0] = 1e16;
                a[1] = -1e16;
                a[2] = 1.0;
                if dot(a[..], b[..], 3) == 1.0 { return 42; }
            }
            if mode == 10 {
                a[0] = -1.0000000000000004;
                a[1] = 1.0000000000000002;
                b[1] = 1.0000000000000002;
                if dot(a[..], b[..], 2) == 0.0 { return 42; }
            }
            if mode == 11 {
                // An out-of-range later index must not suppress earlier work
                // or move its failure ahead of this non-finite product.
                a[0] = 1e308;
                b[0] = 2.0;
                dot(a[..], b[..], 9223372036854775807);
            }
            if mode == 12 { dot(a[..0], b[..], 1); }
            return 1;
        }
    )");
    const auto executable = directory + "/versioned-loops";
    const auto output = directory + "/native.out";
    const auto errors = directory + "/native.err";
    const std::vector<std::string> traces{
        "load;ok;load;ok;load;ok;", "load;ok;load;ok;load;ok;load;",
        "load;ok;load;", "load;ok;load;", "", "", "load;", "load;ok;load;",
        "load;ok;load;ok;load;ok;", "load;ok;load;ok;", "load;", "load;"};
    for (const std::string mode : {"--run", "-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        if (mode != "--run") expect_success(invoke_raw({mode, "-o", executable, file}), "");
        for (unsigned count = 1; count <= traces.size(); ++count) {
            SCOPED_TRACE(count);
            gloin_test::ProcessResult result{};
            if (mode == "--run") {
                std::vector<std::string> arguments{"--run", file, "--"};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result = invoke_raw(arguments);
            } else {
                std::vector<llvm::StringRef> arguments{executable};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output, errors};
                // LLVM's redirect opens do not truncate an existing file.
                ASSERT_FALSE(llvm::sys::fs::remove(output));
                ASSERT_FALSE(llvm::sys::fs::remove(errors));
                result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt,
                    redirects, 10, 0, &result.message, &result.launch_failed);
                result.out = read(output);
                result.err = read(errors);
            }
            ASSERT_FALSE(result.launch_failed) << result.message;
            EXPECT_EQ(result.out, traces[count - 1]);
            if (count == 1 || count == 5 || count == 6 || count == 9 || count == 10)
                EXPECT_EQ(result.status, 42) << result.err << result.message;
            else {
                EXPECT_LT(result.status, 0) << result.err << result.message;
                EXPECT_EQ(result.message.find("timed out"), std::string::npos);
                EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                            result.message.find("Illegal instruction") != std::string::npos) << result.message;
            }
        }
    }
}

TEST_F(CliTest, GIRStridedLoopsPreserveBytesTailsAndOverlappingSlices) {
    const auto file = source(R"(
        def expand(input: [const u8], output: [u8]) -> u64 {
            def mut i: u64 = 0;
            def mut j: u64 = 0;
            while i + 3 <= input.len {
                def a: u8 = input[i];
                def b: u8 = input[i + 1];
                def c: u8 = input[i + 2];
                output[j] = a;
                output[j + 1] = b;
                output[j + 1 + 1] = c;
                output[j + 3] = 77;
                i = i + 3;
                j = j + 4;
            }
            while i < input.len {
                output[j] = input[i];
                i = i + 1;
                j = j + 1;
            }
            return j;
        }
        def main() -> i32 {
            def input: [u8; 20] = {0, 255, 128, 254, 127, 1, 2, 3, 4, 5,
                                   6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
            def mut output: [u8; 32] = {201; 32};
            def marker: u8 = 77;
            def sentinel: u8 = 201;
            for def mut n: u64 = 0; n <= 20; n = n + 1 {
                def needed: u64 = n + n / 3;
                for def mut spare: u64 = 0; spare < 2; spare = spare + 1 {
                    for def mut k: u64 = 0; k < 32; k = k + 1 { output[k] = sentinel; }
                    if expand(input[..n], output[1..needed + spare + 1]) != needed { return 1; }
                    def groups: u64 = n / 3;
                    for def mut k: u64 = 0; k < groups * 4; k = k + 1 {
                        if k % 4 == 3 {
                            if output[k + 1] != marker { return 2; }
                        } else {
                            if output[k + 1] != input[(k / 4) * 3 + k % 4] { return 3; }
                        }
                    }
                    for def mut k: u64 = 0; k < n % 3; k = k + 1 {
                        if output[groups * 4 + k + 1] != input[groups * 3 + k] { return 4; }
                    }
                    if output[0] != sentinel || output[needed + 1] != sentinel { return 5; }
                }
            }
            // The first group's fourth store changes the next group's input.
            // Versioning must not assume that source and destination are disjoint.
            def mut shared: [u8; 8] = {1, 2, 3, 4, 5, 6, 7, 8};
            def expected: [u8; 8] = {1, 2, 3, 77, 77, 5, 6, 77};
            if expand(shared[..6], shared[..]) != 8 { return 6; }
            for def mut k: u64 = 0; k < 8; k = k + 1 {
                if shared[k] != expected[k] { return 7; }
            }
            return 42;
        }
    )");
    expect_run(invoke_raw({"--run", file}), 42);
    const auto executable = directory + "/strided-bytes";
    for (const std::string mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        expect_success(invoke_raw({mode, "-o", executable, file}), "");
        std::string error;
        bool failed = false;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {},
                                           10, 0, &error, &failed), 42) << error;
        EXPECT_FALSE(failed) << error;
    }
}

TEST_F(CliTest, GIRStridedLoopsPreserveShortBufferAndOverflowTrapOrder) {
    const auto file = source(R"(
        import "@process";
        import "@std";
        def pack(output: [u8], n: u64, value: u8) -> u64 {
            def mut i: u64 = 0;
            def mut j: u64 = 0;
            while i + 3 <= n {
                std.print("begin;");
                def byte: u8 = value + 1;
                std.print("sum;");
                output[j] = byte; std.print("0;");
                output[j + 1] = byte; std.print("1;");
                output[j + 2] = byte; std.print("2;");
                output[j + 3] = byte; std.print("3;");
                i = i + 3;
                j = j + 4;
            }
            std.print("end;");
            return i + j;
        }
        def final_test(output: [u8], n: u64) -> void {
            def mut i: u64 = 0;
            def mut j: u64 = 0;
            // One iteration fits; the next header's i+S overflows.
            while i + 9223372036854775808 <= n {
                std.print("begin;");
                output[j] = 1;
                std.print("write;");
                i = i + 9223372036854775808;
                j = j + 1;
            }
            std.print("end;");
        }
        def secondary(output: [u8], n: u64) -> void {
            def mut i: u64 = 0;
            def mut j: u64 = 0;
            while i + 1 <= n {
                std.print("begin;");
                output[j] = 1;
                std.print("write;");
                i = i + 1;
                j = j + 18446744073709551615;
            }
        }
        def main() -> i32 {
            def mode: u64 = process.arg_count();
            def mut output: [u8; 8] = zeroed;
            if mode <= 9 {
                if pack(output[..mode - 1], 6, 1) == 14 { return 42; }
            }
            if mode == 10 { if pack(output[..0], 0, 1) == 0 { return 42; } }
            if mode == 11 { if pack(output[..0], 2, 1) == 0 { return 42; } }
            if mode == 12 { if pack(output[..4], 3, 1) == 7 { return 42; } }
            if mode == 13 { pack(output[..0], 6, 255); }
            if mode == 14 { pack(output[..], 6, 255); }
            if mode == 15 { final_test(output[..], 9223372036854775808); }
            if mode == 16 { final_test(output[..0], 9223372036854775808); }
            if mode == 17 { secondary(output[..], 1); return 42; }
            if mode == 18 { secondary(output[..], 2); }
            if mode == 19 { pack(output[..], 18446744073709551615, 1); }
            return 1;
        }
    )");
    const auto executable = directory + "/strided-traps";
    const auto output = directory + "/native.out";
    const auto errors = directory + "/native.err";
    std::vector<std::string> traces;
    const std::string group = "begin;sum;0;1;2;3;";
    for (unsigned capacity = 0; capacity < 8; ++capacity) {
        std::string trace = capacity >= 4 ? group : "";
        trace += "begin;sum;";
        for (unsigned k = 0; k < capacity % 4; ++k) trace += std::to_string(k) + ";";
        traces.push_back(trace);
    }
    traces.insert(traces.end(), {group + group + "end;", "end;", "end;", group + "end;",
        "begin;", "begin;", "begin;write;", "begin;", "begin;write;",
        "begin;write;begin;", group + group + "begin;sum;"});
    for (const std::string mode : {"--run", "-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        if (mode != "--run") expect_success(invoke_raw({mode, "-o", executable, file}), "");
        for (unsigned count = 1; count <= traces.size(); ++count) {
            SCOPED_TRACE(count);
            gloin_test::ProcessResult result{};
            if (mode == "--run") {
                std::vector<std::string> arguments{"--run", file, "--"};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                result = invoke_raw(arguments);
            } else {
                std::vector<llvm::StringRef> arguments{executable};
                for (unsigned i = 1; i < count; ++i) arguments.push_back("case");
                const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output, errors};
                ASSERT_FALSE(llvm::sys::fs::remove(output));
                ASSERT_FALSE(llvm::sys::fs::remove(errors));
                result.status = llvm::sys::ExecuteAndWait(executable, arguments, std::nullopt,
                    redirects, 10, 0, &result.message, &result.launch_failed);
                result.out = read(output);
                result.err = read(errors);
            }
            ASSERT_FALSE(result.launch_failed) << result.message;
            EXPECT_EQ(result.out, traces[count - 1]);
            if (count == 9 || count == 10 || count == 11 || count == 12 || count == 17)
                EXPECT_EQ(result.status, 42) << result.err << result.message;
            else {
                EXPECT_LT(result.status, 0) << result.err << result.message;
                EXPECT_EQ(result.message.find("timed out"), std::string::npos);
                EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                            result.message.find("Illegal instruction") != std::string::npos) << result.message;
            }
        }
    }
}
