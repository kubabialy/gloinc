#pragma once
#include "tool_paths.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>

namespace gloin_test {
struct ProcessResult {
    int status;
    bool launch_failed;
    std::string message;
    std::string out;
    std::string err;
};
class CliFixture : public testing::Test {
  protected:
    std::string directory;
    std::atomic<unsigned> calls{0};
    void SetUp() override {
        llvm::SmallString<128> path;
        auto error = llvm::sys::fs::createUniqueDirectory("gloinc-cli", path);
        ASSERT_FALSE(error) << error.message();
        directory = path.str().str();
    }
    void TearDown() override {
        if (!directory.empty())
            EXPECT_FALSE(llvm::sys::fs::remove_directories(directory));
    }
    std::string source(const std::string &text, const std::string &name = "program.gloin") {
        const auto path = directory + "/" + name;
        std::ofstream stream(path, std::ios::binary);
        stream.write(text.data(), text.size());
        stream.close();
        EXPECT_TRUE(stream.good());
        return path;
    }
    std::string read(const std::string &path) {
        auto buffer = llvm::MemoryBuffer::getFile(path);
        EXPECT_TRUE(static_cast<bool>(buffer));
        return buffer ? (*buffer)->getBuffer().str() : "";
    }
    ProcessResult invoke(const std::vector<std::string> &arguments,
                         const std::string &stdin_path = "") {
        // Release validation reuses every CLI assertion against installed/extracted binaries.
        const char *override_path = std::getenv("GLOIN_TEST_CLI");
        const std::string executable = override_path ? override_path : gloin_test::gloinc;
        const auto prefix = directory + "/call-" + std::to_string(calls++);
        const auto stdout_path = prefix + ".out";
        const auto stderr_path = prefix + ".err";
        std::vector<llvm::StringRef> argv{executable};
        for (const auto &argument : arguments)
            argv.push_back(argument);
        const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(stdin_path), stdout_path,
                                                            stderr_path};
        ProcessResult result{};
        result.status = llvm::sys::ExecuteAndWait(executable, argv, std::nullopt, redirects, 10, 0,
                                                  &result.message, &result.launch_failed);
        result.out = read(stdout_path);
        result.err = read(stderr_path);
        EXPECT_FALSE(result.launch_failed) << result.message;
        return result;
    }
    void expect_run(const ProcessResult &result, int32_t value, const std::string &out = "") {
        EXPECT_EQ(result.status, static_cast<uint32_t>(value) & 0xff) << result.err << result.message;
        EXPECT_EQ(result.out, out);
        EXPECT_TRUE(result.err.empty()) << result.err;
    }
    void expect_success(const ProcessResult &result, const std::string &out) {
        EXPECT_EQ(result.status, 0) << result.err << result.message;
        EXPECT_EQ(result.out, out);
        EXPECT_TRUE(result.err.empty()) << result.err;
    }
    void expect_error(const ProcessResult &result, int status, const std::string &message) {
        EXPECT_EQ(result.status, status) << result.err << result.message;
        EXPECT_TRUE(result.out.empty()) << result.out;
        EXPECT_NE(result.err.find(message), std::string::npos) << result.err;
    }
};
} // namespace gloin_test
