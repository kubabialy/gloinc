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
// Counted paths: nonempty and no NUL. Native temporary path copies are freed on return.
// lstat: kind 1=file, 2=directory, 3=symlink, 4=other. Size for file/link only.
int32_t gloin_fs_metadata(const char *path, uint64_t size, int32_t *kind, uint64_t *length,
                          int32_t *os_error);
int32_t gloin_fs_mkdir(const char *path, uint64_t size, int32_t *os_error);
int32_t gloin_fs_remove_file(const char *path, uint64_t size, int32_t *os_error);
// POSIX rename: replaces compatible existing targets; cross-filesystem moves fail.
int32_t gloin_fs_rename_replace(const char *from, uint64_t from_size, const char *to,
                                uint64_t to_size, int32_t *os_error);
#ifdef __cplusplus
}
#endif
