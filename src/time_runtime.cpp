#include "stdlib_runtime.h"
#include "time_runtime_internal.h"
#include <cerrno>
#include <limits>
#include <new>
#include <time.h>
namespace {
struct Binding {
    GloinClockSource source;
    Binding *previous;
};
thread_local Binding *current = nullptr;
struct ErrnoGuard {
    int saved = errno;
    ~ErrnoGuard() { errno = saved; }
};
} // namespace
extern "C" void *gloin_time_clock_push(const GloinClockSource *source) {
    if (source && !source->read)
        return nullptr;
    auto *binding = new (std::nothrow) Binding{source ? *source : GloinClockSource{}, current};
    if (binding)
        current = binding;
    return binding;
}
extern "C" int32_t gloin_time_clock_pop(void *scope) {
    if (!scope || scope != current)
        return GLOIN_STD_INVALID;
    auto *old = current;
    current = old->previous;
    delete old;
    return GLOIN_STD_OK;
}
gloin::time::ClockScope::ClockScope(const GloinClockSource *source) noexcept
    : scope(gloin_time_clock_push(source)) {}
gloin::time::ClockScope::~ClockScope() {
    if (scope)
        gloin_time_clock_pop(scope);
}
int32_t gloin::time::checked_nanoseconds(int64_t seconds, int64_t nanoseconds, uint64_t *result) {
    *result = 0;
    if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1000000000)
        return GLOIN_STD_INVALID;
    const auto s = uint64_t(seconds), ns = uint64_t(nanoseconds);
    if (s > (std::numeric_limits<uint64_t>::max() - ns) / 1000000000)
        return GLOIN_STD_OVERFLOW;
    *result = s * 1000000000 + ns;
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_time_monotonic(uint64_t *nanoseconds, int32_t *os_error) {
    ErrnoGuard guard;
    *nanoseconds = 0;
    *os_error = 0;
    if (current && current->source.read) {
        uint64_t ticks = 0;
        int32_t error = 0;
        const int32_t code = current->source.read(current->source.userdata, &ticks, &error);
        if (code == GLOIN_STD_OK) {
            *nanoseconds = ticks;
            return code;
        }
        if (code == GLOIN_STD_IO_ERROR && error >= 0) {
            *os_error = error ? error : EIO;
            return code;
        }
        if (code == GLOIN_STD_OVERFLOW)
            return code;
        return GLOIN_STD_INVALID;
    }
    timespec now{};
    errno = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        *os_error = errno ? errno : EIO;
        return GLOIN_STD_IO_ERROR;
    }
    return gloin::time::checked_nanoseconds(now.tv_sec, now.tv_nsec, nanoseconds);
}
