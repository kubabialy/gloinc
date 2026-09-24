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
// Host/test scope for the backing allocator used by newly created arenas on this thread.
// Both callbacks must be non-null. Existing arenas retain their allocator. Callbacks/context must
// outlive all arenas created in the scope, even if the scope has ended. Scopes are nonmovable and
// strictly nested. The default outside a scope remains malloc/free. No source-language allocation
// policy changes.
class AllocatorScope {
    Allocator allocator;
    const Allocator *previous;

  public:
    explicit AllocatorScope(Allocator value) noexcept;
    ~AllocatorScope();
    AllocatorScope(const AllocatorScope &) = delete;
    AllocatorScope &operator=(const AllocatorScope &) = delete;
};
} // namespace gloin::arena
