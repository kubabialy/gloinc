#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Host callback: synchronous, must not throw or recursively read the same source.
// Return OK, IO_ERROR, INVALID, or OVERFLOW. On OK fill nanoseconds; on IO_ERROR
// optionally fill errno. userdata is borrowed until the scope is popped/run ends.
typedef int32_t (*GloinClockRead)(void *userdata, uint64_t *nanoseconds, int32_t *os_error);
typedef struct GloinClockSource {
    GloinClockRead read;
    void *userdata;
} GloinClockSource;
// Copy the descriptor into a thread-local scope; NULL source selects the OS clock.
// Non-NULL source with NULL read is invalid. NULL token: invalid or allocation failure,
// with the prior scope unchanged. Pop on the same thread, in LIFO order; tokens expire.
void *gloin_time_clock_push(const GloinClockSource *source);
int32_t gloin_time_clock_pop(void *scope);
// Read current provider, or CLOCK_MONOTONIC when unbound. No allocation. All failures
// write zero nanoseconds; only IO_ERROR carries os_error (EIO fallback). Restores errno.
int32_t gloin_time_monotonic(uint64_t *nanoseconds, int32_t *os_error);
#ifdef __cplusplus
}
#endif
