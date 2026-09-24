#include "stdlib_runtime.h"
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fast_float/fast_float.h>
#include <limits>
#include <type_traits>

namespace {
template <typename T> int32_t parse_integer(const char *bytes, uint64_t length, T *output) {
    *output = 0;
    if (!length)
        return GLOIN_STD_INVALID;
    uint64_t position = 0;
    bool negative = bytes[0] == '-';
    if (negative && !std::is_signed_v<T>)
        return GLOIN_STD_INVALID;
    if (negative || bytes[0] == '+')
        ++position;
    if (position == length)
        return GLOIN_STD_INVALID;
    const uint64_t limit = negative ? uint64_t(std::numeric_limits<T>::max()) + 1
                                    : uint64_t(std::numeric_limits<T>::max());
    uint64_t value = 0;
    bool overflow = false;
    for (; position < length; ++position) {
        const unsigned char byte = bytes[position];
        if (byte < '0' || byte > '9')
            return GLOIN_STD_INVALID;
        const uint64_t digit = byte - '0';
        if (value > (limit - digit) / 10)
            overflow = true;
        else if (!overflow)
            value = value * 10 + digit;
    }
    if (overflow)
        return GLOIN_STD_OVERFLOW;
    if constexpr (std::is_signed_v<T>) {
        if (negative && value == uint64_t(std::numeric_limits<T>::max()) + 1)
            *output = std::numeric_limits<T>::min();
        else
            *output = negative ? -static_cast<T>(value) : static_cast<T>(value);
    } else
        *output = value;
    return GLOIN_STD_OK;
}

// Check the whole counted grammar before range conversion. This also rejects
// fast_float's optional nonfinite spellings and tracks only mantissa nonzeroes.
bool decimal_grammar(const char *bytes, uint64_t length, bool &nonzero) {
    if (!length)
        return false;
    uint64_t position = 0;
    if (bytes[0] == '+' || bytes[0] == '-')
        ++position;
    auto digits = [&](bool mantissa) {
        const auto start = position;
        while (position < length && bytes[position] >= '0' && bytes[position] <= '9') {
            if (mantissa && bytes[position] != '0')
                nonzero = true;
            ++position;
        }
        return position != start;
    };
    bool any = digits(true);
    if (position < length && bytes[position] == '.') {
        ++position;
        any = digits(true) || any;
    }
    if (!any)
        return false;
    if (position < length && (bytes[position] == 'e' || bytes[position] == 'E')) {
        ++position;
        if (position < length && (bytes[position] == '+' || bytes[position] == '-'))
            ++position;
        if (!digits(false))
            return false;
    }
    return position == length;
}

template <typename T> int32_t parse_float(const char *bytes, uint64_t length, T *output) {
    *output = 0;
    bool nonzero = false;
    if (!decimal_grammar(bytes, length, nonzero))
        return GLOIN_STD_INVALID;
    if (!nonzero) {
        *output = bytes[0] == '-' ? -T(0) : T(0);
        return GLOIN_STD_OK;
    }
    T value = 0;
    const auto parsed = fast_float::from_chars(bytes + (bytes[0] == '+'), bytes + length, value);
    if (!std::isfinite(value))
        return GLOIN_STD_OVERFLOW;
    if (value == 0)
        return GLOIN_STD_UNDERFLOW;
    if (parsed.ec != std::errc{} || parsed.ptr != bytes + length)
        return GLOIN_STD_INVALID;
    *output = value;
    return GLOIN_STD_OK;
}

template <typename T>
int32_t format(T value, char *bytes, uint64_t *length, size_t capacity, int precision = -1) {
    *length = 0;
    bytes[0] = '\0';
    std::to_chars_result result;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value))
            return GLOIN_STD_INVALID;
        if (precision < 0)
            result = std::to_chars(bytes, bytes + capacity - 1, value);
        else
            result = std::to_chars(bytes, bytes + capacity - 1, value, std::chars_format::fixed,
                                   precision);
    } else
        result = std::to_chars(bytes, bytes + capacity - 1, value);
    if (result.ec != std::errc{}) {
        bytes[0] = '\0';
        return GLOIN_STD_OVERFLOW;
    }
    *length = static_cast<uint64_t>(result.ptr - bytes);
    bytes[*length] = '\0';
    return GLOIN_STD_OK;
}

template <typename T>
int32_t format_fixed(T value, uint32_t precision, char *bytes, uint64_t *length, size_t capacity) {
    *length = 0;
    bytes[0] = '\0';
    if (precision > 18)
        return GLOIN_STD_INVALID;
    return format(value, bytes, length, capacity, static_cast<int>(precision));
}

template <typename T> int32_t integer_to_f64(T value, int32_t mode, double *output) {
    *output = 0;
    if (mode != 0 && mode != 1)
        return GLOIN_STD_INVALID;
    uint64_t magnitude;
    if constexpr (std::is_signed_v<T>)
        magnitude = value < 0 ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
    else
        magnitude = value;
    const auto width = std::bit_width(magnitude);
    // Test discarded bits, never cast a rounded 2^63/2^64 back to an integer.
    if (!mode && width > 53 && (magnitude & ((uint64_t(1) << (width - 53)) - 1)))
        return GLOIN_STD_INEXACT;
    *output = static_cast<double>(value);
    return GLOIN_STD_OK;
}

