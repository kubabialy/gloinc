#include "context_runtime.h"
#include "context_runtime_internal.h"
#include "stdlib_runtime.h"
#include "tool_paths.h"
#include <array>
#include <atomic>
#include <pthread.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct ChildReaper {
    int64_t pid = 0;
    ~ChildReaper() {
        if (pid > 0) {
            kill(static_cast<pid_t>(pid), SIGKILL);
            while (waitpid(static_cast<pid_t>(pid), nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
};
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
TEST_F(ContextRuntimeTest, CheckedReplacementPreservesBytesOwnershipAndMode) {
    const std::string old("a\0b", 3), next("new\0bytes", 9);
    const auto path = file("replace", old);
    ASSERT_EQ(chmod(path.c_str(), 0751), 0);
    struct stat before{}, after{};
    ASSERT_EQ(lstat(path.c_str(), &before), 0);
    int32_t error = 99;
    ASSERT_EQ(gloin_fs_replace_file(path.data(), path.size(), old.data(), old.size(),
                                    next.data(), next.size(), &error), GLOIN_STD_OK);
    EXPECT_EQ(error, 0);
    ASSERT_EQ(lstat(path.c_str(), &after), 0);
    EXPECT_EQ(after.st_mode & 07777, 0751);
    EXPECT_EQ(after.st_uid, before.st_uid);
    EXPECT_EQ(after.st_gid, before.st_gid);
    EXPECT_NE(after.st_ino, before.st_ino);
    std::ifstream input(path, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), next);
    EXPECT_EQ(gloin_fs_replace_file(path.data(), path.size(), next.data(), next.size(),
                                    nullptr, 0, &error), GLOIN_STD_OK);
    EXPECT_EQ(std::filesystem::file_size(path), 0u);
    EXPECT_EQ(gloin_fs_replace_file(path.data(), path.size(), nullptr, 0, "x", 1, &error), GLOIN_STD_OK);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                            std::filesystem::directory_iterator()), 1);
}
TEST_F(ContextRuntimeTest, CheckedReplacementRejectsChangedAndUnsafeSources) {
    const auto path = file("source", "original"), hard = directory + "/hard",
               symbolic = directory + "/symbolic", fifo = directory + "/fifo";
    int32_t error = 0;
    auto replace = [&](const std::string &name, const char *expected) {
        return gloin_fs_replace_file(name.data(), name.size(), expected, std::strlen(expected), "next", 4, &error);
    };
    EXPECT_EQ(replace(path, "stale"), GLOIN_STD_INVALID);
    EXPECT_EQ(replace(path, "original-extra"), GLOIN_STD_INVALID);
    EXPECT_EQ(replace(path, "orig"), GLOIN_STD_INVALID);
    ASSERT_EQ(link(path.c_str(), hard.c_str()), 0);
    EXPECT_EQ(replace(path, "original"), GLOIN_STD_INVALID);
    ASSERT_EQ(unlink(hard.c_str()), 0);
    ASSERT_EQ(symlink("source", symbolic.c_str()), 0);
    EXPECT_NE(replace(symbolic, "original"), GLOIN_STD_OK);
    ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);
    EXPECT_EQ(replace(fifo, ""), GLOIN_STD_INVALID);
    EXPECT_NE(replace(directory, ""), GLOIN_STD_OK);
    EXPECT_NE(replace(path + "/", "original"), GLOIN_STD_OK);
    EXPECT_NE(replace(directory + "/.", ""), GLOIN_STD_OK);
    EXPECT_NE(replace(directory + "/..", ""), GLOIN_STD_OK);
    EXPECT_NE(replace(directory + "/missing", ""), GLOIN_STD_OK);
    EXPECT_EQ(replace(std::string("bad\0path", 8), ""), GLOIN_STD_INVALID);
    ASSERT_EQ(chmod(path.c_str(), 04700), 0);
    EXPECT_EQ(replace(path, "original"), GLOIN_STD_INVALID);
    ASSERT_EQ(chmod(path.c_str(), 0600), 0);
    std::ifstream input(path);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "original");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                            std::filesystem::directory_iterator()), 3);
}
TEST_F(ContextRuntimeTest, CheckedReplacementCleansPartialWritesAndRetainsOriginal) {
    const auto path = file("source", "original");
    const auto pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        signal(SIGXFSZ, SIG_IGN);
        const struct rlimit limit{32, 32};
        if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        const std::string replacement(4096, 'x');
        int32_t error = 0;
        const auto code = gloin_fs_replace_file(path.data(), path.size(), "original", 8,
                                                replacement.data(), replacement.size(), &error);
        _exit(code != GLOIN_STD_OK && error == EFBIG ? 0 : 3);
    }
    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    std::ifstream input(path);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "original");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                            std::filesystem::directory_iterator()), 1);
}
struct PipedChild {
    ChildReaper child;
    int32_t input = -1, output = -1, errors = -1;
    bool grouped = false;
    ~PipedChild() {
        if (grouped && child.pid > 0) {
            int32_t error = 0;
            if (gloin_process_group_signal(child.pid, 1, &error) == GLOIN_STD_CLOSED) child.pid = 0;
        }
        for (int fd : {input, output, errors}) if (fd >= 0) close(fd);
    }
    int32_t start_options(const char *executable, const GloinProcessArgument *args, uint64_t count,
                          const std::string &cwd, const GloinProcessArgument *environment,
                          uint64_t environment_count, bool inherit, bool group, uint64_t stack_bytes = 0) {
        int32_t error = 0;
        grouped = group;
        return gloin_process_start_options(executable, std::strlen(executable), cwd.data(), cwd.size(),
            args, count, environment, environment_count, inherit, 1, group, stack_bytes,
            &child.pid, &input, &output, &errors, &error);
    }
    int32_t start(const char *executable, const GloinProcessArgument *args = nullptr, uint64_t count = 0) {
        int32_t error = 0;
        return gloin_process_start_piped(executable, std::strlen(executable), args, count,
                                        &child.pid, &input, &output, &errors, &error);
    }
};
TEST_F(ContextRuntimeTest, ChildPipesAreNonblockingCloseOnExecAndPreserveBinaryBytes) {
    PipedChild pipes;
    ASSERT_EQ(pipes.start("/bin/cat"), GLOIN_STD_OK);
    for (int fd : {pipes.input, pipes.output, pipes.errors}) {
        ASSERT_GE(fd, 3);
        EXPECT_NE(fcntl(fd, F_GETFL) & O_NONBLOCK, 0);
        EXPECT_NE(fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
    }
    uint8_t bytes[] = {0, 255, 13, 10};
    uint8_t buffer[4] = {};
    uint64_t count = 99;
    int32_t error = 99, ready = 99;
    EXPECT_EQ(gloin_process_pipe_read(pipes.output, buffer, 4, &count, &error), GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(error, 0);
    ASSERT_EQ(gloin_process_pipe_write(pipes.input, bytes, 4, &count, &error), GLOIN_STD_OK);
    EXPECT_EQ(count, 4u);
    ASSERT_EQ(gloin_process_pipe_close(pipes.input, &error), GLOIN_STD_OK);
    pipes.input = -1;
    size_t received = 0;
    for (int attempt = 0; attempt < 50 && received < sizeof(buffer); ++attempt) {
        ASSERT_EQ(gloin_process_pipe_wait(-1, pipes.output, -1, 100, &ready, &error), GLOIN_STD_OK);
        if (!(ready & 2)) continue;
        auto code = gloin_process_pipe_read(pipes.output, buffer + received, sizeof(buffer) - received, &count, &error);
        ASSERT_TRUE(code == GLOIN_STD_OK || code == GLOIN_STD_WOULD_BLOCK);
        received += count;
    }
    EXPECT_EQ(received, sizeof(buffer));
    EXPECT_EQ(std::memcmp(bytes, buffer, sizeof(buffer)), 0);
    int32_t kind = 0, code = 0;
    ASSERT_EQ(gloin_process_wait(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    pipes.child.pid = 0;
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 0);
    ASSERT_EQ(gloin_process_pipe_wait(-1, pipes.output, pipes.errors, 1000, &ready, &error), GLOIN_STD_OK);
    EXPECT_EQ(ready, 6); // EOF must be reported even without POLLIN.
    EXPECT_EQ(gloin_process_pipe_read(pipes.output, buffer, 4, &count, &error), GLOIN_STD_EOF);
    EXPECT_EQ(count, 0u);
}
TEST_F(ContextRuntimeTest, ChildPipesFailedLaunchClosesEveryTemporaryDescriptor) {
    auto descriptors = [] {
        std::set<int> result;
        for (int fd = 0; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) >= 0) result.insert(fd);
        return result;
    };
    const auto before = descriptors();
    for (int i = 0; i < 30; ++i) {
        PipedChild pipes;
        EXPECT_EQ(pipes.start("/gloin-no-such-executable"), GLOIN_STD_NOT_FOUND);
        EXPECT_EQ(pipes.child.pid, 0);
        EXPECT_EQ(pipes.input, -1);
        EXPECT_EQ(pipes.output, -1);
        EXPECT_EQ(pipes.errors, -1);
    }
    EXPECT_EQ(descriptors(), before);
}
TEST_F(ContextRuntimeTest, ChildPipeBrokenWritePreservesSignalMaskAndPendingSigpipe) {
    // Run signal-policy assertions in a subprocess so a regression cannot kill
    // the test runner or alter its policy. The parent asserts normal exit.
    pid_t tester = fork();
    ASSERT_GE(tester, 0);
    if (tester == 0) {
        struct sigaction defaults {};
        defaults.sa_handler = SIG_DFL;
        sigemptyset(&defaults.sa_mask);
        sigaction(SIGPIPE, &defaults, nullptr);
        sigset_t mask, pending;
        sigemptyset(&mask);
        pthread_sigmask(SIG_SETMASK, &mask, nullptr);
        std::atomic<bool> done{false};
        std::thread other([&] { while (!done.load()) std::this_thread::yield(); });
        int fds[2];
        if (pipe(fds)) _exit(1);
        close(fds[0]);
        uint8_t byte = 1;
        uint64_t count = 99;
        int32_t error = 99;
        if (gloin_process_pipe_write(fds[1], &byte, 1, &count, &error) != GLOIN_STD_EOF || count || error) _exit(2);
        pthread_sigmask(SIG_SETMASK, nullptr, &mask);
        if (sigismember(&mask, SIGPIPE)) _exit(3);
        sigaddset(&mask, SIGPIPE);
        pthread_sigmask(SIG_SETMASK, &mask, nullptr);
        raise(SIGPIPE);
        if (gloin_process_pipe_write(fds[1], &byte, 1, &count, &error) != GLOIN_STD_EOF) _exit(4);
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) != 1) _exit(5);
        pthread_sigmask(SIG_SETMASK, nullptr, &mask);
        if (sigismember(&mask, SIGPIPE) != 1) _exit(6);
        int signal = 0;
        sigwait(&mask, &signal);
        defaults.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &defaults, nullptr);
        sigemptyset(&mask);
        pthread_sigmask(SIG_SETMASK, &mask, nullptr);
        if (gloin_process_pipe_write(fds[1], &byte, 1, &count, &error) != GLOIN_STD_EOF) _exit(7);
        close(fds[1]);
        done = true;
        other.join();
        _exit(0);
    }
    int status = 0;
    ASSERT_EQ(waitpid(tester, &status, 0), tester);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(ContextRuntimeTest, ChildPipesWorkWhenHostStandardDescriptorsAreClosed) {
    pid_t tester = fork();
    ASSERT_GE(tester, 0);
    if (tester == 0) {
        close(0);
        close(1);
        close(2);
        PipedChild pipes;
        if (pipes.start("/bin/cat") != GLOIN_STD_OK) _exit(1);
        if (pipes.input < 3 || pipes.output < 3 || pipes.errors < 3) _exit(2);
        uint8_t byte = 42, received = 0;
        uint64_t count = 0;
        int32_t error = 0, ready = 0;
        if (gloin_process_pipe_write(pipes.input, &byte, 1, &count, &error) != GLOIN_STD_OK || count != 1) _exit(3);
        if (gloin_process_pipe_close(pipes.input, &error) != GLOIN_STD_OK) _exit(4);
        pipes.input = -1;
        if (gloin_process_pipe_wait(-1, pipes.output, -1, 5000, &ready, &error) != GLOIN_STD_OK || !(ready & 2)) _exit(5);
        if (gloin_process_pipe_read(pipes.output, &received, 1, &count, &error) != GLOIN_STD_OK || count != 1 || received != byte) _exit(6);
        int32_t kind = 0, code = 0;
        if (gloin_process_wait(pipes.child.pid, 1, &kind, &code, &error) != GLOIN_STD_OK || kind != 1 || code != 0) _exit(7);
        _exit(0);
    }
    int status = 0;
    ASSERT_EQ(waitpid(tester, &status, 0), tester);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(ContextRuntimeTest, ChildPipeWaitValidatesInputsAndReportsInvalidDescriptors) {
    int32_t ready = 99, error = 99;
    EXPECT_EQ(gloin_process_pipe_wait(-1, -1, -1, -1, &ready, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(ready, 0);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_process_pipe_wait(-1, -1, -1, 0, &ready, &error), GLOIN_STD_OK);
    EXPECT_EQ(ready, 0);
    int fd = open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    close(fd);
    EXPECT_NE(gloin_process_pipe_wait(-1, fd, -1, 0, &ready, &error), GLOIN_STD_OK);
    EXPECT_EQ(error, EBADF);
    uint8_t byte = 0;
    uint64_t count = 99;
    EXPECT_EQ(gloin_process_pipe_read(-1, &byte, 1, &count, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(gloin_process_pipe_write(0, &byte, 0, &count, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
}
TEST_F(ContextRuntimeTest, ChildLaunchPreservesArgumentBytesAndNonzeroExit) {
    const char script[] = "test \"$#\" = 3 && test \"$1\" = '' && test \"$2\" = 'a b' && "
                          "test \"$3\" = '$(literal);*' || exit 91; exit 37";
    const GloinProcessArgument args[] = {{"-c", 2}, {script, sizeof(script) - 1},
                                        {"fixture", 7}, {nullptr, 0}, {"a b", 3},
                                        {"$(literal);*", 12}};
    ChildReaper child;
    int32_t error = 99;
    ASSERT_EQ(gloin_process_start("/bin/sh", 7, args, 6, &child.pid, &error), GLOIN_STD_OK);
    EXPECT_EQ(error, 0);
    int32_t kind = 99, code = 99;
    ASSERT_EQ(gloin_process_wait(child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 37);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_process_wait(child.pid, 0, &kind, &code, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(error, ECHILD);
    EXPECT_EQ(kind, 0);
    EXPECT_EQ(code, 0);
    child.pid = 0;
}
TEST_F(ContextRuntimeTest, ChildLaunchRejectsInvalidInputsAndReportsHostFailures) {
    int64_t pid = 99;
    int32_t error = 99;
    const GloinProcessArgument bad[] = {{"a\0b", 3}};
    EXPECT_EQ(gloin_process_start("/bin/sh", 7, bad, 1, &pid, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(pid, 0);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_process_start("/bin/sh", 7, nullptr, 1, &pid, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_process_start("", 0, nullptr, 0, &pid, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_process_start("x\0y", 3, nullptr, 0, &pid, &error), GLOIN_STD_INVALID);
    const auto missing = directory + "/missing";
    EXPECT_EQ(gloin_process_start(missing.data(), missing.size(), nullptr, 0, &pid, &error),
              GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(pid, 0);
    EXPECT_EQ(error, ENOENT);
    const auto denied = file("not executable");
    EXPECT_EQ(gloin_process_start(denied.data(), denied.size(), nullptr, 0, &pid, &error),
              GLOIN_STD_PERMISSION_DENIED);
    EXPECT_EQ(pid, 0);
    EXPECT_EQ(error, EACCES);
    int32_t kind = 99, code = 99;
    EXPECT_EQ(gloin_process_wait(-1, 1, &kind, &code, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_process_signal(0, 1, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_process_signal(-1, 1, &error), GLOIN_STD_INVALID);
}
TEST_F(ContextRuntimeTest, ChildLaunchClosesNonstandardDescriptors) {
    const auto path = file("private descriptor");
    const int original = open(path.c_str(), O_RDONLY);
    ASSERT_GE(original, 0);
    const int descriptor = fcntl(original, F_DUPFD, 64);
    close(original);
    ASSERT_GE(descriptor, 64);
    const std::string script = "test ! -e /dev/fd/" + std::to_string(descriptor);
    const GloinProcessArgument args[] = {{"-c", 2}, {script.data(), script.size()}};
    ChildReaper child;
    int32_t error = 0;
    const auto launched = gloin_process_start("/bin/sh", 7, args, 2, &child.pid, &error);
    close(descriptor);
    ASSERT_EQ(launched, GLOIN_STD_OK);
    int32_t kind = 0, code = 0;
    ASSERT_EQ(gloin_process_wait(child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    child.pid = 0;
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 0);
}
TEST_F(ContextRuntimeTest, ChildPollAndForcedTerminationReapExactlyOnce) {
    const GloinProcessArgument args[] = {{"30", 2}};
    ChildReaper child;
    int32_t error = 0;
    ASSERT_EQ(gloin_process_start("/bin/sleep", 10, args, 1, &child.pid, &error), GLOIN_STD_OK);
    int32_t kind = 99, code = 99;
    ASSERT_EQ(gloin_process_wait(child.pid, 0, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 0);
    EXPECT_EQ(code, 0);
    ASSERT_EQ(gloin_process_signal(child.pid, 1, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_process_wait(child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    child.pid = 0;
    EXPECT_EQ(kind, 2);
    EXPECT_EQ(code, SIGKILL);
}
TEST_F(ContextRuntimeTest, ChildLaunchResetsIgnoredSignalsAndBlockedMask) {
    struct sigaction old_action {}, ignored {};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    ASSERT_EQ(sigaction(SIGTERM, &ignored, &old_action), 0);
    sigset_t blocked, old_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    const int mask_error = pthread_sigmask(SIG_BLOCK, &blocked, &old_mask);
    ChildReaper child;
    int32_t error = 0;
    const GloinProcessArgument args[] = {{"30", 2}};
    const auto launched = gloin_process_start("/bin/sleep", 10, args, 1, &child.pid, &error);
    sigaction(SIGTERM, &old_action, nullptr);
    if (!mask_error) pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
    ASSERT_EQ(mask_error, 0);
    ASSERT_EQ(launched, GLOIN_STD_OK);
    ASSERT_EQ(gloin_process_signal(child.pid, 0, &error), GLOIN_STD_OK);
    int32_t kind = 0, code = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!kind && std::chrono::steady_clock::now() < deadline) {
        ASSERT_EQ(gloin_process_wait(child.pid, 0, &kind, &code, &error), GLOIN_STD_OK);
        if (!kind) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (kind) child.pid = 0;
    EXPECT_EQ(kind, 2);
    EXPECT_EQ(code, SIGTERM);
}
TEST_F(ContextRuntimeTest, ChildLaunchRejectsAutoReapingHosts) {
    struct sigaction old_action {}, ignored {};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    ASSERT_EQ(sigaction(SIGCHLD, &ignored, &old_action), 0);
    ChildReaper child;
    int32_t error = 99;
    const GloinProcessArgument args[] = {{"30", 2}};
    const auto launched = gloin_process_start("/bin/sleep", 10, args, 1, &child.pid, &error);
    sigaction(SIGCHLD, &old_action, nullptr);
    EXPECT_EQ(launched, GLOIN_STD_INVALID);
    EXPECT_EQ(child.pid, 0);
    EXPECT_EQ(error, 0);
}
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
TEST_F(ContextRuntimeTest, ChildOptionsReplaceEnvironmentAndCwdWithoutMutatingParent) {
    EnvGuard parent("GLOIN_PARENT_ONLY");
    ASSERT_EQ(setenv(parent.name.c_str(), "parent", 1), 0);
    const auto cwd = std::filesystem::current_path();
    const auto executable = directory + "/runner with spaces";
    ASSERT_EQ(symlink("/bin/sh", executable.c_str()), 0);
    const std::string script = "test \"$GLOIN_VALUE\" = 'a b=café' && test \"${GLOIN_EMPTY+x}\" = x && "
        "test -z \"$GLOIN_EMPTY\" && test -z \"${GLOIN_PARENT_ONLY+x}\" || exit 91; pwd -P; exit 37";
    const GloinProcessArgument args[] = {{"-c", 2}, {script.data(), script.size()}};
    const std::string value = "GLOIN_VALUE=a b=café";
    const GloinProcessArgument environment[] = {{value.data(), value.size()}, {"GLOIN_EMPTY=", 12}};
    PipedChild pipes;
    ASSERT_EQ(pipes.start_options("./runner with spaces", args, 2, directory, environment, 2, false, true), GLOIN_STD_OK);
    int32_t kind = 0, code = 0, error = 0;
    ASSERT_EQ(gloin_process_observe(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 37);
    uint8_t bytes[4096];
    uint64_t count = 0;
    ASSERT_EQ(gloin_process_pipe_read(pipes.output, bytes, sizeof(bytes), &count, &error), GLOIN_STD_OK);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes), count), std::filesystem::canonical(directory).string() + "\n");
    EXPECT_EQ(std::filesystem::current_path(), cwd);
    EXPECT_STREQ(std::getenv(parent.name.c_str()), "parent");
    EXPECT_EQ(gloin_process_group_signal(pipes.child.pid, 1, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_process_wait(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    pipes.child.pid = 0;
    EXPECT_EQ(code, 37);
}
struct SignalSettings {
    struct sigaction action{};
    sigset_t mask{};
    bool active = false;
    bool prepare() {
        struct sigaction ignored{};
        ignored.sa_handler = SIG_IGN;
        sigemptyset(&ignored.sa_mask);
        if (sigaction(SIGUSR1, &ignored, &action)) return false;
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGUSR2);
        if (pthread_sigmask(SIG_BLOCK, &blocked, &mask)) { sigaction(SIGUSR1, &action, nullptr); return false; }
        active = true;
        return true;
    }
    ~SignalSettings() {
        if (active) { sigaction(SIGUSR1, &action, nullptr); pthread_sigmask(SIG_SETMASK, &mask, nullptr); }
    }
};
TEST_F(ContextRuntimeTest, ChildStackLimitPreservesParentAndInheritedHardLimit) {
    struct rlimit before{}, after{};
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &before), 0);
    SignalSettings signals;
    ASSERT_TRUE(signals.prepare());
    // Fork the main thread while another host thread continuously uses its heap.
    struct AllocatingThread {
        std::atomic<bool> stop{false};
        std::thread worker{[this] {
            while (!stop.load()) {
                std::vector<char> bytes(4096, 'x');
                std::this_thread::yield();
            }
        }};
        ~AllocatingThread() { stop = true; worker.join(); }
    } allocator;
    const int fd = open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 3);
    const std::string hard = std::to_string(before.rlim_max), descriptor = std::to_string(fd);
    for (const uint64_t limit : {uint64_t(0), uint64_t(2097152)}) {
        const std::string soft = std::to_string(limit ? limit : before.rlim_cur);
        const GloinProcessArgument args[] = {{"limits", 6}, {soft.data(), soft.size()},
            {hard.data(), hard.size()}, {descriptor.data(), descriptor.size()}, {"1", 1}};
        PipedChild pipes;
        ASSERT_EQ(pipes.start_options(gloin_test::process_fixture, args, 5, "", nullptr, 0, true, true, limit), GLOIN_STD_OK);
        int32_t kind = 0, code = 0, error = 0;
        ASSERT_EQ(gloin_process_observe(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
        EXPECT_EQ(kind, 1); EXPECT_EQ(code, 37);
        uint8_t bytes[32]{};
        uint64_t count = 0;
        ASSERT_EQ(gloin_process_pipe_read(pipes.output, bytes, sizeof bytes, &count, &error), GLOIN_STD_OK);
        EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes), count), "limits verified\n");
        EXPECT_GE(fcntl(fd, F_GETFD), 0);
    }
    close(fd);
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &after), 0);
    EXPECT_EQ(after.rlim_cur, before.rlim_cur); EXPECT_EQ(after.rlim_max, before.rlim_max);
    struct sigaction action{};
    sigset_t mask;
    ASSERT_EQ(sigaction(SIGUSR1, nullptr, &action), 0);
    EXPECT_EQ(action.sa_handler, SIG_IGN);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, nullptr, &mask), 0);
    EXPECT_EQ(sigismember(&mask, SIGUSR2), 1);
}
TEST_F(ContextRuntimeTest, ChildStackLimitRejectsSetupFailuresWithoutLeakingChildrenOrDescriptors) {
    auto descriptors = [] {
        std::set<int> result;
        for (int fd = 0; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) >= 0) result.insert(fd);
        return result;
    };
    const auto before = descriptors();
    const auto invalid_image = file("not-an-executable", "invalid image");
    ASSERT_EQ(chmod(invalid_image.c_str(), 0700), 0);
    for (const uint64_t limit : {uint64_t(2097152), UINT64_MAX, uint64_t(INT64_MAX)}) {
        for (const auto &executable : {directory + "/missing", invalid_image, directory}) {
            PipedChild pipes;
            EXPECT_NE(pipes.start_options(executable.c_str(), nullptr, 0, "", nullptr, 0, true, true, limit), GLOIN_STD_OK);
            EXPECT_EQ(pipes.child.pid, 0); EXPECT_EQ(pipes.input, -1);
            EXPECT_EQ(pipes.output, -1); EXPECT_EQ(pipes.errors, -1);
        }
    }
    PipedChild cwd;
    EXPECT_EQ(cwd.start_options("/bin/true", nullptr, 0, directory + "/absent", nullptr, 0, true, false, 2097152), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(cwd.child.pid, 0); EXPECT_EQ(cwd.input, -1);
    EXPECT_EQ(descriptors(), before);
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
    // A successful exec whose program exits 127 is still a normal child result.
    const GloinProcessArgument args[] = {{"-c", 2}, {"exit 127", 8}};
    PipedChild exited;
    ASSERT_EQ(exited.start_options("/bin/sh", args, 2, "", nullptr, 0, true, false, 2097152), GLOIN_STD_OK);
    int32_t kind = 0, code = 0, error = 0;
    ASSERT_EQ(gloin_process_wait(exited.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    exited.child.pid = 0;
    EXPECT_EQ(kind, 1); EXPECT_EQ(code, 127);
}
TEST_F(ContextRuntimeTest, ChildStackLimitHonorsHardCeilingAndClosesDescriptorsAboveSoftLimit) {
    for (const std::string mode : {"limited-hard", "limited-low-fd"}) {
        const GloinProcessArgument args[] = {{mode.data(), mode.size()}};
        PipedChild probe;
        ASSERT_EQ(probe.start(gloin_test::process_fixture, args, 1), GLOIN_STD_OK);
        int32_t kind = 0, code = 0, error = 0;
        ASSERT_EQ(gloin_process_wait(probe.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
        probe.child.pid = 0;
        EXPECT_EQ(kind, 1) << mode; EXPECT_EQ(code, 0) << mode;
    }
}
TEST_F(ContextRuntimeTest, ChildStackLimitHostThreadsFollowPlatformContract) {
    struct rlimit before{}, after{};
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &before), 0);
    const std::string hard = std::to_string(before.rlim_max);
    std::vector<std::future<int>> workers;
    for (int i = 0; i < 4; ++i) {
        workers.push_back(std::async(std::launch::async, [&, i] {
            const uint64_t limit = i % 2 ? 2097152 : 0;
            const std::string soft = std::to_string(limit ? limit : before.rlim_cur);
            const GloinProcessArgument args[] = {{"limits", 6}, {soft.data(), soft.size()},
                {hard.data(), hard.size()}, {"-1", 2}, {"0", 1}};
            PipedChild child;
            int64_t pid = 0;
            int32_t detail = 0;
            const auto started = gloin_process_start_options(gloin_test::process_fixture,
                std::strlen(gloin_test::process_fixture), nullptr, 0, args, 5, nullptr, 0,
                1, 1, 0, limit, &pid, &child.input, &child.output, &child.errors, &detail);
#ifdef __APPLE__
            if (limit) return started == GLOIN_STD_INVALID && detail == ENOTSUP && pid == 0
                && child.input == -1 && child.output == -1 && child.errors == -1 ? 37 : -4;
#endif
            if (started != GLOIN_STD_OK) return -1000 - detail;
            child.child.pid = pid;
            int32_t kind = 0, code = 0, error = 0;
            if (gloin_process_wait(child.child.pid, 1, &kind, &code, &error) != GLOIN_STD_OK) return -2;
            child.child.pid = 0;
            return kind == 1 ? code : -3;
        }));
    }
    for (auto &worker : workers) EXPECT_EQ(worker.get(), 37);
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &after), 0);
    EXPECT_EQ(after.rlim_cur, before.rlim_cur); EXPECT_EQ(after.rlim_max, before.rlim_max);
}
TEST_F(ContextRuntimeTest, ChildOptionsEmptyEnvironmentAndInvalidEntries) {
    PipedChild pipes;
    ASSERT_EQ(pipes.start_options("/usr/bin/env", nullptr, 0, "", nullptr, 0, false, false), GLOIN_STD_OK);
    int32_t kind = 0, code = 0, error = 0;
    ASSERT_EQ(gloin_process_wait(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    pipes.child.pid = 0;
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 0);
    uint8_t byte = 0;
    uint64_t count = 0;
    EXPECT_EQ(gloin_process_pipe_read(pipes.output, &byte, 1, &count, &error), GLOIN_STD_EOF);
    for (const std::string invalid : std::vector<std::string>{"", "KEY", "=value", std::string("KEY=v\0x", 7)}) {
        const GloinProcessArgument entry{invalid.data(), invalid.size()};
        PipedChild rejected;
        EXPECT_EQ(rejected.start_options("/bin/true", nullptr, 0, "", &entry, 1, false, true), GLOIN_STD_INVALID);
        EXPECT_EQ(rejected.child.pid, 0);
        EXPECT_EQ(rejected.input, -1);
    }
    const GloinProcessArgument duplicates[] = {{"KEY=a", 5}, {"KEY=b", 5}};
    PipedChild rejected;
    EXPECT_EQ(rejected.start_options("/bin/true", nullptr, 0, "", duplicates, 2, false, true), GLOIN_STD_INVALID);
    EXPECT_EQ(rejected.start_options("/bin/true", nullptr, 0, "", duplicates, 1, true, true), GLOIN_STD_INVALID);
    EXPECT_EQ(rejected.start_options("/bin/true", nullptr, 0, std::string("bad\0cwd", 7), nullptr, 0, true, true), GLOIN_STD_INVALID);
}
TEST_F(ContextRuntimeTest, ChildGroupRetainsExitedLeaderAndClosesDescendantPipes) {
    const auto parent_group = getpgrp();
    const std::string script = "/bin/sleep 30 & printf held; exit 37";
    const GloinProcessArgument args[] = {{"-c", 2}, {script.data(), script.size()}};
    PipedChild pipes;
    ASSERT_EQ(pipes.start_options("/bin/sh", args, 2, "", nullptr, 0, true, true), GLOIN_STD_OK);
    ChildReaper unrelated;
    const GloinProcessArgument delay[] = {{"30", 2}};
    int32_t error = 0, kind = 0, code = 0;
    ASSERT_EQ(gloin_process_start("/bin/sleep", 10, delay, 1, &unrelated.pid, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_process_observe(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 37);
    uint8_t bytes[16];
    uint64_t count = 0;
    ASSERT_EQ(gloin_process_pipe_read(pipes.output, bytes, sizeof(bytes), &count, &error), GLOIN_STD_OK);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes), count), "held");
    EXPECT_EQ(gloin_process_pipe_read(pipes.output, bytes, sizeof(bytes), &count, &error), GLOIN_STD_WOULD_BLOCK);
    ASSERT_EQ(gloin_process_group_signal(pipes.child.pid, 1, &error), GLOIN_STD_OK);
    bool eof = false;
    for (int i = 0; i < 50 && !eof; ++i) {
        int32_t ready = 0;
        ASSERT_EQ(gloin_process_pipe_wait(-1, pipes.output, -1, 100, &ready, &error), GLOIN_STD_OK);
        if (ready & 2) eof = gloin_process_pipe_read(pipes.output, bytes, sizeof(bytes), &count, &error) == GLOIN_STD_EOF;
    }
    EXPECT_TRUE(eof); // The descendant cannot retain the output pipe after cleanup.
    ASSERT_EQ(gloin_process_observe(pipes.child.pid, 0, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(code, 37); // The original leader's exit remains available.
    ASSERT_EQ(gloin_process_wait(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    pipes.child.pid = 0;
    EXPECT_EQ(code, 37);
    ASSERT_EQ(gloin_process_wait(unrelated.pid, 0, &kind, &code, &error), GLOIN_STD_OK);
    EXPECT_EQ(kind, 0);
    EXPECT_EQ(getpgrp(), parent_group);
}
TEST_F(ContextRuntimeTest, ChildGroupRefusesSignalsAfterExternalReap) {
    for (int64_t invalid : {-1, 0, 1}) {
        int32_t detail = 99;
        EXPECT_EQ(gloin_process_group_signal(invalid, 1, &detail), GLOIN_STD_INVALID);
        EXPECT_EQ(detail, 0);
    }
    PipedChild pipes;
    ASSERT_EQ(pipes.start_options("/usr/bin/true", nullptr, 0, "", nullptr, 0, true, true), GLOIN_STD_OK);
    int32_t kind = 0, code = 0, error = 0;
    const int64_t pid = pipes.child.pid;
    ASSERT_EQ(gloin_process_wait(pid, 1, &kind, &code, &error), GLOIN_STD_OK);
    pipes.child.pid = 0;
    EXPECT_EQ(gloin_process_group_signal(pid, 1, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(error, ECHILD);
}
TEST_F(ContextRuntimeTest, ChildOptionsFailedCwdLaunchReleasesDescriptors) {
    auto descriptors = [] {
        std::set<int> result;
        for (int fd = 0; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) >= 0) result.insert(fd);
        return result;
    };
    const auto before = descriptors();
    for (int i = 0; i < 10; ++i) {
        PipedChild pipes;
        const auto launched = pipes.start_options("/bin/sh", nullptr, 0, directory + "/absent", nullptr, 0, true, true);
        if (launched == GLOIN_STD_OK) {
            // POSIX permits a setup failure to appear as child exit 127.
            int32_t kind = 0, code = 0, error = 0;
            EXPECT_EQ(gloin_process_observe(pipes.child.pid, 1, &kind, &code, &error), GLOIN_STD_OK);
            EXPECT_EQ(kind, 1);
            EXPECT_EQ(code, 127);
        } else {
            EXPECT_EQ(launched, GLOIN_STD_NOT_FOUND);
            EXPECT_EQ(pipes.child.pid, 0);
            EXPECT_EQ(pipes.input, -1);
            EXPECT_EQ(pipes.output, -1);
            EXPECT_EQ(pipes.errors, -1);
        }
    }
    EXPECT_EQ(descriptors(), before);
}
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
TEST_F(ContextRuntimeTest, SymlinksPreserveCountedBytesAndNeverReplaceEntries) {
    int32_t error = 99;
    const auto target = file("target"), link = directory + "/link";
    const std::string text = "./target", padded = text + "ignored", name = link + "ignored";
    ASSERT_EQ(gloin_fs_symlink(padded.data(), text.size(), name.data(), link.size(), &error), GLOIN_STD_OK);
    EXPECT_EQ(error, 0);
    EXPECT_TRUE(std::filesystem::equivalent(target, link));
    EXPECT_EQ(gloin_fs_symlink("absent", 6, link.data(), link.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    EXPECT_EQ(error, EEXIST);
    EXPECT_EQ(gloin_fs_symlink("absent", 6, target.data(), target.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    EXPECT_EQ(gloin_fs_symlink("absent", 6, directory.data(), directory.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    const auto broken = directory + "/broken", cycle = directory + "/cycle", raw = directory + "/raw";
    const std::string raw_text = std::string("\xff", 1) + " target";
    for (const auto &[path, value] : std::vector<std::pair<std::string, std::string>>{
             {broken, "absent"}, {cycle, "cycle"}, {raw, raw_text}}) {
        ASSERT_EQ(gloin_fs_symlink(value.data(), value.size(), path.data(), path.size(), &error), GLOIN_STD_OK);
        std::array<uint8_t, 32> buffer;
        buffer.fill(0xa5);
        uint64_t length = 99;
        ASSERT_EQ(gloin_fs_read_link(path.data(), path.size(), buffer.data(), buffer.size(), &length, &error), GLOIN_STD_OK);
        EXPECT_EQ(error, 0);
        EXPECT_EQ(std::string(reinterpret_cast<char *>(buffer.data()), length), value);
        EXPECT_EQ(buffer[length], 0xa5); // Counted bytes; the ABI adds no NUL.
    }
    EXPECT_EQ(gloin_fs_symlink("another", 7, broken.data(), broken.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    EXPECT_EQ(gloin_fs_remove_file(link.data(), link.size(), &error), GLOIN_STD_OK);
    EXPECT_TRUE(std::filesystem::exists(target));
}
TEST_F(ContextRuntimeTest, ReadLinkDetectsExactAndOverflowBoundsWithoutWritingPastCapacity) {
    const auto link = directory + "/link";
    int32_t error = 99;
    ASSERT_EQ(gloin_fs_symlink("target", 6, link.data(), link.size(), &error), GLOIN_STD_OK);
    for (uint64_t capacity : {1u, 6u, 7u, 8u}) {
        std::array<uint8_t, 10> buffer;
        buffer.fill(0xa5);
        uint64_t length = 99;
        EXPECT_EQ(gloin_fs_read_link(link.data(), link.size(), buffer.data() + 1, capacity, &length, &error),
                  capacity <= 6 ? GLOIN_STD_TOO_LONG : GLOIN_STD_OK);
        EXPECT_EQ(length, capacity <= 6 ? 0u : 6u);
        EXPECT_EQ(error, 0);
        EXPECT_EQ(buffer[0], 0xa5);
        EXPECT_EQ(buffer[capacity + 1], 0xa5);
        if (length) EXPECT_EQ(std::string(reinterpret_cast<char *>(buffer.data() + 1), length), "target");
    }
    // Reserve virtual address space without zeroing/touching gigabytes. A large
    // caller bound must not wrap the Linux syscall's signed-int buffer count.
    const uint64_t large = uint64_t(INT32_MAX) + 1;
    void *mapping = mmap(nullptr, large, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    uint64_t length = 99;
    EXPECT_EQ(gloin_fs_read_link(link.data(), link.size(), static_cast<uint8_t *>(mapping), large,
                                &length, &error), GLOIN_STD_OK);
    EXPECT_EQ(length, 6u);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(munmap(mapping, large), 0);
}
TEST_F(ContextRuntimeTest, SymlinkValidationAndHostErrorsAreDistinct) {
    const auto keep = file("keep"), missing = directory + "/missing", link = directory + "/link";
    const auto nul = keep + std::string("\0suffix", 7);
    int32_t error = 99;
    for (const auto &text : {std::string(), nul}) {
        EXPECT_EQ(gloin_fs_symlink(text.data(), text.size(), link.data(), link.size(), &error), GLOIN_STD_INVALID);
        EXPECT_EQ(error, 0);
        EXPECT_EQ(gloin_fs_symlink("x", 1, text.data(), text.size(), &error), GLOIN_STD_INVALID);
    }
    EXPECT_EQ(gloin_fs_symlink(nullptr, 1, link.data(), link.size(), &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_symlink("x", UINT64_MAX, link.data(), link.size(), &error), GLOIN_STD_INVALID);
    EXPECT_FALSE(std::filesystem::exists(link));
    std::array<uint8_t, 32> bytes{};
    uint64_t length = 99;
    for (const auto &path : {std::string(), nul}) {
        EXPECT_EQ(gloin_fs_read_link(path.data(), path.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(error, 0);
    }
    for (uint64_t capacity : {uint64_t(0), UINT64_MAX})
        EXPECT_EQ(gloin_fs_read_link(keep.data(), keep.size(), bytes.data(), capacity, &length, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_read_link(keep.data(), keep.size(), nullptr, 1, &length, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_read_link(keep.data(), keep.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(error, EINVAL);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(gloin_fs_read_link(missing.data(), missing.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOENT);
    const auto cycle = directory + "/cycle", nested = cycle + "/child";
    ASSERT_EQ(gloin_fs_symlink("cycle", 5, cycle.data(), cycle.size(), &error), GLOIN_STD_OK);
    EXPECT_EQ(gloin_fs_read_link(nested.data(), nested.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(error, ELOOP);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(gloin_fs_symlink("x", 1, nested.data(), nested.size(), &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(error, ELOOP);
    const auto absent_parent = missing + "/link";
    EXPECT_EQ(gloin_fs_symlink("x", 1, absent_parent.data(), absent_parent.size(), &error), GLOIN_STD_NOT_FOUND);
    EXPECT_TRUE(std::filesystem::exists(keep));
}
TEST_F(ContextRuntimeTest, DirectoryCursorReturnsNamesAndDistinguishesEnd) {
    file("alpha");
    file("é.txt");
    ASSERT_EQ(mkdir((directory + "/sub").c_str(), 0700), 0);
    void *handle = nullptr;
    int32_t error = 99;
    EXPECT_EQ(gloin_fs_dir_open("", 0, &handle, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_fs_dir_open(directory.data(), directory.size(), &handle, &error), GLOIN_STD_OK);
    ASSERT_NE(handle, nullptr);
    std::set<std::string> names;
    for (int i = 0; i < 10; ++i) {
        uint64_t length = 99;
        int32_t status = 99;
        const char *name = gloin_fs_dir_next(handle, &length, &status, &error);
        if (status == GLOIN_STD_EOF) {
            EXPECT_EQ(name, nullptr);
            EXPECT_EQ(length, 0u);
            EXPECT_EQ(error, 0);
            break;
        }
        ASSERT_EQ(status, GLOIN_STD_OK);
        ASSERT_NE(name, nullptr);
        names.emplace(name, length);
    }
    EXPECT_EQ(names, (std::set<std::string>{"alpha", "é.txt", "sub"}));
    EXPECT_EQ(gloin_fs_dir_close(handle, &error), GLOIN_STD_OK);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(gloin_fs_dir_close(nullptr, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_dir_open((directory + "/missing").c_str(),
                                directory.size() + 8, &handle, &error), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(error, ENOENT);
}
TEST_F(ContextRuntimeTest, CanonicalPathsResolveLinksAndRespectExactBounds) {
    const auto target = file("target"), link = directory + "/link";
    ASSERT_EQ(symlink("target", link.c_str()), 0);
    const auto expected = std::filesystem::canonical(target).string();
    const auto input = directory + "/./link", padded = input + "ignored";
    std::vector<uint8_t> bytes(expected.size() + 3, 0xa5);
    int32_t error = 99;
    uint64_t length = 99;
    EXPECT_EQ(gloin_fs_canonical_path(padded.data(), input.size(), bytes.data() + 1,
                                     expected.size() + 1, &length, &error), GLOIN_STD_OK);
    EXPECT_EQ(length, expected.size());
    EXPECT_EQ(error, 0);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes.data() + 1), length), expected);
    EXPECT_EQ(bytes.front(), 0xa5);
    EXPECT_EQ(bytes.back(), 0xa5);
    for (uint64_t capacity : {uint64_t(1), uint64_t(expected.size())}) {
        EXPECT_EQ(gloin_fs_canonical_path(input.data(), input.size(), bytes.data(), capacity,
                                         &length, &error), GLOIN_STD_TOO_LONG);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(error, 0);
    }
    const auto missing = directory + "/missing", broken = directory + "/broken", cycle = directory + "/cycle";
    ASSERT_EQ(symlink("missing", broken.c_str()), 0);
    ASSERT_EQ(symlink("cycle", cycle.c_str()), 0);
    for (const auto &path : {missing, broken}) {
        EXPECT_EQ(gloin_fs_canonical_path(path.data(), path.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_NOT_FOUND);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(error, ENOENT);
    }
    EXPECT_EQ(gloin_fs_canonical_path(cycle.data(), cycle.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(error, ELOOP);
    const auto nul = target + std::string("\0suffix", 7);
    for (const auto &path : {std::string(), nul}) {
        EXPECT_EQ(gloin_fs_canonical_path(path.data(), path.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
        EXPECT_EQ(error, 0);
    }
    EXPECT_EQ(gloin_fs_canonical_path("x", UINT64_MAX, bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_canonical_path(target.data(), target.size(), nullptr, 1, &length, &error), GLOIN_STD_INVALID);
    for (uint64_t capacity : {uint64_t(0), UINT64_MAX})
        EXPECT_EQ(gloin_fs_canonical_path(target.data(), target.size(), bytes.data(), capacity, &length, &error), GLOIN_STD_INVALID);
}
TEST_F(ContextRuntimeTest, TemporaryDirectoriesAreExclusivePrivateAndUseCallerStorage) {
    const auto pattern = directory + "/space XXX-XXXXXX";
    const auto padded = pattern + "ignored";
    std::set<std::string> names;
    for (int i = 0; i < 20; ++i) {
        std::vector<uint8_t> bytes(pattern.size() + 3, 0xa5);
        int32_t error = 99;
        uint64_t length = 99;
        ASSERT_EQ(gloin_fs_temp_dir(padded.data(), pattern.size(), bytes.data() + 1,
                                   pattern.size() + 1, &length, &error), GLOIN_STD_OK);
        ASSERT_EQ(length, pattern.size());
        EXPECT_EQ(error, 0);
        const std::string name(reinterpret_cast<char *>(bytes.data() + 1), length);
        EXPECT_TRUE(name.starts_with(directory + "/space XXX-"));
        EXPECT_TRUE(names.insert(name).second);
        struct stat info{};
        ASSERT_EQ(lstat(name.c_str(), &info), 0);
        EXPECT_TRUE(S_ISDIR(info.st_mode));
        EXPECT_EQ(info.st_mode & 0077, 0);
        EXPECT_EQ(bytes.front(), 0xa5);
        EXPECT_EQ(bytes.back(), 0xa5);
        EXPECT_EQ(gloin_fs_remove_dir(name.data(), name.size(), &error), GLOIN_STD_OK);
    }
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}
TEST_F(ContextRuntimeTest, TemporaryDirectoryFailuresCreateNothing) {
    const auto pattern = directory + "/prefix-XXXXXX";
    std::vector<uint8_t> bytes(pattern.size() + 2, 0xa5);
    int32_t error = 99;
    uint64_t length = 99;
    EXPECT_EQ(gloin_fs_temp_dir(pattern.data(), pattern.size(), bytes.data(), pattern.size(), &length, &error), GLOIN_STD_TOO_LONG);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(error, 0);
    const auto nul = pattern + std::string("\0XXXXXX", 7);
    for (const auto &input : {std::string(), directory + "/wrong", nul}) {
        EXPECT_EQ(gloin_fs_temp_dir(input.data(), input.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(error, 0);
    }
    EXPECT_EQ(gloin_fs_temp_dir("x", UINT64_MAX, bytes.data(), bytes.size(), &length, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_fs_temp_dir(pattern.data(), pattern.size(), nullptr, bytes.size(), &length, &error), GLOIN_STD_INVALID);
    for (uint64_t capacity : {uint64_t(0), UINT64_MAX})
        EXPECT_EQ(gloin_fs_temp_dir(pattern.data(), pattern.size(), bytes.data(), capacity, &length, &error), GLOIN_STD_INVALID);
    const auto missing = directory + "/missing/p-XXXXXX";
    bytes.resize(missing.size() + 1);
    EXPECT_EQ(gloin_fs_temp_dir(missing.data(), missing.size(), bytes.data(), bytes.size(), &length, &error), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOENT);
    EXPECT_EQ(length, 0u);
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}
TEST_F(ContextRuntimeTest, RemoveDirectoryRejectsContentsLinksAndSpecialLeaves) {
    const auto child = directory + "/child", link = directory + "/link";
    ASSERT_EQ(mkdir(child.c_str(), 0700), 0);
    int32_t error = 99;
    const auto file_path = file("child/keep");
    EXPECT_EQ(gloin_fs_remove_dir(child.data(), child.size(), &error), GLOIN_STD_ALREADY_EXISTS);
    EXPECT_TRUE(std::filesystem::exists(file_path));
    ASSERT_EQ(symlink("child", link.c_str()), 0);
    for (const auto &name : {link, link + "///", file_path}) {
        EXPECT_NE(gloin_fs_remove_dir(name.data(), name.size(), &error), GLOIN_STD_OK);
        EXPECT_TRUE(std::filesystem::exists(file_path));
        EXPECT_TRUE(std::filesystem::is_symlink(link));
    }
    const auto nul = child + std::string("\0tail", 5);
    for (const auto &name : {std::string(), std::string("/"), std::string("///"), std::string("."),
                             std::string(".."), child + "/.", child + "/../", nul}) {
        EXPECT_EQ(gloin_fs_remove_dir(name.data(), name.size(), &error), GLOIN_STD_INVALID);
        EXPECT_EQ(error, 0);
    }
    EXPECT_EQ(gloin_fs_remove_dir("x", UINT64_MAX, &error), GLOIN_STD_INVALID);
    ASSERT_EQ(unlink(file_path.c_str()), 0);
    const auto slashes = child + "///";
    EXPECT_EQ(gloin_fs_remove_dir(slashes.data(), slashes.size(), &error), GLOIN_STD_OK);
    EXPECT_FALSE(std::filesystem::exists(child));
    EXPECT_EQ(gloin_fs_remove_dir(child.data(), child.size(), &error), GLOIN_STD_NOT_FOUND);
    EXPECT_TRUE(std::filesystem::is_symlink(link));
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
