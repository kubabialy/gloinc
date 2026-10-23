#include "hashmap_runtime.h"
#include <cstring>

extern "C" int64_t gloin_hashmap_word(uint64_t value) {
    // The SplitMix64 finalizer avalanches both high and low input bits.
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return static_cast<int64_t>((value ^ (value >> 31)) & INT64_MAX);
}

extern "C" int64_t gloin_hashmap_string(const char *bytes, uint64_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint64_t i = 0; i < length; ++i) {
        hash ^= static_cast<unsigned char>(bytes[i]);
        hash *= UINT64_C(1099511628211);
    }
    return gloin_hashmap_word(hash);
}

extern "C" int32_t gloin_hashmap_string_equal(const char *left, uint64_t left_length,
                                             const char *right, uint64_t right_length) {
    return left_length == right_length &&
           (left_length == 0 || std::memcmp(left, right, left_length) == 0);
}
