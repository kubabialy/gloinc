#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "context_runtime.h"
#include "io_runtime_internal.h"
#include "stdlib_runtime.h"
#include "process_runtime_internal.h"
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <crt_externs.h>
#include <sys/sysctl.h>
#else
extern char **environ;
#endif

namespace {
bool text_valid(const char *bytes, uint64_t size) {
    return size <= uint64_t(PTRDIFF_MAX) - 1 && (!size || (bytes && !std::memchr(bytes, 0, size)));
}
int32_t failure(int code, int32_t *os_error) {
    *os_error = code;
    return gloin::io::error_status(code);
}
bool pid_valid(int64_t pid) {
    return pid > 0 && pid <= std::numeric_limits<pid_t>::max();
}
#ifdef __APPLE__
// Darwin's killpg path skips zombies and returns EPERM when none were signalled.
// Only normalize that error after a complete snapshot proves every member is
// a zombie or already exiting. P_WEXIT precedes SZOMB during descriptor teardown.
// Never infer an empty group merely from the leader's cached exit status.
int empty_group(pid_t group) {
    int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PGRP, group};
    try {
        for (int attempt = 0; attempt < 3; ++attempt) {
            size_t size = 0;
            if (sysctl(query, 4, nullptr, &size, nullptr, 0)) return errno;
            std::vector<kinfo_proc> members(size / sizeof(kinfo_proc) + 8);
            size = members.size() * sizeof(kinfo_proc);
            if (sysctl(query, 4, members.data(), &size, nullptr, 0)) {
                if (errno == ENOMEM) continue;
                return errno;
            }
            if (size % sizeof(kinfo_proc)) return EIO;
            for (size_t i = 0; i < size / sizeof(kinfo_proc); ++i) {
                const auto &member = members[i].kp_proc;
                if (member.p_stat != SZOMB && !(member.p_flag & P_WEXIT)) return EPERM;
            }
            return 0;
        }
        return EAGAIN;
    } catch (const std::bad_alloc &) {
        return ENOMEM;
    } catch (const std::length_error &) {
        return ENOMEM;
    }
}
#endif
struct SpawnSetup {
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    bool has_attributes = false;
    bool has_actions = false;
    ~SpawnSetup() {
        if (has_actions) posix_spawn_file_actions_destroy(&actions);
        if (has_attributes) posix_spawnattr_destroy(&attributes);
    }
    int prepare(const int *streams, const char *cwd, bool new_group) {
        int error = posix_spawnattr_init(&attributes);
        if (error) return error;
        has_attributes = true;
        error = posix_spawn_file_actions_init(&actions);
        if (error) return error;
        has_actions = true;

        sigset_t mask, defaults;
        sigemptyset(&mask);
        sigfillset(&defaults);
        sigdelset(&defaults, SIGKILL);
        sigdelset(&defaults, SIGSTOP);
        if ((error = posix_spawnattr_setsigmask(&attributes, &mask)) ||
            (error = posix_spawnattr_setsigdefault(&attributes, &defaults))) return error;
        short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
        if (new_group) {
            flags |= POSIX_SPAWN_SETPGROUP;
            if ((error = posix_spawnattr_setpgroup(&attributes, 0))) return error;
        }
#ifdef __APPLE__
        flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
        if ((error = posix_spawnattr_setflags(&attributes, flags))) return error;
        if (cwd && (error = posix_spawn_file_actions_addchdir_np(&actions, cwd))) return error;
        // Explicit dup2 actions also preserve standard descriptors marked CLOEXEC.
        // A closed standard descriptor stays closed. Coordinate host fd mutation.
        for (int fd = 0; fd < 3; ++fd) {
            const int source = streams ? streams[fd] : fd;
            if (fcntl(source, F_GETFD) < 0) {
                if (errno == EBADF) continue;
                return errno;
            }
            if ((error = posix_spawn_file_actions_adddup2(&actions, source, fd))) return error;
        }
#ifndef __APPLE__
        // Redirections must precede closing their source descriptors.
        if ((error = posix_spawn_file_actions_addclosefrom_np(&actions, 3))) return error;
#endif
        return 0;
    }
};

