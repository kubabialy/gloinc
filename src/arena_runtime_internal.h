#pragma once
#include <cstddef>

namespace gloin::arena {
// Per-arena backing allocator. Tests can inject failures without global state or
// exposing fault injection to source programs. Callbacks must not throw.
struct Allocator {
    void *context;
    void *(*allocate)(void *, size_t) noexcept;
    void (*release)(void *, void *) noexcept;
};
void *create(Allocator allocator) noexcept;
} // namespace gloin::arena
