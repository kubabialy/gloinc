#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Stable statuses, mirrored by status.gloin (std.gloin retains compatibility aliases).
enum {
    GLOIN_STD_OK = 0,
    GLOIN_STD_EOF = 1,
    GLOIN_STD_INVALID = 2,
    GLOIN_STD_OVERFLOW = 3,
    GLOIN_STD_IO_ERROR = 4,
    GLOIN_STD_TOO_LONG = 5,
    GLOIN_STD_NO_MEMORY = 6,
    GLOIN_STD_OUT_OF_RANGE = 7,
    GLOIN_STD_INEXACT = 8,
    GLOIN_STD_UNDERFLOW = 9,
    GLOIN_STD_NOT_FOUND = 10,
    GLOIN_STD_PERMISSION_DENIED = 11,
    GLOIN_STD_ALREADY_EXISTS = 12,
    GLOIN_STD_CLOSED = 13
};
int32_t gloin_std_parse_i32(const char *bytes, uint64_t length, int32_t *value);
// Destination has at least 12 bytes, including the trailing NUL.
void gloin_std_format_i32(int32_t value, char *bytes, uint64_t *length);
// Destination has limit + 1 bytes. Consumes one whole line, including on TOO_LONG.
int32_t gloin_std_input(char *bytes, uint64_t limit, uint64_t *length);
// Copy exactly length bytes, without a terminator. Both ranges must be live,
// at least length bytes, and nonoverlapping. Zero length accesses neither pointer.
// O(length), no allocation; the Gloin wrapper supplies fresh arena storage.
void gloin_strings_copy(const char *source, uint64_t length, char *destination);
// Numeric extensions: failed scalar outputs are zero, failed formatted outputs
// have length zero and bytes[0]=NUL. Parse needs no allocation; format buffers
// require 21 bytes for integers, 32 for shortest floats, 64/352 for fixed f32/f64.
// Mode 0 is exact, 1 is rounded (integer/float narrowing) or truncating (float/integer).
int32_t gloin_std_parse_i64(const char *bytes, uint64_t length, int64_t *value);
int32_t gloin_std_parse_u64(const char *bytes, uint64_t length, uint64_t *value);
int32_t gloin_std_parse_f32(const char *bytes, uint64_t length, float *value);
int32_t gloin_std_parse_f64(const char *bytes, uint64_t length, double *value);
int32_t gloin_std_parse_bool(const char *bytes, uint64_t length, uint8_t *value);
int32_t gloin_std_format_i64(int64_t value, char *bytes, uint64_t *length);
int32_t gloin_std_format_u64(uint64_t value, char *bytes, uint64_t *length);
int32_t gloin_std_format_f32(float value, char *bytes, uint64_t *length);
int32_t gloin_std_format_f64(double value, char *bytes, uint64_t *length);
int32_t gloin_std_format_f32_fixed(float value, uint32_t precision, char *bytes, uint64_t *length);
int32_t gloin_std_format_f64_fixed(double value, uint32_t precision, char *bytes, uint64_t *length);
int32_t gloin_std_i64_from_i32(int32_t value, int64_t *output);
int32_t gloin_std_i32_from_i64(int64_t value, int32_t *output);
int32_t gloin_std_u64_from_i64(int64_t value, uint64_t *output);
int32_t gloin_std_i64_from_u64(uint64_t value, int64_t *output);
int32_t gloin_std_f64_from_i64(int64_t value, int32_t mode, double *output);
int32_t gloin_std_f64_from_u64(uint64_t value, int32_t mode, double *output);
int32_t gloin_std_i64_from_f64(double value, int32_t mode, int64_t *output);
int32_t gloin_std_u64_from_f64(double value, int32_t mode, uint64_t *output);
int32_t gloin_std_f64_from_f32(float value, double *output);
int32_t gloin_std_f32_from_f64(double value, int32_t mode, float *output);
// External LLVM execution uses the same reserved byte-output ABI as the JIT.
#ifdef __APPLE__
void gloin_std_output(const char *bytes, uint64_t length) __asm__("_gloin.runtime.output");
#else
void gloin_std_output(const char *bytes, uint64_t length) __asm__("gloin.runtime.output");
#endif
#ifdef __cplusplus
}
#endif