struct LaunchOptions {
    const char *cwd = nullptr;
    uint64_t cwd_size = 0;
    const GloinProcessArgument *environment = nullptr;
    uint64_t environment_count = 0;
    bool inherit_environment = true;
    bool new_group = false;
    uint64_t stack_bytes = 0;
};

bool options_valid(const LaunchOptions &options) {
    if (options.stack_bytes >= uint64_t(INT64_MAX) || !text_valid(options.cwd, options.cwd_size) ||
        (options.environment_count && (!options.environment || options.inherit_environment)) ||
        options.environment_count > uint64_t(PTRDIFF_MAX) / sizeof(GloinProcessArgument) - 1)
        return false;
    for (uint64_t i = 0; i < options.environment_count; ++i) {
        const auto &entry = options.environment[i];
        if (!entry.size || !text_valid(entry.bytes, entry.size)) return false;
        const std::string_view value(entry.bytes, entry.size);
        const auto equal = value.find('=');
        if (equal == 0 || equal == std::string_view::npos) return false;
        const auto key = value.substr(0, equal);
        // Duplicate keys have host-dependent lookup behavior; reject them.
        for (uint64_t j = 0; j < i; ++j) {
            const auto &previous = options.environment[j];
            const std::string_view candidate(previous.bytes, previous.size);
            if (candidate.substr(0, candidate.find('=')) == key) return false;
        }
    }
    return true;
}

int32_t start(const char *executable, uint64_t size,
                                        const GloinProcessArgument *arguments, uint64_t count,
                                        const int *streams, const LaunchOptions &options,
                                        int64_t *pid, int32_t *os_error) {
    *pid = 0;
    *os_error = 0;
    if (!size || !text_valid(executable, size) || !options_valid(options) || (count && !arguments) ||
        count > uint64_t(PTRDIFF_MAX) / sizeof(GloinProcessArgument) - 2)
        return GLOIN_STD_INVALID;
    for (uint64_t i = 0; i < count; ++i)
        if (!text_valid(arguments[i].bytes, arguments[i].size)) return GLOIN_STD_INVALID;
    // An auto-reaping host cannot provide the ownership contract exposed by Child.
    // Do not change process-global signal handling on the embedding application's behalf.
    struct sigaction child_signal {};
    if (sigaction(SIGCHLD, nullptr, &child_signal)) return failure(errno, os_error);
    if (child_signal.sa_handler == SIG_IGN || (child_signal.sa_flags & SA_NOCLDWAIT))
        return GLOIN_STD_INVALID;
    try {
        std::vector<std::string> values;
        values.reserve(count + 1);
        values.emplace_back(executable, size);
        for (uint64_t i = 0; i < count; ++i)
            values.emplace_back(arguments[i].size ? arguments[i].bytes : "", arguments[i].size);
        std::vector<char *> argv;
        argv.reserve(count + 2);
        for (auto &value : values) argv.push_back(value.data());
        argv.push_back(nullptr);
        std::string cwd(options.cwd_size ? options.cwd : "", options.cwd_size);
        std::vector<std::string> environment_values;
        std::vector<char *> environment_pointers;
        char **environment = nullptr;
        if (options.inherit_environment) {
#ifdef __APPLE__
            environment = *_NSGetEnviron();
#else
            environment = environ;
#endif
        } else {
            environment_values.reserve(options.environment_count);
            for (uint64_t i = 0; i < options.environment_count; ++i) {
                const auto &entry = options.environment[i];
                environment_values.emplace_back(entry.bytes, entry.size);
            }
            environment_pointers.reserve(options.environment_count + 1);
            for (auto &entry : environment_values) environment_pointers.push_back(entry.data());
            environment_pointers.push_back(nullptr);
            environment = environment_pointers.data();
        }
        if (options.stack_bytes) {
            pid_t child = 0;
            if (int error = gloin::process::start_limited(values[0].c_str(), argv.data(), environment,
                    streams, cwd.empty() ? nullptr : cwd.c_str(), options.new_group, options.stack_bytes, &child)) {
                *os_error = error;
                return error == ENOTSUP || error == EINVAL ? GLOIN_STD_INVALID : gloin::io::error_status(error);
            }
            *pid = child;
            return GLOIN_STD_OK;
        }
        SpawnSetup setup;
        if (int error = setup.prepare(streams, cwd.empty() ? nullptr : cwd.c_str(), options.new_group))
            return failure(error, os_error);
        pid_t child = 0;
        const int error = posix_spawn(&child, values[0].c_str(), &setup.actions,
                                      &setup.attributes, argv.data(), environment);
        if (error) return failure(error, os_error);
        *pid = child;
        return GLOIN_STD_OK;
    } catch (const std::bad_alloc &) {
        return GLOIN_STD_NO_MEMORY;
    } catch (const std::length_error &) {
        return GLOIN_STD_NO_MEMORY;
    }
}

