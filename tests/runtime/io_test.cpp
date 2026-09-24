#include "io_runtime.h"
#include "io_runtime_internal.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
class IoRuntimeTest : public testing::Test {
  protected:
    std::string directory;
    void SetUp() override {
        char pattern[] = "/tmp/gloinc-io-native-XXXXXX";
        const char *made = mkdtemp(pattern);
        ASSERT_NE(made, nullptr);
        directory = made;
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
    FILE *open(const std::string &path, int mode, int32_t expected = GLOIN_STD_OK) {
        int32_t status = -1, error = -1;
        auto *stream =
            static_cast<FILE *>(gloin_io_open(path.data(), path.size(), mode, &status, &error));
        EXPECT_EQ(status, expected) << path;
        if (expected == GLOIN_STD_OK) {
            EXPECT_NE(stream, nullptr);
            EXPECT_EQ(error, 0);
        } else
            EXPECT_EQ(stream, nullptr);
        return stream;
    }
    void close(FILE *stream) {
        int32_t error = -1;
        EXPECT_EQ(gloin_io_close(stream, &error), GLOIN_STD_OK);
        EXPECT_EQ(error, 0);
    }
    void write(FILE *stream, const std::string &text) {
        uint64_t written = 99;
        int32_t error = -1;
        EXPECT_EQ(gloin_io_write(stream, text.data(), text.size(), 1, &written, &error),
                  GLOIN_STD_OK);
        EXPECT_EQ(written, text.size());
        EXPECT_EQ(error, 0);
    }
    void read(FILE *stream, uint64_t bound, int mode, int32_t status, const std::string &expected,
              int32_t expected_error = 0) {
        std::vector<char> bytes(bound + 3, '#');
        uint64_t length = 99;
        int32_t error = -1;
        EXPECT_EQ(gloin_io_read(stream, bytes.data() + 1, bound, mode, &length, &error), status);
        ASSERT_LE(length, bound);
        EXPECT_EQ(std::string(bytes.data() + 1, length), expected);
        EXPECT_EQ(error, expected_error);
        EXPECT_EQ(bytes[length + 1], '\0');
        EXPECT_EQ(bytes.front(), '#');
        EXPECT_EQ(bytes.back(), '#');
    }
};
// A FILE with deterministic partial input and flush/close errors. State is
// caller-owned; fclose must invoke close once even when the callback reports failure.
struct Cookie {
    std::string data;
    size_t offset = 0;
    int read_error = 0, write_error = 0, close_error = 0, closes = 0;
    static int read(void *context, char *bytes, int size) {
        auto &self = *static_cast<Cookie *>(context);
        if (self.offset == self.data.size()) {
            errno = self.read_error;
            return self.read_error ? -1 : 0;
        }
        const auto count = std::min(size_t(size), self.data.size() - self.offset);
        std::memcpy(bytes, self.data.data() + self.offset, count);
        self.offset += count;
        return int(count);
    }
    static int write(void *context, const char *, int size) {
        auto &self = *static_cast<Cookie *>(context);
        errno = self.write_error;
        return self.write_error ? -1 : size;
    }
    static int close(void *context) {
        auto &self = *static_cast<Cookie *>(context);
        ++self.closes;
        errno = self.close_error;
        return self.close_error ? -1 : 0;
    }
    FILE *stream(bool reading) {
#ifdef __APPLE__
        return funopen(this, reading ? read : nullptr, reading ? nullptr : write, nullptr, close);
#else
        cookie_io_functions_t callbacks{};
        if (reading)
            callbacks.read = [](void *c, char *b, size_t n) -> ssize_t {
                return read(c, b, int(std::min(n, size_t(INT32_MAX))));
            };
        else
            callbacks.write = [](void *c, const char *b, size_t n) -> ssize_t {
                return write(c, b, int(std::min(n, size_t(INT32_MAX))));
            };
        callbacks.close = close;
        return fopencookie(this, reading ? "r" : "w", callbacks);
#endif
    }
};
} // namespace
TEST_F(IoRuntimeTest, ExplicitOpenModesBinaryBytesAndEmptyFiles) {
    const auto path = directory + "/a file.bin";
    FILE *stream = open(path, 1);
    ASSERT_NE(stream, nullptr);
    const std::string binary("a\0\xffz", 4);
    write(stream, binary);
    close(stream);
    EXPECT_EQ(open(path, 1, GLOIN_STD_ALREADY_EXISTS), nullptr);
    stream = open(path, 3);
    ASSERT_NE(stream, nullptr);
    write(stream, "!");
    close(stream);
    stream = open(path, 0);
    ASSERT_NE(stream, nullptr);
    read(stream, 5, 2, GLOIN_STD_OK, binary + "!");
    close(stream);
    stream = open(path, 2);
    ASSERT_NE(stream, nullptr);
    close(stream);
    stream = open(path, 0);
    ASSERT_NE(stream, nullptr);
    read(stream, 10, 2, GLOIN_STD_OK, "");
    read(stream, 10, 1, GLOIN_STD_EOF, "");
    close(stream);
    stream = open(directory + "/new-append", 3);
    ASSERT_NE(stream, nullptr);
    close(stream);
}
TEST_F(IoRuntimeTest, OpenFailuresPreserveErrnoAndDoNotTruncateInvalidPaths) {
    int32_t status, error;
    const auto missing = directory + "/missing";
    EXPECT_EQ(gloin_io_open(missing.data(), missing.size(), 0, &status, &error), nullptr);
    EXPECT_EQ(status, GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(error, ENOENT);
    FILE *stream = open(directory + "/keep", 1);
    ASSERT_NE(stream, nullptr);
    write(stream, "keep");
    close(stream);
    for (const std::string path : {std::string(), directory + "/keep" + std::string("\0tail", 5)}) {
        EXPECT_EQ(gloin_io_open(path.data(), path.size(), 2, &status, &error), nullptr);
        EXPECT_EQ(status, GLOIN_STD_INVALID);
        EXPECT_EQ(error, 0);
    }
    EXPECT_EQ(gloin_io_open(missing.data(), missing.size(), 4, &status, &error), nullptr);
    EXPECT_EQ(status, GLOIN_STD_INVALID);
    stream = open(directory + "/keep", 0);
    ASSERT_NE(stream, nullptr);
    read(stream, 4, 2, GLOIN_STD_OK, "keep");
    close(stream);
    EXPECT_EQ(gloin::io::error_status(EACCES), GLOIN_STD_PERMISSION_DENIED);
    EXPECT_EQ(gloin::io::error_status(EPERM), GLOIN_STD_PERMISSION_DENIED);
    EXPECT_EQ(gloin::io::error_status(ENOTDIR), GLOIN_STD_NOT_FOUND);
    EXPECT_EQ(gloin::io::error_status(ENOMEM), GLOIN_STD_NO_MEMORY);
}
TEST_F(IoRuntimeTest, PermissionErrorIsReportedRatherThanMissing) {
    auto path = directory + "/restricted";
    FILE *stream = open(path, 1);
    ASSERT_NE(stream, nullptr);
    close(stream);
    ASSERT_EQ(chmod(path.c_str(), 0), 0);
    if (geteuid() != 0) {
        int32_t status, error;
        EXPECT_EQ(gloin_io_open(path.data(), path.size(), 0, &status, &error), nullptr);
        EXPECT_EQ(status, GLOIN_STD_PERMISSION_DENIED);
        EXPECT_EQ(error, EACCES);
    }
    // Mandatory permission-failure path even when the test host is root.
    Cookie cookie{"", 0, EACCES};
    stream = cookie.stream(true);
    ASSERT_NE(stream, nullptr);
    read(stream, 4, 0, GLOIN_STD_PERMISSION_DENIED, "", EACCES);
    close(stream);
    ASSERT_EQ(chmod(path.c_str(), 0600), 0);
}
TEST_F(IoRuntimeTest, ChunkReadsPreserveBinaryDataFinalPartialAndRepeatedEnd) {
    Cookie cookie{std::string("a\0bc\xff", 5)};
    FILE *stream = cookie.stream(true);
    ASSERT_NE(stream, nullptr);
    read(stream, 3, 1, GLOIN_STD_OK, std::string("a\0b", 3));
    read(stream, 3, 1, GLOIN_STD_OK, std::string("c\xff", 2));
    read(stream, 3, 1, GLOIN_STD_EOF, "");
    read(stream, 3, 1, GLOIN_STD_EOF, "");
    close(stream);
}
TEST_F(IoRuntimeTest, ReadAllBoundsProbeDoesNotLoseTheNextByte) {
    Cookie cookie{"abcd"};
    FILE *stream = cookie.stream(true);
    ASSERT_NE(stream, nullptr);
    read(stream, 0, 2, GLOIN_STD_TOO_LONG, "");
    read(stream, 2, 2, GLOIN_STD_TOO_LONG, "ab");
    read(stream, 2, 2, GLOIN_STD_OK, "cd");
    read(stream, 0, 2, GLOIN_STD_OK, "");
    close(stream);
}
TEST_F(IoRuntimeTest, LinesStripOnlyTerminatorsAndDrainOverflow) {
    Cookie cookie{std::string("\n\r\na\rb\r\nlonger\n") + std::string("A\0B\n", 4) + "last\r"};
    FILE *stream = cookie.stream(true);
    ASSERT_NE(stream, nullptr);
    read(stream, 0, 0, GLOIN_STD_OK, "");
    read(stream, 0, 0, GLOIN_STD_OK, "");
    read(stream, 3, 0, GLOIN_STD_OK, "a\rb");
    read(stream, 3, 0, GLOIN_STD_TOO_LONG, "");
    read(stream, 3, 0, GLOIN_STD_OK, std::string("A\0B", 3));
    read(stream, 5, 0, GLOIN_STD_OK, "last\r");
    read(stream, 5, 0, GLOIN_STD_EOF, "");
    close(stream);
}
TEST_F(IoRuntimeTest, PartialReadErrorsKeepChunkAndAllPrefixesButClearLines) {
    for (int mode : {0, 1, 2}) {
        Cookie cookie{"abc", 0, EIO};
        FILE *stream = cookie.stream(true);
        ASSERT_NE(stream, nullptr);
        read(stream, 8, mode, GLOIN_STD_IO_ERROR, mode == 0 ? "" : "abc", EIO);
        cookie.read_error = 0;
        read(stream, 8, mode, mode == 2 ? GLOIN_STD_OK : GLOIN_STD_EOF, "");
        close(stream);
        EXPECT_EQ(cookie.closes, 1);
    }
}
TEST_F(IoRuntimeTest, InvalidBoundsAndClosedHandlesConsumeNothing) {
    Cookie cookie{"abc"};
    FILE *stream = cookie.stream(true);
    ASSERT_NE(stream, nullptr);
    read(stream, 0, 1, GLOIN_STD_INVALID, "");
    read(stream, 3, 9, GLOIN_STD_INVALID, "");
    char byte = '#';
    uint64_t length = 99;
    int32_t error = 99;
    EXPECT_EQ(gloin_io_read(stream, &byte, UINT64_MAX, 1, &length, &error), GLOIN_STD_NO_MEMORY);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(cookie.offset, 0u);
    read(stream, 3, 2, GLOIN_STD_OK, "abc");
    close(stream);
    EXPECT_EQ(gloin_io_read(nullptr, &byte, 0, 0, &length, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(gloin_io_write(nullptr, "", 0, 1, &length, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(gloin_io_flush(nullptr, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(gloin_io_close(nullptr, &error), GLOIN_STD_CLOSED);
}
TEST_F(IoRuntimeTest, PartialWritesReportProgressAndAllRetriesOnlySuccessfulShortWrites) {
    struct Sink {
        std::string data;
        unsigned calls = 0;
        bool fail = false;
    } sink;
    auto writer = +[](void *context, const char *bytes, size_t n) -> gloin::io::WriteAttempt {
        auto &s = *static_cast<Sink *>(context);
        ++s.calls;
        size_t count = std::min(n, size_t(2));
        s.data.append(bytes, count);
        return {count, s.fail ? ENOSPC : 0};
    };
    uint64_t written;
    int32_t error;
    EXPECT_EQ(gloin::io::write(&sink, writer, "abcde", 5, false, &written, &error), GLOIN_STD_OK);
    EXPECT_EQ(written, 2u);
    EXPECT_EQ(sink.data, "ab");
    EXPECT_EQ(sink.calls, 1u);
    sink = {};
    EXPECT_EQ(gloin::io::write(&sink, writer, "abcde", 5, true, &written, &error), GLOIN_STD_OK);
    EXPECT_EQ(written, 5u);
    EXPECT_EQ(sink.data, "abcde");
    EXPECT_EQ(sink.calls, 3u);
    sink = {};
    sink.fail = true;
    EXPECT_EQ(gloin::io::write(&sink, writer, "abcde", 5, true, &written, &error),
              GLOIN_STD_IO_ERROR);
    EXPECT_EQ(written, 2u);
    EXPECT_EQ(error, ENOSPC);
    EXPECT_EQ(sink.calls, 1u);
}
TEST_F(IoRuntimeTest, ZeroProgressAndEmptyWriteDoNotSpin) {
    unsigned calls = 0;
    auto zero = +[](void *p, const char *, size_t) -> gloin::io::WriteAttempt {
        ++*static_cast<unsigned *>(p);
        return {0, 0};
    };
    uint64_t written;
    int32_t error;
    EXPECT_EQ(gloin::io::write(&calls, zero, "abc", 3, true, &written, &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(written, 0u);
    EXPECT_EQ(error, EIO);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(gloin::io::write(&calls, zero, nullptr, 0, true, &written, &error), GLOIN_STD_OK);
    EXPECT_EQ(written, 0u);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(calls, 1u);
}
TEST_F(IoRuntimeTest, FlushFailureAndCloseFailureAreObservableAndConsumeOnce) {
    Cookie cookie;
    cookie.write_error = ENOSPC;
    cookie.close_error = EIO;
    FILE *stream = cookie.stream(false);
    ASSERT_NE(stream, nullptr);
    std::array<char, 128> buffer{};
    ASSERT_EQ(std::setvbuf(stream, buffer.data(), _IOFBF, buffer.size()), 0);
    write(stream, "pending");
    int32_t error = 0;
    EXPECT_EQ(gloin_io_flush(stream, &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(error, ENOSPC);
    EXPECT_EQ(gloin_io_close(stream, &error), GLOIN_STD_IO_ERROR);
    EXPECT_NE(error, 0);
    EXPECT_EQ(cookie.closes, 1);
}
TEST_F(IoRuntimeTest, BrokenPipeReturnsErrorAndPreservesSignalMaskAndPendingSignal) {
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    ASSERT_EQ(::close(descriptors[0]), 0);
    FILE *stream = fdopen(descriptors[1], "w");
    ASSERT_NE(stream, nullptr);
    ASSERT_EQ(setvbuf(stream, nullptr, _IONBF, 0), 0);
    sigset_t before{}, after{};
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &before), 0);
    uint64_t written;
    int32_t error;
    EXPECT_EQ(gloin_io_write(stream, "x", 1, 1, &written, &error), GLOIN_STD_IO_ERROR);
    EXPECT_EQ(error, EPIPE);
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &after), 0);
    EXPECT_EQ(sigismember(&before, SIGPIPE), sigismember(&after, SIGPIPE));
    sigset_t set{}, previous{};
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, &set, &previous), 0);
    ASSERT_EQ(pthread_kill(pthread_self(), SIGPIPE), 0);
    EXPECT_EQ(gloin_io_write(stream, "x", 1, 1, &written, &error), GLOIN_STD_IO_ERROR);
    ASSERT_EQ(sigpending(&after), 0);
    EXPECT_EQ(sigismember(&after, SIGPIPE), 1);
    int signal;
    ASSERT_EQ(sigwait(&set, &signal), 0);
    EXPECT_EQ(signal, SIGPIPE);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, &previous, nullptr), 0);
    close(stream);
}
TEST_F(IoRuntimeTest, StandardHandlesAreBorrowedAndOsMessagesAreBounded) {
    EXPECT_EQ(gloin_io_standard(1), stdin);
    EXPECT_EQ(gloin_io_standard(2), stdout);
    EXPECT_EQ(gloin_io_standard(3), stderr);
    EXPECT_EQ(gloin_io_standard(0), nullptr);
    std::array<char, 258> bytes;
    bytes.fill('#');
    uint64_t length = 999;
    EXPECT_EQ(gloin_io_error_message(ENOENT, bytes.data() + 1, &length), GLOIN_STD_OK);
    EXPECT_GT(length, 0u);
    EXPECT_LT(length, 256u);
    EXPECT_EQ(bytes[length + 1], '\0');
    EXPECT_EQ(bytes.front(), '#');
    EXPECT_EQ(bytes.back(), '#');
    EXPECT_EQ(gloin_io_error_message(-1, bytes.data() + 1, &length), GLOIN_STD_INVALID);
    EXPECT_EQ(length, 0u);
    EXPECT_EQ(bytes[1], '\0');
}
TEST_F(IoRuntimeTest, RepeatedOwnedOpenCloseDoesNotLeakDescriptors) {
    const auto path = directory + "/repeat";
    for (unsigned i = 0; i < 2000; ++i) {
        FILE *stream = open(path, 2);
        ASSERT_NE(stream, nullptr);
        write(stream, "x");
        const int descriptor = fileno(stream);
        close(stream);
        EXPECT_EQ(fcntl(descriptor, F_GETFD), -1);
        EXPECT_EQ(errno, EBADF);
    }
}
