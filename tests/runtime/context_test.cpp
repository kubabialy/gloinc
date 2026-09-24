#include "context_runtime.h"
#include "context_runtime_internal.h"
#include "stdlib_runtime.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class ContextRuntimeTest : public testing::Test {
  protected:
    std::string directory;
    void SetUp() override {
        char pattern[] = "/tmp/gloinc-context-XXXXXX";
        auto *p = mkdtemp(pattern);
        ASSERT_NE(p, nullptr);
        directory = p;
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
    std::string file(const std::string &name, const std::string &data = "abc") {
        auto path = directory + "/" + name;
        std::ofstream out(path, std::ios::binary);
        out << data;
        return path;
    }
    void metadata(const std::string &path, int32_t expected, int32_t expected_kind,
                  uint64_t expected_size) {
        int32_t kind = 99, error = 99;
        uint64_t size = 99;
        EXPECT_EQ(gloin_fs_metadata(path.data(), path.size(), &kind, &size, &error), expected);
        EXPECT_EQ(kind, expected_kind);
        EXPECT_EQ(size, expected_size);
        if (expected == GLOIN_STD_OK || expected == GLOIN_STD_INVALID)
            EXPECT_EQ(error, 0);
        else
            EXPECT_NE(error, 0);
    }
};
struct CwdGuard {
    int original = ::open(".", O_RDONLY);
    ~CwdGuard() {
        if (original >= 0) {
            fchdir(original);
            ::close(original);
        }
    }
};
struct EnvGuard {
    std::string name, old;
    bool existed;
    explicit EnvGuard(std::string n)
        : name(std::move(n)), existed(std::getenv(name.c_str()) != nullptr) {
        if (existed)
            old = std::getenv(name.c_str());
    }
    ~EnvGuard() {
        if (existed)
            setenv(name.c_str(), old.c_str(), 1);
        else
            unsetenv(name.c_str());
    }
};
} // namespace
TEST_F(ContextRuntimeTest, MetadataReportsFilesDirectoriesSymlinksAndOtherKinds) {
    auto path = file("bytes", std::string("a\0b", 3));
    metadata(path, GLOIN_STD_OK, 1, 3);
    metadata(directory, GLOIN_STD_OK, 2, 0);
    auto link = directory + "/broken";
    ASSERT_EQ(symlink("absent", link.c_str()), 0);
    metadata(link, GLOIN_STD_OK, 3, 6);
    auto fifo = directory + "/pipe";
    ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);
    metadata(fifo, GLOIN_STD_OK, 4, 0);
    metadata(directory + "/missing", GLOIN_STD_NOT_FOUND, 0, 0);
}
TEST_F(ContextRuntimeTest, CountedPathsAndInvalidInputsDoNotTouchPrefixFiles) {
    auto path = file("keep");
    auto extended = path + "junk";
    int32_t kind, error;
    uint64_t size;
    EXPECT_EQ(gloin_fs_metadata(extended.data(), path.size(), &kind, &size, &error), GLOIN_STD_OK);
    EXPECT_EQ(size, 3u);
    auto nul = path + std::string("\0tail", 5);
    metadata(nul, GLOIN_STD_INVALID, 0, 0);
    metadata("", GLOIN_STD_INVALID, 0, 0);
    EXPECT_EQ(gloin_fs_remove_file(nul.data(), nul.size(), &error), GLOIN_STD_INVALID);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_fs_rename_replace(path.data(), path.size(), nul.data(), nul.size(), &error),
              GLOIN_STD_INVALID);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(gloin_fs_metadata("x", UINT64_MAX, &kind, &size, &error), GLOIN_STD_INVALID);
}
TEST_F(ContextRuntimeTest, MkdirUnlinkAndReplacementRenameHaveExplicitEffects) {
    auto dir = directory + "/child";
    int32_t error = 99;
    EXPECT_EQ(gloin_fs_mkdir(dir.data(), dir.size(), &error), GLOIN_STD_OK);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_fs_mkdir(dir.data(), dir.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    EXPECT_EQ(error, EEXIST);
    EXPECT_NE(gloin_fs_remove_file(dir.data(), dir.size(), &error), GLOIN_STD_OK);
    EXPECT_TRUE(std::filesystem::is_directory(dir));
    auto from = file("from", "new"), to = file("to", "old");
    EXPECT_EQ(gloin_fs_rename_replace(from.data(), from.size(), to.data(), to.size(), &error),
              GLOIN_STD_OK);
    EXPECT_FALSE(std::filesystem::exists(from));
    std::ifstream input(to);
    std::string text;
    input >> text;
    EXPECT_EQ(text, "new");
    auto link = directory + "/link";
    ASSERT_EQ(symlink(to.c_str(), link.c_str()), 0);
    EXPECT_EQ(gloin_fs_remove_file(link.data(), link.size(), &error), GLOIN_STD_OK);
    EXPECT_TRUE(std::filesystem::exists(to));
    EXPECT_EQ(gloin_fs_remove_file(to.data(), to.size(), &error), GLOIN_STD_OK);
    EXPECT_EQ(gloin_fs_remove_file(to.data(), to.size(), &error), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOENT);
}
TEST_F(ContextRuntimeTest, PermissionDeniedIsNotAbsence) {
    auto child = directory + "/private";
    ASSERT_EQ(mkdir(child.c_str(), 0700), 0);
    auto path = child + "/x";
    {
        std::ofstream f(path);
        f << "x";
    }
    ASSERT_EQ(chmod(child.c_str(), 0), 0);
    if (geteuid() != 0)
        metadata(path, GLOIN_STD_PERMISSION_DENIED, 0, 0);
    ASSERT_EQ(chmod(child.c_str(), 0700), 0);
    // NOT_FOUND from a non-directory path also preserves ENOTDIR.
    path += "/nested";
    int32_t kind, error;
    uint64_t size;
    EXPECT_EQ(gloin_fs_metadata(path.data(), path.size(), &kind, &size, &error),
              GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOTDIR);
}
TEST_F(ContextRuntimeTest, CArgumentsAreOwnedAndNestedScopesRestoreTheirPredecessors) {
    EXPECT_EQ(gloin_process_arg_count(), 0u);
    uint64_t length = 99;
    int32_t status = 99;
    EXPECT_EQ(gloin_process_arg(0, &length, &status), nullptr);
    EXPECT_EQ(status, GLOIN_STD_OUT_OF_RANGE);
    EXPECT_EQ(length, 0u);
    char value[] = "first";
    const char *outer_args[] = {value, ""};
    void *outer = gloin_process_arguments_push(2, outer_args);
    ASSERT_NE(outer, nullptr);
    value[0] = 'X';
    EXPECT_STREQ(gloin_process_arg(0, &length, &status), "first");
    EXPECT_EQ(length, 5u);
    EXPECT_EQ(status, GLOIN_STD_OK);
    EXPECT_STREQ(gloin_process_arg(1, &length, &status), "");
    EXPECT_EQ(length, 0u);
    const char *inner_args[] = {"inner"};
    void *inner = gloin_process_arguments_push(1, inner_args);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(gloin_process_arguments_pop(outer), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_process_arg_count(), 1u);
    EXPECT_EQ(gloin_process_arguments_pop(inner), GLOIN_STD_OK);
    EXPECT_EQ(gloin_process_arg_count(), 2u);
    EXPECT_EQ(gloin_process_arguments_pop(outer), GLOIN_STD_OK);
    EXPECT_EQ(gloin_process_arg_count(), 0u);
    EXPECT_EQ(gloin_process_arguments_pop(nullptr), GLOIN_STD_INVALID);
}
TEST_F(ContextRuntimeTest, InvalidBindingsPreserveContextAndThreadsAreIsolated) {
    std::vector<std::string> args{"host"};
    gloin::process::ArgumentsScope outer(args);
    ASSERT_TRUE(outer.valid());
    const char *invalid[] = {nullptr};
    EXPECT_EQ(gloin_process_arguments_push(1, invalid), nullptr);
    EXPECT_EQ(gloin_process_arguments_push(UINT64_MAX, invalid), nullptr);
    EXPECT_EQ(gloin_process_arg_count(), 1u);
    {
        gloin::process::ArgumentsScope bad(std::vector<std::string>{std::string("x\0y", 3)});
        EXPECT_FALSE(bad.valid());
        EXPECT_EQ(gloin_process_arg_count(), 1u);
    }
    auto task = [](std::string name) {
        std::vector<std::string> args{name};
        gloin::process::ArgumentsScope scope(args);
        if (!scope.valid())
            return false;
        args[0] = "changed";
        for (int i = 0; i < 1000; ++i) {
            uint64_t n;
            int32_t status;
            auto p = gloin_process_arg(0, &n, &status);
            if (status != 0 || std::string(p, n) != name)
                return false;
        }
        return true;
    };
    auto first = std::async(std::launch::async, task, "one"),
         second = std::async(std::launch::async, task, "two");
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
    uint64_t n;
    int32_t status;
    EXPECT_STREQ(gloin_process_arg(0, &n, &status), "host");
}
TEST_F(ContextRuntimeTest, EnvironmentPreservesMissingEmptyAndRawBytes) {
    EnvGuard env("GLOIN_SPEC030E_NATIVE");
    uint64_t length;
    int32_t status;
    unsetenv(env.name.c_str());
    EXPECT_EQ(gloin_process_env(env.name.data(), env.name.size(), &length, &status), nullptr);
    EXPECT_EQ(status, GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(length, 0u);
    setenv(env.name.c_str(), "", 1);
    auto value = gloin_process_env(env.name.data(), env.name.size(), &length, &status);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(status, GLOIN_STD_OK);
    EXPECT_EQ(length, 0u);
    std::string raw("a\xff", 2);
    setenv(env.name.c_str(), raw.c_str(), 1);
    value = gloin_process_env(env.name.data(), env.name.size(), &length, &status);
    EXPECT_EQ(std::string(value, length), raw);
    for (const auto &bad : std::vector<std::string>{"", "a=b", std::string("x\0y", 3)}) {
        EXPECT_EQ(gloin_process_env(bad.data(), bad.size(), &length, &status), nullptr);
        EXPECT_EQ(status, GLOIN_STD_INVALID);
        EXPECT_EQ(length, 0u);
    }
    auto extended = env.name + "extra";
    value = gloin_process_env(extended.data(), env.name.size(), &length, &status);
    EXPECT_EQ(std::string(value, length), raw);
}
TEST_F(ContextRuntimeTest, CwdBoundsAndGuardBytesNeverExposeTruncatedPaths) {
    CwdGuard cwd;
    ASSERT_GE(cwd.original, 0);
    ASSERT_EQ(chdir(directory.c_str()), 0);
    // macOS canonicalizes /tmp to /private/tmp; compare with actual host cwd.
    const auto expected = std::filesystem::current_path().string();
    std::vector<char> bytes(expected.size() + 3, '#');
    uint64_t length;
    int32_t error;
    EXPECT_EQ(gloin_process_cwd(bytes.data() + 1, expected.size() + 1, &length, &error),
              GLOIN_STD_OK);
    EXPECT_EQ(std::string(bytes.data() + 1, length), expected);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(bytes.front(), '#');
    EXPECT_EQ(bytes.back(), '#');
    EXPECT_EQ(bytes[length + 1], 0);
    EXPECT_EQ(gloin_process_cwd(bytes.data() + 1, expected.size(), &length, &error),
              GLOIN_STD_TOO_LONG);
    EXPECT_EQ(error, ERANGE);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(bytes[1], 0);
    EXPECT_EQ(gloin_process_cwd(bytes.data() + 1, 1, &length, &error), GLOIN_STD_TOO_LONG);
}
TEST_F(ContextRuntimeTest, DeletedWorkingDirectoryReturnsAnOsError) {
    CwdGuard cwd;
    ASSERT_GE(cwd.original, 0);
    auto gone = directory + "/gone";
    ASSERT_EQ(mkdir(gone.c_str(), 0700), 0);
    ASSERT_EQ(chdir(gone.c_str()), 0);
    ASSERT_EQ(rmdir(gone.c_str()), 0);
    char bytes[4096];
    uint64_t length = 99;
    int32_t error;
    EXPECT_EQ(gloin_process_cwd(bytes, sizeof(bytes), &length, &error), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOENT);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(bytes[0], 0);
}
