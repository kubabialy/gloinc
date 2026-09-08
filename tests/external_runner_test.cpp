#include "support/external_runner.h"
#include "tool_paths.h"
#include <future>
#include <gtest/gtest.h>

namespace {
const gloin_test::ToolCommand copy_tool{gloin_test::process_fixture, {"copy"}};
const gloin_test::ToolCommand echo_tool{gloin_test::process_fixture, {"echo"}};

void expect_failure(llvm::Expected<int> result, const std::string &message) {
    ASSERT_FALSE(static_cast<bool>(result));
    const auto error = llvm::toString(result.takeError());
    EXPECT_NE(error.find(message), std::string::npos) << error;
}
} // namespace

TEST(ExternalRunnerTest, PreservesNegativeProgramResult) {
    auto result = gloin_test::run_external_mlir("-1\n", copy_tool, echo_tool);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, -1);
}

TEST(ExternalRunnerTest, RejectsOptimizerFailureDespiteNumericOutput) {
    expect_failure(
        gloin_test::run_external_mlir("42", {gloin_test::process_fixture, {"fail"}}, echo_tool),
        "intentional tool failure");
}

TEST(ExternalRunnerTest, RejectsRunnerFailureDespiteNumericOutput) {
    expect_failure(
        gloin_test::run_external_mlir("42", copy_tool, {gloin_test::process_fixture, {"fail"}}),
        "intentional tool failure");
}

TEST(ExternalRunnerTest, RejectsMalformedAndOutOfRangeOutput) {
    for (const auto *output : {"", "42garbage", "42\n17", "2147483648", "-2147483649"})
        expect_failure(gloin_test::run_external_mlir(output, copy_tool, echo_tool), "one i32");
}

TEST(ExternalRunnerTest, RejectsMissingTool) {
    expect_failure(
        gloin_test::run_external_mlir(
            "42", {std::string(gloin_test::process_fixture) + ".missing", {}}, echo_tool),
        "failed");
}

TEST(ExternalRunnerTest, TimesOutOptimizer) {
    expect_failure(
        gloin_test::run_external_mlir("42", {gloin_test::process_fixture, {"hang"}}, echo_tool, 1),
        "status -2");
}

TEST(ExternalRunnerTest, TimesOutRunner) {
    expect_failure(
        gloin_test::run_external_mlir("42", copy_tool, {gloin_test::process_fixture, {"hang"}}, 1),
        "status -2");
}

TEST(ExternalRunnerTest, IsolatesConcurrentInvocations) {
    auto first = std::async(std::launch::async, [] {
        return gloin_test::run_external_mlir("17", copy_tool, echo_tool);
    });
    auto second = std::async(std::launch::async, [] {
        return gloin_test::run_external_mlir("93", copy_tool, echo_tool);
    });
    auto a = first.get();
    auto b = second.get();
    ASSERT_TRUE(static_cast<bool>(a)) << llvm::toString(a.takeError());
    ASSERT_TRUE(static_cast<bool>(b)) << llvm::toString(b.takeError());
    EXPECT_EQ(*a, 17);
    EXPECT_EQ(*b, 93);
}