struct Pipes {
    int descriptors[6] = {-1, -1, -1, -1, -1, -1};
    ~Pipes() {
        for (int fd : descriptors) if (fd >= 0) close(fd);
    }
    int prepare() {
        for (int i = 0; i < 6; i += 2) {
#ifdef __APPLE__
            if (pipe(descriptors + i)) return errno;
#else
            if (pipe2(descriptors + i, O_CLOEXEC)) return errno;
#endif
            for (int j = i; j < i + 2; ++j) {
                int &fd = descriptors[j];
                // Keep all temporary descriptors away from child targets 0..2,
                // even when the embedding host has closed standard streams.
                if (fd < 3) {
                    int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
                    if (moved < 0) return errno;
                    close(fd);
                    fd = moved;
                }
                if (fcntl(fd, F_SETFD, FD_CLOEXEC)) return errno;
            }
        }
        // Parent stdin writer, stdout reader, stderr reader. Child ends block.
        for (int index : {1, 2, 4}) {
            const int fd = descriptors[index];
            const int flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) return errno;
        }
        return 0;
    }
    int release(int index) {
        int fd = descriptors[index];
        descriptors[index] = -1;
        return fd;
    }
};
} // namespace

extern "C" int32_t gloin_process_start(const char *executable, uint64_t size,
                                        const GloinProcessArgument *arguments, uint64_t count,
                                        int64_t *pid, int32_t *os_error) {
    return start(executable, size, arguments, count, nullptr, {}, pid, os_error);
}

