#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "process_runtime_internal.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <vector>
#endif

namespace gloin::process {
namespace {
struct StatusPipe {
    int read_end = -1, write_end = -1;
    ~StatusPipe() {
        if (read_end >= 0) close(read_end);
        if (write_end >= 0) close(write_end);
    }
    int create() {
        int descriptors[2];
#ifdef __APPLE__
        if (pipe(descriptors)) return errno;
#else
        if (pipe2(descriptors, O_CLOEXEC)) return errno;
#endif
        read_end = descriptors[0];
        write_end = descriptors[1];
        for (int *fd : {&read_end, &write_end}) {
            if (*fd < 3) {
                const int moved = fcntl(*fd, F_DUPFD_CLOEXEC, 3);
                if (moved < 0) return errno;
                close(*fd);
                *fd = moved;
            }
            if (fcntl(*fd, F_SETFD, FD_CLOEXEC)) return errno;
        }
        return 0;
    }
};

#ifdef __APPLE__
// Darwin has no close_range. Include descriptors left above a lowered soft
// limit; newly opened descriptors cannot exceed the current limit. As with
// inherited cwd/env/std descriptors, embedders must coordinate limit mutation.
int descriptor_bound(int *bound) {
    struct rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit)) return errno;
    *bound = int(std::min<rlim_t>(limit.rlim_cur, INT_MAX));
    size_t capacity = 128;
    for (;;) {
        if (capacity > size_t(INT_MAX) / sizeof(proc_fdinfo)) return EOVERFLOW;
        std::vector<proc_fdinfo> descriptors(capacity);
        const int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, descriptors.data(),
                                       int(descriptors.size() * sizeof(proc_fdinfo)));
        if (bytes < 0) return errno ? errno : EIO;
        if (bytes % sizeof(proc_fdinfo)) return EIO;
        const size_t count = size_t(bytes) / sizeof(proc_fdinfo);
        if (count >= capacity) { capacity *= 2; continue; }
        // Our open status pipe guarantees a nonempty descriptor set.
        if (!count) return errno ? errno : EIO;
        for (size_t i = 0; i < count; ++i) {
            if (descriptors[i].proc_fd >= uint32_t(INT_MAX)) return EOVERFLOW;
            *bound = std::max(*bound, int(descriptors[i].proc_fd) + 1);
        }
        return 0;
    }
}
#endif

// All functions below the fork boundary use only fixed storage and host calls.
// No C++ allocation, strings, logging, locks, callbacks or stdio in the child.
[[noreturn]] void child_failure(int descriptor, int error) {
    const char *bytes = reinterpret_cast<const char *>(&error);
    size_t remaining = sizeof(error);
    while (remaining) {
        const ssize_t written = write(descriptor, bytes, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        bytes += written;
        remaining -= size_t(written);
    }
    _exit(127);
}

int close_descriptors(int retained, int bound) {
#ifdef __APPLE__
    for (int fd = 3; fd < bound; ++fd) {
        if (fd == retained) continue;
        int result;
        do { result = close(fd); } while (result < 0 && errno == EINTR);
        if (result < 0 && errno != EBADF) return errno;
    }
#else
    (void)bound;
    // Ubuntu 24.04's supported kernel/libc provide close_range. If denied or
    // unavailable, fail the launch rather than leak nonstandard descriptors.
    if (retained > 3 && close_range(3, unsigned(retained - 1), 0)) return errno;
    if (close_range(unsigned(retained) + 1, UINT_MAX, 0)) return errno;
#endif
    return 0;
}

void reap_failed(pid_t child, bool terminate) {
    if (terminate) kill(child, SIGKILL);
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}
} // namespace

int start_limited(const char *executable, char *const *arguments, char *const *environment,
                  const int *streams, const char *cwd, bool new_group,
                  uint64_t stack_bytes, pid_t *child) {
    *child = 0;
#ifdef __APPLE__
    // Darwin checks that stack shrinking runs on the original main stack.
    // A fork from a worker preserves that worker's stack. Reject this context
    // before creating a child; never alter the embedding parent's limits.
    if (!pthread_main_np()) return ENOTSUP;
#endif
    struct rlimit stack{};
    if (!stack_bytes || stack_bytes >= uint64_t(INT64_MAX)) return EINVAL;
    if (getrlimit(RLIMIT_STACK, &stack)) return errno;
    if (stack.rlim_max != RLIM_INFINITY && stack_bytes > stack.rlim_max) return EINVAL;
    stack.rlim_cur = rlim_t(stack_bytes); // Both supported ABIs use 64-bit rlim_t.

    StatusPipe channel;
    if (int error = channel.create()) return error;
    int descriptor_limit = 0;
#ifdef __APPLE__
    if (int error = descriptor_bound(&descriptor_limit)) return error;
#endif
    // Prepare signal structures before fork. Blocking this thread's signals
    // prevents inherited handlers from running in the child's setup window.
    sigset_t blocked, original, empty;
    sigfillset(&blocked);
    sigemptyset(&empty);
    struct sigaction defaults{};
    defaults.sa_handler = SIG_DFL;
    sigemptyset(&defaults.sa_mask);
    if (int error = pthread_sigmask(SIG_SETMASK, &blocked, &original)) return error;
    const pid_t launched = fork();
    const int fork_error = errno;
    if (launched == 0) {
        const int errors = channel.write_end;
        for (int signal = 1; signal < NSIG; ++signal) {
            if (signal == SIGKILL || signal == SIGSTOP) continue;
            if (sigaction(signal, &defaults, nullptr) && errno != EINVAL) child_failure(errors, errno);
        }
        if (new_group && setpgid(0, 0)) child_failure(errors, errno);
        if (cwd && chdir(cwd)) child_failure(errors, errno);
        for (int fd = 0; fd < 3; ++fd) {
            const int source = streams ? streams[fd] : fd;
            if (source != fd) {
                if (dup2(source, fd) < 0) child_failure(errors, errno);
            } else if (fcntl(fd, F_SETFD, 0) < 0 && errno != EBADF) {
                child_failure(errors, errno);
            }
        }
        if (int error = close_descriptors(errors, descriptor_limit)) child_failure(errors, error);
        // setrlimit is an AS-safe libc operation on our glibc target and a
        // direct syscall wrapper on Darwin. Apply only in the child, before exec.
        if (setrlimit(RLIMIT_STACK, &stack)) child_failure(errors, errno);
        if (sigprocmask(SIG_SETMASK, &empty, nullptr)) child_failure(errors, errno);
        execve(executable, arguments, environment);
        child_failure(errors, errno);
    }
    const int restored = pthread_sigmask(SIG_SETMASK, &original, nullptr);
    if (launched < 0) return restored ? restored : fork_error;
    if (restored) { reap_failed(launched, true); return restored; }
    close(channel.write_end);
    channel.write_end = -1;
    int error = 0;
    char *bytes = reinterpret_cast<char *>(&error);
    size_t received = 0;
    while (received < sizeof(error)) {
        const ssize_t count = read(channel.read_end, bytes + received, sizeof(error) - received);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { const int failed = errno; reap_failed(launched, true); return failed; }
        if (!count) break;
        received += size_t(count);
    }
    if (received) {
        const int failed = received == sizeof(error) && error > 0 ? error : EIO;
        reap_failed(launched, true);
        return failed;
    }
    // EOF: close-on-exec consumed the error writer. An independently signalled
    // child may also produce EOF; its terminal status remains observable normally.
    *child = launched;
    return 0;
}
} // namespace gloin::process