template <typename T> int32_t f64_to_integer(double value, int32_t mode, T *output) {
    *output = 0;
    if (!std::isfinite(value) || (mode != 0 && mode != 1))
        return GLOIN_STD_INVALID;
    const double candidate = std::trunc(value);
    constexpr double lower = std::is_signed_v<T> ? -0x1p63 : 0.0;
    constexpr double upper = std::is_signed_v<T> ? 0x1p63 : 0x1p64;
    if (candidate < lower || candidate >= upper)
        return GLOIN_STD_OVERFLOW;
    if (!mode && candidate != value)
        return GLOIN_STD_INEXACT;
    *output = static_cast<T>(candidate);
    return GLOIN_STD_OK;
}
} // namespace

extern "C" int32_t gloin_std_parse_i64(const char *bytes, uint64_t length, int64_t *value) {
    return parse_integer(bytes, length, value);
}
extern "C" int32_t gloin_std_parse_u64(const char *bytes, uint64_t length, uint64_t *value) {
    return parse_integer(bytes, length, value);
}
extern "C" int32_t gloin_std_parse_f32(const char *bytes, uint64_t length, float *value) {
    return parse_float(bytes, length, value);
}
extern "C" int32_t gloin_std_parse_f64(const char *bytes, uint64_t length, double *value) {
    return parse_float(bytes, length, value);
}
extern "C" int32_t gloin_std_parse_bool(const char *bytes, uint64_t length, uint8_t *value) {
    *value = 0;
    if (length == 4 && std::memcmp(bytes, "true", 4) == 0) {
        *value = 1;
        return GLOIN_STD_OK;
    }
    return length == 5 && std::memcmp(bytes, "false", 5) == 0 ? GLOIN_STD_OK : GLOIN_STD_INVALID;
}
extern "C" int32_t gloin_std_format_i64(int64_t value, char *bytes, uint64_t *length) {
    return format(value, bytes, length, 21);
}
extern "C" int32_t gloin_std_format_u64(uint64_t value, char *bytes, uint64_t *length) {
    return format(value, bytes, length, 21);
}
extern "C" int32_t gloin_std_format_f32(float value, char *bytes, uint64_t *length) {
    return format(value, bytes, length, 32);
}
extern "C" int32_t gloin_std_format_f64(double value, char *bytes, uint64_t *length) {
    return format(value, bytes, length, 32);
}
extern "C" int32_t gloin_std_format_f32_fixed(float value, uint32_t precision, char *bytes,
                                              uint64_t *length) {
    return format_fixed(value, precision, bytes, length, 64);
}
extern "C" int32_t gloin_std_format_f64_fixed(double value, uint32_t precision, char *bytes,
                                              uint64_t *length) {
    return format_fixed(value, precision, bytes, length, 352);
}
extern "C" int32_t gloin_std_i64_from_i32(int32_t value, int64_t *output) {
    *output = value;
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_std_i32_from_i64(int64_t value, int32_t *output) {
    *output = 0;
    if (value < INT32_MIN || value > INT32_MAX)
        return GLOIN_STD_OVERFLOW;
    *output = static_cast<int32_t>(value);
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_std_u64_from_i64(int64_t value, uint64_t *output) {
    *output = 0;
    if (value < 0)
        return GLOIN_STD_OVERFLOW;
    *output = static_cast<uint64_t>(value);
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_std_i64_from_u64(uint64_t value, int64_t *output) {
    *output = 0;
    if (value > uint64_t(INT64_MAX))
        return GLOIN_STD_OVERFLOW;
    *output = static_cast<int64_t>(value);
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_std_f64_from_i64(int64_t value, int32_t mode, double *output) {
    return integer_to_f64(value, mode, output);
}
extern "C" int32_t gloin_std_f64_from_u64(uint64_t value, int32_t mode, double *output) {
    return integer_to_f64(value, mode, output);
}
extern "C" int32_t gloin_std_i64_from_f64(double value, int32_t mode, int64_t *output) {
    return f64_to_integer(value, mode, output);
}
extern "C" int32_t gloin_std_u64_from_f64(double value, int32_t mode, uint64_t *output) {
    return f64_to_integer(value, mode, output);
}
extern "C" int32_t gloin_std_f64_from_f32(float value, double *output) {
    *output = 0;
    if (!std::isfinite(value))
        return GLOIN_STD_INVALID;
    *output = value;
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_std_f32_from_f64(double value, int32_t mode, float *output) {
    *output = 0;
    if (!std::isfinite(value) || (mode != 0 && mode != 1))
        return GLOIN_STD_INVALID;
    if (std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
        return GLOIN_STD_OVERFLOW;
    const auto converted = static_cast<float>(value);
    if (converted == 0 && value != 0)
        return GLOIN_STD_UNDERFLOW;
    if (!mode && static_cast<double>(converted) != value)
        return GLOIN_STD_INEXACT;
    *output = converted;
    return GLOIN_STD_OK;
}
