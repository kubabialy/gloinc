#pragma once
#include <cstdint>
#include <cstdio>
namespace gloin::standard {
// Injectable stream keeps native tests independent of global stdin.
int32_t input(FILE *stream, char *bytes, uint64_t limit, uint64_t *length,
              int32_t *os_error = nullptr) noexcept;
} // namespace gloin::standard
