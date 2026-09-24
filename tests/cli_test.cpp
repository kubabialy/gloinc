#include "codegen.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include <future>

namespace {
class CliTest : public gloin_test::CliFixture {};
} // namespace

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
        expect_success(invoke({option}), "gloinc 0.0.2 (LLVM/MLIR 21.1.6)\n");
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

TEST_F(CliTest, NativeObjectAndFailurePaths) {
    const auto file = source("def main() -> i32 { return 42; }");
    const auto object = directory + "/program.o";
    expect_success(invoke({"--emit-object", "-o", object, file}), "");
    auto buffer = llvm::MemoryBuffer::getFile(object);
    ASSERT_TRUE(buffer);
    EXPECT_GT((*buffer)->getBufferSize(), 4u);
    EXPECT_EQ((*buffer)->getBuffer().substr(0, 4), "\xcf\xfa\xed\xfe");
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
        EXPECT_NE(result.out.find("llvm.intr.trap"), std::string::npos);
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
