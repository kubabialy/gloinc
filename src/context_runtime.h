#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Host embedding API. Deep-copy NUL-terminated argument bytes into a thread-local
// invocation scope. Null return means invalid inputs/allocation failure; prior
// context is unchanged. Pop on the same thread in LIFO order. Default count is 0.
void *gloin_process_arguments_push(uint64_t count, const char *const *arguments);
int32_t gloin_process_arguments_pop(void *scope);
uint64_t gloin_process_arg_count(void);
// Returned arg/env bytes are borrowed for immediate copying by process.gloin.
// Args survive until scope pop; env requires no concurrent host environment mutation.
const char *gloin_process_arg(uint64_t index, uint64_t *length, int32_t *status);
const char *gloin_process_env(const char *name, uint64_t size, uint64_t *length, int32_t *status);
// Buffer capacity includes NUL. No truncation; ERANGE -> TOO_LONG, length 0.
int32_t gloin_process_cwd(char *bytes, uint64_t capacity, uint64_t *length, int32_t *os_error);
// Layout of one Gloin string in a contiguous argument slice (supported 64-bit targets).
typedef struct GloinProcessArgument {
    const char *bytes;
    uint64_t size;
} GloinProcessArgument;
// Explicit executable path, no PATH search or shell. argv[0] is executable.
// Copies counted inputs before launch; only descriptors 0..2 are inherited.
int32_t gloin_process_start(const char *executable, uint64_t size,
                            const GloinProcessArgument *arguments, uint64_t count,
                            int64_t *pid, int32_t *os_error);
// block is 0 (poll) or 1 (wait). kind: 0=running, 1=exited, 2=signaled.
// A terminal observation reaps the child. EINTR is retried; ECHILD -> CLOSED.
int32_t gloin_process_wait(int64_t pid, int32_t block, int32_t *kind,
                           int32_t *code, int32_t *os_error);
// force is 0 (SIGTERM) or 1 (SIGKILL). Does not wait/reap.
int32_t gloin_process_signal(int64_t pid, int32_t force, int32_t *os_error);
// Piped launch replaces all three standard streams. Parent ends are nonblocking
// and CLOEXEC; child ends block. Failure returns pid=0, descriptors=-1.
int32_t gloin_process_start_piped(const char *executable, uint64_t size,
    const GloinProcessArgument *arguments, uint64_t count, int64_t *pid,
    int32_t *input, int32_t *output, int32_t *errors, int32_t *os_error);
// Explicit launch options. Empty cwd inherits; relative executable resolves in
// the child cwd. Supplied environment replaces inheritance (even when empty).
// Entries are unique nonempty NAME=value strings. No shell/PATH lookup.
// new_group creates PGID=PID; retain the leader with observe until final wait.
// stack_bytes=0 inherits; otherwise set the child's soft RLIMIT_STACK before
// exec, preserving its inherited hard limit. Parent limits remain unchanged.
int32_t gloin_process_start_options(const char *executable, uint64_t size,
    const char *cwd, uint64_t cwd_size, const GloinProcessArgument *arguments, uint64_t count,
    const GloinProcessArgument *environment, uint64_t environment_count,
    int32_t inherit_environment, int32_t piped, int32_t new_group, uint64_t stack_bytes,
    int64_t *pid, int32_t *input, int32_t *output, int32_t *errors, int32_t *os_error);
// Same kind/code contract as wait, but WNOWAIT retains terminal status/PID.
int32_t gloin_process_observe(int64_t pid, int32_t block, int32_t *kind,
    int32_t *code, int32_t *os_error);
// Only for an unreaped, owned child launched with PGID=PID. Empty group is OK.
// Observation first verifies child ownership; embedders must coordinate reaping.
int32_t gloin_process_group_signal(int64_t pid, int32_t force, int32_t *os_error);
// Single read/write; nonempty buffers. OK/END/WOULD_BLOCK are ordinary outcomes.
// Write suppresses only its own SIGPIPE, preserving the host's signal policy.
int32_t gloin_process_pipe_read(int32_t fd, uint8_t *bytes, uint64_t size,
    uint64_t *count, int32_t *os_error);
