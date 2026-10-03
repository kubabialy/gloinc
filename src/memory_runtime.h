#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Returns null for invalid alignment, overflow, or allocation failure.
// Zero bytes still reserves a distinct address. Release accepts null.
void *gloin_memory_alloc(uint64_t bytes, uint64_t alignment);
void gloin_memory_free(void *pointer);
#ifdef __cplusplus
}
#endif
