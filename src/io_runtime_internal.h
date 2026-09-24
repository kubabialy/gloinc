#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
namespace gloin::io {
// Per-call injection, never process-global hooks. Exercises partial/progress
// handling independently from a host stdio implementation's internal retries.
struct WriteAttempt {
    size_t count;
    int error;
};
using Writer = WriteAttempt (*)(void *, const char *, size_t);
int32_t write(void *context, Writer writer, const char *bytes, uint64_t length, bool all,
              uint64_t *written, int32_t *os_error) noexcept;
int32_t error_status(int error) noexcept;
} // namespace gloin::io
