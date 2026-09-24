#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Private math.gloin dispatch ABI; only these enumerated operations are accepted.
// Finite inputs only. Every failure writes +0. No allocation or retained pointers.
// Calls isolate libm in the default floating environment and restore caller errno,
// rounding mode and exception flags. Output must point to writable scalar storage.
enum {
    GLOIN_MATH_ABS = 0,
    GLOIN_MATH_FLOOR,
    GLOIN_MATH_CEIL,
    GLOIN_MATH_TRUNC,
    GLOIN_MATH_ROUND,
    GLOIN_MATH_SQRT,
    GLOIN_MATH_EXP,
    GLOIN_MATH_LOG,
    GLOIN_MATH_LOG10,
    GLOIN_MATH_SIN,
    GLOIN_MATH_COS,
    GLOIN_MATH_TAN
};
enum { GLOIN_MATH_POW = 0, GLOIN_MATH_ATAN2, GLOIN_MATH_HYPOT };
int32_t gloin_math_unary_f32(int32_t operation, float value, float *output);
int32_t gloin_math_unary_f64(int32_t operation, double value, double *output);
int32_t gloin_math_binary_f32(int32_t operation, float first, float second, float *output);
int32_t gloin_math_binary_f64(int32_t operation, double first, double second, double *output);
#ifdef __cplusplus
}
#endif
