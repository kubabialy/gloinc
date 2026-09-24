#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void *gloin_arena_general_create(void);
void *gloin_arena_general_alloc(void *state, uint64_t size, uint64_t alignment);
void gloin_arena_general_reset(void *state);
void gloin_arena_general_destroy(void *state);
void gloin_arena_zero_bytes(void *bytes, uint64_t size);
#ifdef __cplusplus
}
#endif
