#include "arena_runtime.h"
#include "arena_runtime_internal.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

namespace {
constexpr size_t initial_capacity = 64 * 1024;
constexpr size_t growth_limit = 1024 * 1024;
struct Block {
    Block *next;
    size_t capacity;
    size_t used;
    std::byte *data() { return reinterpret_cast<std::byte *>(this + 1); }
};
struct State {
    gloin::arena::Allocator allocator;
    Block *head = nullptr;
    Block *tail = nullptr;
    Block *current = nullptr;
    size_t next_capacity = initial_capacity;
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
} // namespace

void *gloin::arena::create(Allocator allocator) noexcept {
    auto *memory = allocator.allocate(allocator.context, sizeof(State));
    return memory ? new (memory) State{allocator} : nullptr;
}

extern "C" void *gloin_arena_general_create() {
    return gloin::arena::create({nullptr, allocate, release});
}

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

extern "C" void gloin_arena_general_reset(void *opaque) {
    auto *state = static_cast<State *>(opaque);
    for (auto *block = state->head; block; block = block->next)
        block->used = 0;
    state->current = state->head;
}

extern "C" void gloin_arena_general_destroy(void *opaque) {
    if (!opaque)
        return;
    auto *state = static_cast<State *>(opaque);
    const auto allocator = state->allocator;
    auto *block = state->head;
    while (block) {
        auto *next = block->next;
        allocator.release(allocator.context, block);
        block = next;
    }
    allocator.release(allocator.context, state);
}
