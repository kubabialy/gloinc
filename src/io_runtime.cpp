#include "io_runtime.h"
#include "io_runtime_internal.h"
#include "stdlib_runtime.h"
#include "stdlib_runtime_internal.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <signal.h>

namespace {
// Suppress only SIGPIPE generated during this thread's I/O. Preserve the caller's
// signal mask, disposition, and any signal that was pending before the operation.
class PipeSignal {
    sigset_t set{}, previous{};
    bool pending = false, active = false;

  public:
    int error = 0;
    bool broken_pipe = false;
    PipeSignal() {
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        error = pthread_sigmask(SIG_BLOCK, &set, &previous);
        if (error)
            return;
        active = true;
        sigset_t signals{};
        if (sigpending(&signals)) {
            error = errno;
            return;
        }
        pending = sigismember(&signals, SIGPIPE) == 1;
    }
    ~PipeSignal() {
        if (!active)
            return;
        sigset_t signals{};
        if (broken_pipe && !error && !pending && !sigpending(&signals) &&
            sigismember(&signals, SIGPIPE) == 1) {
            int signal = 0;
            sigwait(&set, &signal);
        }
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }
};
int32_t failure(int code, int32_t *os_error) {
    *os_error = code ? code : EIO;
    return gloin::io::error_status(*os_error);
}
gloin::io::WriteAttempt stdio_write(void *context, const char *bytes, size_t size) {
    auto *stream = static_cast<FILE *>(context);
    errno = 0;
    const size_t count = std::fwrite(bytes, 1, size, stream);
    const int error = std::ferror(stream) ? (errno ? errno : EIO) : 0;
    return {count, error};
}
// POSIX strerror_r returns int; GNU's variant returns a pointer, sometimes to
// static storage. Both paths copy only bounded text into the caller's buffer.
[[maybe_unused]] int message_result(int result, char *) { return result; }
[[maybe_unused]] int message_result(char *result, char *bytes) {
    const auto size = std::strlen(result);
    if (size >= 256)
        return ERANGE;
    if (result != bytes)
        std::memcpy(bytes, result, size + 1);
    return 0;
}
} // namespace

