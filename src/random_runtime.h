#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// SplitMix64-v1: update one caller-owned u64 by fixed-increment wrapping arithmetic
// and return its mixed value. No global state, allocation, seed restrictions, or failure.
// state must point to a live writable u64. Sharing it across threads needs synchronization.
uint64_t gloin_random_splitmix64(uint64_t *state);
#ifdef __cplusplus
}
#endif
