#include "math_runtime.h"
#include "stdlib_runtime.h"
#include <cerrno>
#include <cfenv>
#include <cmath>

#pragma STDC FENV_ACCESS ON

namespace {
// Default round-to-nearest and nontrapping exceptions for libm. Restore the entire
// environment even on failure; never leak libm errno or exception flags to callers.
class Environment {
    fenv_t saved{};
    int old_errno = errno;
    bool captured = false;
    bool ready = false;

  public:
    Environment() {
        captured = std::fegetenv(&saved) == 0;
        ready = captured && std::fesetenv(FE_DFL_ENV) == 0;
        errno = 0;
    }
    ~Environment() {
        if (captured)
            std::fesetenv(&saved);
        errno = old_errno;
    }
    bool valid() const { return ready; }
};
template <class T> int32_t finish(T result, bool nonzero_exact, T *output) {
    if (std::isnan(result) || std::fetestexcept(FE_INVALID) || errno == EDOM)
        return GLOIN_STD_INVALID;
    if (std::isinf(result) || std::fetestexcept(FE_OVERFLOW))
        return GLOIN_STD_OVERFLOW;
    // Nonzero subnormals are successful, even if libm raises underflow/ERANGE.
    if (result == T(0) && nonzero_exact)
        return GLOIN_STD_UNDERFLOW;
    *output = result;
    return GLOIN_STD_OK;
}
template <class T> int32_t unary(int32_t op, T value, T *output) {
    *output = T(0);
    Environment environment;
    if (!environment.valid() || !std::isfinite(value))
        return GLOIN_STD_INVALID;
    if ((op == GLOIN_MATH_SQRT && value < T(0)) ||
        ((op == GLOIN_MATH_LOG || op == GLOIN_MATH_LOG10) && value <= T(0)))
        return GLOIN_STD_INVALID;
    T result;
    bool nonzero_exact = false;
    switch (op) {
    case GLOIN_MATH_ABS:
        result = std::fabs(value);
        break;
    case GLOIN_MATH_FLOOR:
        result = std::floor(value);
        break;
    case GLOIN_MATH_CEIL:
        result = std::ceil(value);
        break;
    case GLOIN_MATH_TRUNC:
        result = std::trunc(value);
        break;
    case GLOIN_MATH_ROUND:
        result = std::round(value);
        break;
    case GLOIN_MATH_SQRT:
        result = std::sqrt(value);
        nonzero_exact = value != T(0);
        break;
    case GLOIN_MATH_EXP:
        result = std::exp(value);
        nonzero_exact = true;
        break;
    case GLOIN_MATH_LOG:
        result = std::log(value);
        nonzero_exact = value != T(1);
        break;
    case GLOIN_MATH_LOG10:
        result = std::log10(value);
        nonzero_exact = value != T(1);
        break;
    case GLOIN_MATH_SIN:
        result = std::sin(value);
        nonzero_exact = value != T(0);
        break;
    case GLOIN_MATH_COS:
        result = std::cos(value);
        nonzero_exact = true;
        break;
    case GLOIN_MATH_TAN:
        result = std::tan(value);
        nonzero_exact = value != T(0);
        break;
    default:
        return GLOIN_STD_INVALID;
    }
    return finish(result, nonzero_exact, output);
}
template <class T> int32_t binary(int32_t op, T first, T second, T *output) {
    *output = T(0);
    Environment environment;
    if (!environment.valid() || !std::isfinite(first) || !std::isfinite(second))
        return GLOIN_STD_INVALID;
    T result;
    bool nonzero_exact;
    switch (op) {
    case GLOIN_MATH_POW:
        if ((first == T(0) && second < T(0)) || (first < T(0) && std::trunc(second) != second))
            return GLOIN_STD_INVALID;
        // Define 0**0 as 1, including -0, independently of host pole conventions.
        if (second == T(0)) {
            *output = T(1);
            return GLOIN_STD_OK;
        }
        result = std::pow(first, second);
        nonzero_exact = first != T(0);
        break;
    case GLOIN_MATH_ATAN2:
        // Order is (y,x); axes and the four signed-zero origins follow atan2.
        result = std::atan2(first, second);
        nonzero_exact = first != T(0);
        break;
    case GLOIN_MATH_HYPOT:
        result = std::hypot(first, second);
        nonzero_exact = first != T(0) || second != T(0);
        break;
    default:
        return GLOIN_STD_INVALID;
    }
    return finish(result, nonzero_exact, output);
}
} // namespace
extern "C" int32_t gloin_math_unary_f32(int32_t op, float value, float *output) {
    return unary(op, value, output);
}
extern "C" int32_t gloin_math_unary_f64(int32_t op, double value, double *output) {
    return unary(op, value, output);
}
extern "C" int32_t gloin_math_binary_f32(int32_t op, float first, float second, float *output) {
    return binary(op, first, second, output);
}
extern "C" int32_t gloin_math_binary_f64(int32_t op, double first, double second, double *output) {
    return binary(op, first, second, output);
}