int32_t gloin::io::error_status(int error) noexcept {
    switch (error) {
    case ENOENT:
    case ENOTDIR:
        return GLOIN_STD_NOT_FOUND;
    case EACCES:
    case EPERM:
        return GLOIN_STD_PERMISSION_DENIED;
    case EEXIST:
        return GLOIN_STD_ALREADY_EXISTS;
    case ENOMEM:
        return GLOIN_STD_NO_MEMORY;
    default:
        return GLOIN_STD_IO_ERROR;
    }
}
extern "C" void *gloin_io_standard(int32_t which) {
    return which == 1 ? stdin : which == 2 ? stdout : which == 3 ? stderr : nullptr;
}
extern "C" void *gloin_io_open(const char *path, uint64_t length, int32_t mode, int32_t *status,
                               int32_t *os_error) {
    *os_error = 0;
    *status = GLOIN_STD_INVALID;
    if (mode < 0 || mode > 3 || !length || !path)
        return nullptr;
    if (length > uint64_t(std::numeric_limits<ptrdiff_t>::max()) - 1) {
        *status = GLOIN_STD_NO_MEMORY;
        return nullptr;
    }
    if (std::memchr(path, '\0', length))
        return nullptr;
    auto *terminated = static_cast<char *>(std::malloc(length + 1));
    if (!terminated) {
        *status = GLOIN_STD_NO_MEMORY;
        return nullptr;
    }
    std::memcpy(terminated, path, length);
    terminated[length] = '\0';
    const char *modes[] = {"rb", "wbx", "wb", "ab"};
    errno = 0;
    FILE *stream = std::fopen(terminated, modes[mode]);
    const int open_error = errno;
    std::free(terminated);
    if (!stream) {
        *status = failure(open_error, os_error);
        return nullptr;
    }
    // No retained hidden byte buffer; FILE itself is native-owned until fclose.
    errno = 0;
    if (std::setvbuf(stream, nullptr, _IONBF, 0)) {
        const int buffer_error = errno;
        std::fclose(stream);
        *status = failure(buffer_error, os_error);
        return nullptr;
    }
    *status = GLOIN_STD_OK;
    return stream;
}
extern "C" int32_t gloin_io_read(void *handle, char *bytes, uint64_t capacity, int32_t mode,
                                 uint64_t *length, int32_t *os_error) {
    *length = 0;
    *os_error = 0;
    bytes[0] = '\0';
    if (!handle)
        return GLOIN_STD_CLOSED;
    if (mode < 0 || mode > 2 || (mode == 1 && !capacity))
        return GLOIN_STD_INVALID;
    if (capacity > uint64_t(std::numeric_limits<ptrdiff_t>::max()) - 1)
        return GLOIN_STD_NO_MEMORY;
    auto *stream = static_cast<FILE *>(handle);
    std::clearerr(stream);
    if (mode == 0) {
        const int32_t code = gloin::standard::input(stream, bytes, capacity, length, os_error);
        return code == GLOIN_STD_IO_ERROR ? failure(*os_error, os_error) : code;
    }
    errno = 0;
    const auto received = capacity ? std::fread(bytes, 1, capacity, stream) : 0;
    *length = received;
    bytes[received] = '\0';
    if (std::ferror(stream))
        return failure(errno, os_error);
    if (mode == 1)
        return received ? GLOIN_STD_OK : GLOIN_STD_EOF;
    if (received < capacity)
        return GLOIN_STD_OK;
    errno = 0;
    const int probe = std::fgetc(stream);
    if (probe == EOF)
        return std::ferror(stream) ? failure(errno, os_error) : GLOIN_STD_OK;
    // Standard C guarantees one pushback byte, including after a zero-bound read.
    if (std::ungetc(probe, stream) == EOF)
        return failure(errno, os_error);
    return GLOIN_STD_TOO_LONG;
}
int32_t gloin::io::write(void *context, Writer writer, const char *bytes, uint64_t length, bool all,
                         uint64_t *written, int32_t *os_error) noexcept {
    *written = 0;
    *os_error = 0;
    if (length > uint64_t(std::numeric_limits<ptrdiff_t>::max()))
        return GLOIN_STD_INVALID;
    while (*written < length) {
        const auto remaining = static_cast<size_t>(length - *written);
        const auto attempt = writer(context, bytes + *written, remaining);
        if (attempt.count > remaining)
            return failure(EIO, os_error);
        *written += attempt.count;
        if (attempt.error)
            return failure(attempt.error, os_error);
        if (!attempt.count)
            return failure(EIO, os_error);
        if (!all)
            break;
    }
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_io_write(void *handle, const char *bytes, uint64_t length, int32_t all,
                                  uint64_t *written, int32_t *os_error) {
    *written = 0;
    *os_error = 0;
    if (!handle)
        return GLOIN_STD_CLOSED;
    if (all != 0 && all != 1)
        return GLOIN_STD_INVALID;
    PipeSignal signal;
    if (signal.error)
        return failure(signal.error, os_error);
    auto *stream = static_cast<FILE *>(handle);
    std::clearerr(stream);
    const int32_t code =
        gloin::io::write(stream, stdio_write, bytes, length, all, written, os_error);
    signal.broken_pipe = *os_error == EPIPE;
    return code;
}
extern "C" int32_t gloin_io_flush(void *handle, int32_t *os_error) {
    *os_error = 0;
    if (!handle)
        return GLOIN_STD_CLOSED;
    PipeSignal signal;
    if (signal.error)
        return failure(signal.error, os_error);
    errno = 0;
    const int32_t code =
        std::fflush(static_cast<FILE *>(handle)) ? failure(errno, os_error) : GLOIN_STD_OK;
    signal.broken_pipe = *os_error == EPIPE;
    return code;
}
extern "C" int32_t gloin_io_close(void *handle, int32_t *os_error) {
    *os_error = 0;
    if (!handle)
        return GLOIN_STD_CLOSED;
    PipeSignal signal;
    // Even a masking failure must consume the resource; fclose on an owned,
    // unbuffered stream has no pending byte buffer to flush.
    errno = 0;
    const int result = std::fclose(static_cast<FILE *>(handle));
    if (result) {
        const int32_t code = failure(errno, os_error);
        signal.broken_pipe = *os_error == EPIPE;
        return code;
    }
    return signal.error ? failure(signal.error, os_error) : GLOIN_STD_OK;
}
extern "C" int32_t gloin_io_error_message(int32_t code, char *bytes, uint64_t *length) {
    *length = 0;
    bytes[0] = '\0';
    if (code < 0)
        return GLOIN_STD_INVALID;
    const int result = message_result(strerror_r(code, bytes, 256), bytes);
    if (result) {
        bytes[0] = '\0';
        return result == ERANGE ? GLOIN_STD_TOO_LONG : GLOIN_STD_INVALID;
    }
    *length = std::strlen(bytes);
    return GLOIN_STD_OK;
}
