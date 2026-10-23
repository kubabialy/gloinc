#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Internal table hashes are nonnegative i64 values. They are deterministic,
// noncryptographic, and are not a persistent serialization format.
int64_t gloin_hashmap_word(uint64_t value);
int64_t gloin_hashmap_string(const char *bytes, uint64_t length);
int32_t gloin_hashmap_string_equal(const char *left, uint64_t left_length,
                                 const char *right, uint64_t right_length);
#ifdef __cplusplus
}
#endif