int32_t gloin_process_pipe_write(int32_t fd, const uint8_t *bytes, uint64_t size,
    uint64_t *count, int32_t *os_error);
// Consumes fd even on close failure; never retry with the same descriptor number.
int32_t gloin_process_pipe_close(int32_t fd, int32_t *os_error);
// -1 disables a stream. ready bits: stdin=1, stdout=2, stderr=4. timeout >= 0.
// EINTR returns OK/ready=0 so callers can recompute deadlines. HUP/ERR are ready.
int32_t gloin_process_pipe_wait(int32_t input, int32_t output, int32_t errors,
    int32_t timeout_ms, int32_t *ready, int32_t *os_error);
// Counted paths: nonempty and no NUL. Native temporary path copies are freed on return.
// lstat: kind 1=file, 2=directory, 3=symlink, 4=other. Size for file/link only.
int32_t gloin_fs_metadata(const char *path, uint64_t size, int32_t *kind, uint64_t *length,
                          int32_t *os_error);
int32_t gloin_fs_mkdir(const char *path, uint64_t size, int32_t *os_error);
int32_t gloin_fs_remove_file(const char *path, uint64_t size, int32_t *os_error);
// POSIX rename: replaces compatible existing targets; cross-filesystem moves fail.
int32_t gloin_fs_rename_replace(const char *from, uint64_t from_size, const char *to,
                                uint64_t to_size, int32_t *os_error);
// Store target text verbatim; existing link_path is never replaced.
int32_t gloin_fs_symlink(const char *target, uint64_t target_size, const char *link_path,
                         uint64_t link_size, int32_t *os_error);
// Capacity includes one overflow probe byte: success requires length < capacity.
// Native request is capped at INT32_MAX; filling that cap also returns TOO_LONG.
// No NUL terminator. TOO_LONG returns length=0; bytes are unspecified on failure.
int32_t gloin_fs_read_link(const char *path, uint64_t size, uint8_t *bytes,
                           uint64_t capacity, uint64_t *length, int32_t *os_error);
// Resolve an existing path. Capacity includes NUL; insufficient space -> TOO_LONG.
// Temporary native realpath storage is freed before return. Failure length is 0.
int32_t gloin_fs_canonical_path(const char *path, uint64_t size, uint8_t *bytes,
                               uint64_t capacity, uint64_t *length, int32_t *os_error);
// Template ends in XXXXXX. Validate/copy to caller storage BEFORE exclusive mkdir.
// Capacity includes NUL. No allocation or fallible copying after directory creation.
int32_t gloin_fs_temp_dir(const char *pattern, uint64_t size, uint8_t *bytes,
                         uint64_t capacity, uint64_t *length, int32_t *os_error);
// Strip trailing slashes; refuse root and final "." or "..". Never recurse/follow a final link.
int32_t gloin_fs_remove_dir(const char *path, uint64_t size, int32_t *os_error);
// Replace an existing singly-linked regular file owned by this user, after
// comparing expected bytes. Stages beside it, preserves uid/gid and mode 0777.
// Requires exclusive editing; final check+rename is not a compare-and-swap.
int32_t gloin_fs_replace_file(const char *path, uint64_t path_size,
    const char *expected, uint64_t expected_size, const char *replacement, uint64_t replacement_size,
    int32_t *os_error);
// Directory cursor. Entries are relative names, excluding "." and "..".
// next() borrows bytes only until the following next() or close(). Order is host-defined.
int32_t gloin_fs_dir_open(const char *path, uint64_t size, void **handle, int32_t *os_error);
const char *gloin_fs_dir_next(void *handle, uint64_t *length, int32_t *status,
                              int32_t *os_error);
int32_t gloin_fs_dir_close(void *handle, int32_t *os_error);
#ifdef __cplusplus
}
#endif
