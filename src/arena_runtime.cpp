#include "arena_runtime.h"
#include "arena_runtime_internal.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace {
constexpr size_t initial_capacity = 64 * 1024;
constexpr size_t growth_limit = 1024 * 1024;
thread_local const gloin::arena::Allocator *current_allocator = nullptr;
struct Block {
    Block *next;
    size_t capacity;
    size_t used;
    std::byte *data() { return reinterpret_cast<std::byte *>(this + 1); }
};
struct State;
struct OwnedBlock {
    State *owner;
    OwnedBlock *previous;
    OwnedBlock *next;
    void *allocation;
};
struct State {
    gloin::arena::Allocator allocator;
    Block *head = nullptr;
    Block *tail = nullptr;
    Block *current = nullptr;
    size_t next_capacity = initial_capacity;
    OwnedBlock *owned = nullptr;
};

void *reserve(Block *block, size_t size, size_t alignment) noexcept {
    void *pointer = block->data() + block->used;
    size_t space = block->capacity - block->used;
    if (!std::align(alignment, size, pointer, space))
        return nullptr;
    block->used = static_cast<std::byte *>(pointer) - block->data() + size;
    return pointer;
}
void *allocate(void *, size_t size) noexcept { return std::malloc(size); }
void release(void *, void *pointer) noexcept { std::free(pointer); }

void release_owned(State *state, OwnedBlock *block) noexcept {
    if (block->previous)
        block->previous->next = block->next;
    else
        state->owned = block->next;
    if (block->next)
        block->next->previous = block->previous;
    state->allocator.release(state->allocator.context, block->allocation);
}

void release_all_owned(State *state) noexcept {
    while (state->owned)
        release_owned(state, state->owned);
}
} // namespace

extern "C" void gloin_arena_zero_bytes(void *bytes, uint64_t size) {
    if (size)
        std::memset(bytes, 0, static_cast<size_t>(size));
}

void *gloin::arena::create(Allocator allocator) noexcept {
    auto *memory = allocator.allocate(allocator.context, sizeof(State));
    return memory ? new (memory) State{allocator} : nullptr;
}

extern "C" void *gloin_arena_general_create() {
    return gloin::arena::create(current_allocator
                                    ? *current_allocator
                                    : gloin::arena::Allocator{nullptr, allocate, release});
}

gloin::arena::AllocatorScope::AllocatorScope(Allocator value) noexcept
    : allocator(value), previous(current_allocator) {
    current_allocator = &allocator;
}
gloin::arena::AllocatorScope::~AllocatorScope() { current_allocator = previous; }

extern "C" void *gloin_arena_general_alloc(void *opaque, uint64_t bytes, uint64_t alignment) {
    // Keeping the entire block within PTRDIFF_MAX also makes all internal
    // pointer differences representable. Validate before changing any state.
    constexpr auto limit = static_cast<uint64_t>(std::numeric_limits<ptrdiff_t>::max());
    if (!opaque || !alignment || (alignment & (alignment - 1)) || alignment > limit ||
        bytes > limit)
        return nullptr;
    const auto size = static_cast<size_t>(std::max<uint64_t>(bytes, 1));
    const auto align = static_cast<size_t>(alignment);
    if (size > limit - (align - 1) || size + (align - 1) > limit - sizeof(Block))
        return nullptr;
    auto *state = static_cast<State *>(opaque);
    for (auto *block = state->current; block; block = block->next) {
        if (auto *pointer = reserve(block, size, align)) {
            state->current = block;
            return pointer;
        }
    }
    const auto capacity = std::max(state->next_capacity, size);
    if (capacity > limit - sizeof(Block) - (align - 1))
        return nullptr;
    const auto storage = capacity + align - 1;
    auto *memory = state->allocator.allocate(state->allocator.context, sizeof(Block) + storage);
    if (!memory)
        return nullptr;
    auto *block = new (memory) Block{nullptr, storage, 0};
    auto *pointer = reserve(block, size, align);
    // The extra alignment bytes guarantee success. Keep failure transactional
    // even if the backing allocator is replaced during native testing.
    if (!pointer) {
        state->allocator.release(state->allocator.context, memory);
        return nullptr;
    }
    if (state->tail)
        state->tail->next = block;
    else
        state->head = block;
    state->tail = state->current = block;
    state->next_capacity = std::min(state->next_capacity * 2, growth_limit);
    return pointer;
}

extern "C" void *gloin_arena_owned_alloc(void *opaque, uint64_t bytes, uint64_t alignment) {
    constexpr auto limit = static_cast<uint64_t>(std::numeric_limits<ptrdiff_t>::max());
    if (!opaque || !alignment || (alignment & (alignment - 1)) || alignment > limit ||
        bytes > limit)
        return nullptr;
    const auto size = static_cast<size_t>(std::max<uint64_t>(bytes, 1));
    const auto align = static_cast<size_t>(std::max<uint64_t>(alignment, alignof(OwnedBlock)));
    if (size > limit - sizeof(OwnedBlock) || size + sizeof(OwnedBlock) > limit - (align - 1))
        return nullptr;
    const auto total = sizeof(OwnedBlock) + size + align - 1;
    auto *state = static_cast<State *>(opaque);
    auto *allocation = state->allocator.allocate(state->allocator.context, total);
    if (!allocation)
        return nullptr;
    void *storage = static_cast<std::byte *>(allocation) + sizeof(OwnedBlock);
    size_t space = total - sizeof(OwnedBlock);
    if (!std::align(align, size, storage, space)) {
        state->allocator.release(state->allocator.context, allocation);
        return nullptr;
    }
    // The aligned header immediately precedes the payload, allowing O(1)
    // unlinking without searching other maps' storage or the bump blocks.
    auto *header = static_cast<std::byte *>(storage) - sizeof(OwnedBlock);
    auto *block = new (header) OwnedBlock{state, nullptr, state->owned, allocation};
    if (state->owned)
        state->owned->previous = block;
    state->owned = block;
    return storage;
}

extern "C" void gloin_arena_owned_release(void *opaque, void *storage) {
    if (!storage)
        return;
    auto *state = static_cast<State *>(opaque);
    auto *block =
        reinterpret_cast<OwnedBlock *>(static_cast<std::byte *>(storage) - sizeof(OwnedBlock));
    // Pointer validity is a caller obligation, as for raw memory.free. A live
    // block from another owner must never corrupt either arena's linked list.
    if (!state || block->owner != state)
        std::abort();
    release_owned(state, block);
}

extern "C" void gloin_arena_general_reset(void *opaque) {
    auto *state = static_cast<State *>(opaque);
    release_all_owned(state);
    for (auto *block = state->head; block; block = block->next)
        block->used = 0;
    state->current = state->head;
}

extern "C" void gloin_arena_general_destroy(void *opaque) {
    if (!opaque)
        return;
    auto *state = static_cast<State *>(opaque);
    release_all_owned(state);
    const auto allocator = state->allocator;
    auto *block = state->head;
    while (block) {
        auto *next = block->next;
        allocator.release(allocator.context, block);
        block = next;
    }
    allocator.release(allocator.context, state);
}
