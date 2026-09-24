#include "random_runtime.h"
// SplitMix64 fixed-increment transition from Sebastiano Vigna's 2015 public-domain
// reference: https://prng.di.unimi.it/splitmix64.c . State is caller-owned here.
// Algorithm version and constants are a reproducibility contract, not tunables.
extern "C" uint64_t gloin_random_splitmix64(uint64_t *state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