extern "C" int32_t gloin_process_start_piped(const char *executable, uint64_t size,
        const GloinProcessArgument *arguments, uint64_t count, int64_t *pid,
        int32_t *input, int32_t *output, int32_t *errors, int32_t *os_error) {
    *pid = 0;
    *input = *output = *errors = -1;
    *os_error = 0;
    Pipes pipes;
    if (int error = pipes.prepare()) return failure(error, os_error);
    int streams[] = {pipes.descriptors[0], pipes.descriptors[3], pipes.descriptors[5]};
    const int32_t code = start(executable, size, arguments, count, streams, {}, pid, os_error);
    if (code != GLOIN_STD_OK) return code;
    *input = pipes.release(1);
    *output = pipes.release(2);
    *errors = pipes.release(4);
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_start_options(const char *executable, uint64_t size,
        const char *cwd, uint64_t cwd_size, const GloinProcessArgument *arguments, uint64_t count,
        const GloinProcessArgument *environment, uint64_t environment_count,
        int32_t inherit_environment, int32_t piped, int32_t new_group, uint64_t stack_bytes,
        int64_t *pid, int32_t *input, int32_t *output, int32_t *errors, int32_t *os_error) {
    *pid = 0;
    *input = *output = *errors = -1;
    *os_error = 0;
    if ((inherit_environment != 0 && inherit_environment != 1) ||
        (piped != 0 && piped != 1) || (new_group != 0 && new_group != 1)) return GLOIN_STD_INVALID;
    const LaunchOptions options{cwd, cwd_size, environment, environment_count,
                                inherit_environment != 0, new_group != 0, stack_bytes};
    if (!options_valid(options)) return GLOIN_STD_INVALID;
    if (!piped) return start(executable, size, arguments, count, nullptr, options, pid, os_error);
    Pipes pipes;
    if (int error = pipes.prepare()) return failure(error, os_error);
    int streams[] = {pipes.descriptors[0], pipes.descriptors[3], pipes.descriptors[5]};
    const int32_t code = start(executable, size, arguments, count, streams, options, pid, os_error);
    if (code != GLOIN_STD_OK) return code;
    *input = pipes.release(1);
    *output = pipes.release(2);
    *errors = pipes.release(4);
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_pipe_read(int32_t fd, uint8_t *bytes, uint64_t size,
                                            uint64_t *count, int32_t *os_error) {
    *count = 0;
    *os_error = 0;
    if (fd < 0 || !bytes || !size || size > uint64_t(SSIZE_MAX)) return GLOIN_STD_INVALID;
    const ssize_t received = read(fd, bytes, size);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return GLOIN_STD_WOULD_BLOCK;
        return failure(errno, os_error);
    }
    *count = received;
    return received ? GLOIN_STD_OK : GLOIN_STD_EOF;
}

extern "C" int32_t gloin_process_pipe_write(int32_t fd, const uint8_t *bytes, uint64_t size,
                                             uint64_t *count, int32_t *os_error) {
    *count = 0;
    *os_error = 0;
    if (fd < 0 || !bytes || !size || size > uint64_t(SSIZE_MAX)) return GLOIN_STD_INVALID;
#ifdef __APPLE__
    // Darwin can deliver a pipe signal to another unblocked host thread. Its
    // per-descriptor suppression is necessary in multithreaded hosts (the JIT).
    if (fcntl(fd, F_SETNOSIGPIPE, 1)) return failure(errno, os_error);
    const ssize_t written = write(fd, bytes, size);
    const int error = written < 0 ? errno : 0;
    if (written >= 0) *count = written;
#else
    // A broken pipe must not terminate the embedding process. Block SIGPIPE on
    // this thread only, preserving both the old mask and any preexisting signal.
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous)) return failure(error, os_error);
    if (sigpending(&pending)) {
        const int error = errno;
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        return failure(error, os_error);
    }
    const bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    const ssize_t written = write(fd, bytes, size);
    const int error = written < 0 ? errno : 0;
    if (error == EPIPE && !already_pending && !sigpending(&pending) &&
        sigismember(&pending, SIGPIPE) == 1) {
        int signal = 0;
        sigwait(&blocked, &signal);
    }
    const int restored = pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    if (written >= 0) *count = written;
    if (restored) return failure(restored, os_error);
