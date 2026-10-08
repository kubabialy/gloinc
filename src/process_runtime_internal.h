#pragma once
#include <cstdint>
#include <sys/types.h>

namespace gloin::process {
// Parent prepares all storage. A successful return transfers one unreaped PID;
// setup/exec errors are returned synchronously and leave no child to reap.
int start_limited(const char *executable, char *const *arguments, char *const *environment,
                  const int *streams, const char *cwd, bool new_group,
                  uint64_t stack_bytes, pid_t *child);
}
