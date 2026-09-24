#pragma once
#include "time_runtime.h"
namespace gloin::time {
// Independently testable conversion of native seconds/nanoseconds, without sleeping.
int32_t checked_nanoseconds(int64_t seconds, int64_t nanoseconds, uint64_t *result);
class ClockScope {
    void *scope = nullptr;

  public:
    explicit ClockScope(const GloinClockSource *source) noexcept;
    ~ClockScope();
    bool valid() const { return scope != nullptr; }
    ClockScope(const ClockScope &) = delete;
    ClockScope &operator=(const ClockScope &) = delete;
};
} // namespace gloin::time