#endif
    if (error == EPIPE) return GLOIN_STD_EOF;
    if (error == EAGAIN || error == EWOULDBLOCK || error == EINTR) return GLOIN_STD_WOULD_BLOCK;
    if (error) return failure(error, os_error);
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_pipe_close(int32_t fd, int32_t *os_error) {
    *os_error = 0;
    if (fd < 0) return GLOIN_STD_INVALID;
    // Never retry close: on supported hosts the descriptor has been consumed,
    // and another thread could already have reused its number.
    if (close(fd)) return failure(errno, os_error);
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_pipe_wait(int32_t input, int32_t output, int32_t errors,
        int32_t timeout_ms, int32_t *ready, int32_t *os_error) {
    *ready = 0;
    *os_error = 0;
    if (input < -1 || output < -1 || errors < -1 || timeout_ms < 0) return GLOIN_STD_INVALID;
    pollfd descriptors[] = {{input, POLLOUT, 0}, {output, POLLIN, 0}, {errors, POLLIN, 0}};
    const int observed = poll(descriptors, 3, timeout_ms);
    if (observed < 0) {
        // Let Gloin recompute its remaining deadline after interruptions.
        if (errno == EINTR) return GLOIN_STD_OK;
        return failure(errno, os_error);
    }
    for (int i = 0; i < 3; ++i) {
        if (descriptors[i].revents & POLLNVAL) return failure(EBADF, os_error);
        if (descriptors[i].revents & (descriptors[i].events | POLLHUP | POLLERR)) *ready |= 1 << i;
    }
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_wait(int64_t pid, int32_t block, int32_t *kind,
                                       int32_t *code, int32_t *os_error) {
    *kind = 0;
    *code = 0;
    *os_error = 0;
    if (!pid_valid(pid) || (block != 0 && block != 1)) return GLOIN_STD_INVALID;
    int status = 0;
    pid_t observed;
    do {
        observed = waitpid(static_cast<pid_t>(pid), &status, block ? 0 : WNOHANG);
    } while (observed < 0 && errno == EINTR);
    if (observed < 0) {
        *os_error = errno;
        return errno == ECHILD ? GLOIN_STD_CLOSED : gloin::io::error_status(errno);
    }
    if (!observed) return GLOIN_STD_OK;
    if (WIFEXITED(status)) {
        *kind = 1;
        *code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *kind = 2;
        *code = WTERMSIG(status);
    } else {
        return failure(EIO, os_error);
    }
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_signal(int64_t pid, int32_t force, int32_t *os_error) {
    *os_error = 0;
    if (!pid_valid(pid) || (force != 0 && force != 1)) return GLOIN_STD_INVALID;
    if (kill(static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM)) return failure(errno, os_error);
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_observe(int64_t pid, int32_t block, int32_t *kind,
                                         int32_t *code, int32_t *os_error) {
    *kind = *code = *os_error = 0;
    if (!pid_valid(pid) || (block != 0 && block != 1)) return GLOIN_STD_INVALID;
    siginfo_t information{};
    int observed;
    do {
        observed = waitid(P_PID, static_cast<id_t>(pid), &information,
                          WEXITED | WNOWAIT | (block ? 0 : WNOHANG));
    } while (observed < 0 && errno == EINTR);
    if (observed < 0) {
        *os_error = errno;
        return errno == ECHILD ? GLOIN_STD_CLOSED : gloin::io::error_status(errno);
    }
    if (!information.si_pid) return GLOIN_STD_OK;
    if (information.si_code == CLD_EXITED) *kind = 1;
    else if (information.si_code == CLD_KILLED || information.si_code == CLD_DUMPED) *kind = 2;
    else return failure(EIO, os_error);
    *code = information.si_status;
    return GLOIN_STD_OK;
}

extern "C" int32_t gloin_process_group_signal(int64_t pid, int32_t force, int32_t *os_error) {
    *os_error = 0;
    // -1 is kill's broadcast sentinel, never a process-group target here.
    if (pid <= 1 || !pid_valid(pid) || (force != 0 && force != 1)) return GLOIN_STD_INVALID;
    // Retaining the unreaped leader prevents PID/PGID reuse. Refuse to signal
    // if an embedding host has violated ownership by reaping it externally.
    int32_t kind = 0, code = 0;
    const int32_t owned = gloin_process_observe(pid, 0, &kind, &code, os_error);
    if (owned != GLOIN_STD_OK) return owned;
    const int signal = force ? SIGKILL : SIGTERM;
    if (kill(-static_cast<pid_t>(pid), signal) && errno != ESRCH) {
        int error = errno;
#ifdef __APPLE__
        if (error == EPERM) error = empty_group(static_cast<pid_t>(pid));
#endif
        if (error) return failure(error, os_error);
    }
    // The leader may itself have changed group. It remains our direct child.
    if (!kind && kill(static_cast<pid_t>(pid), signal) && errno != ESRCH) return failure(errno, os_error);
    return GLOIN_STD_OK;
}
