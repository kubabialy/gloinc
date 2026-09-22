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
        for (const std::string word :
             {"Usage:", "--run", "--check", "--emit-ir", "--emit-llvm", "Exit status:"})
            EXPECT_NE(result.out.find(word), std::string::npos);
    }
    for (const std::string option : {"--version", "-V"})
        expect_success(invoke({option}), "gloinc 0.0.1-dev (LLVM/MLIR 21.1.6)\n");
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
                                               {"--run", "--check", "missing"},
                                               {"--emit-ir", "--emit-llvm", "missing"},
                                               {"--help", "missing"},
                                               {"--version", "--help"},
                                               {"--check=anything", "missing"}})
        expect_error(invoke(arguments), 2, "Usage:");
}

TEST_F(CliTest, InputFilesControlDefaultAndExplicitRun) {
    auto first = source("def main() -> i32 { return 17; }", "first.gloin");
    expect_success(invoke({first}), "17\n");
    expect_success(invoke({"--run", gloin_test::core_example}), "42\n");
}

TEST_F(CliTest, EveryI32ResultUsesStdoutAndSuccessStatus) {
    for (const std::string value : {"0", "-1", "2147483647", "-2147483648", "2"}) {
        auto file = source("def main() -> i32 { return " + value + "; }");
        expect_success(invoke({file}), value + "\n");
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
          "import \"@std\";", "def main() -> i32 { defer f(); return 0; }"}) {
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
    expect_success(invoke({"--", file}), "42\n");
    expect_success(invoke({file, "--run"}), "42\n");
    expect_error(invoke({"--", "--not-a-file"}), 1, "cannot read");
    expect_error(invoke({"--not-a-file"}), 2, "option");
    auto link = directory + "/linked.gloin";
    ASSERT_FALSE(llvm::sys::fs::create_link(file, link));
    expect_success(invoke({link}), "42\n");
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
        expect_success(invoke({file}), "3\n");
}

TEST_F(CliTest, ConcurrentInvocationsHaveIndependentOutput) {
    auto first = source("def main() -> i32 { return 17; }", "first.gloin");
    auto second = source("def main() -> i32 { return 93; }", "second.gloin");
    auto a = std::async(std::launch::async, [&] { return invoke({first}); });
    auto b = std::async(std::launch::async, [&] { return invoke({second}); });
    expect_success(a.get(), "17\n");
    expect_success(b.get(), "93\n");
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
