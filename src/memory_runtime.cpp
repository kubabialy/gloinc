#include "memory_runtime.h"
#include <cstddef>
#include <cstdlib>
#include <limits>

extern "C" void *gloin_memory_alloc(uint64_t bytes, uint64_t alignment) {
    constexpr auto limit = static_cast<uint64_t>(std::numeric_limits<ptrdiff_t>::max());
    if (!alignment || (alignment & (alignment - 1)) || alignment > limit || bytes > limit)
        return nullptr;
    const auto required = static_cast<size_t>(bytes ? bytes : 1);
    const auto native_alignment = static_cast<size_t>(
        alignment < alignof(void *) ? alignof(void *) : alignment);
    if (required > limit - (native_alignment - 1))
        return nullptr;
    void *result = nullptr;
    return posix_memalign(&result, native_alignment, required) == 0 ? result : nullptr;
}

extern "C" void gloin_memory_free(void *pointer) { std::free(pointer); }
