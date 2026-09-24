#include "stdlib_runtime.h"
#include <array>
#include <bit>
#include <clocale>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace {
template <class T, class F>
void parse(F function, const std::string &text, int32_t status, T expected = 0) {
    T value = 99;
    EXPECT_EQ(function(text.data(), text.size(), &value), status) << text;
    EXPECT_EQ(value, expected) << text;
    if constexpr (std::is_floating_point_v<T>)
        EXPECT_EQ(std::signbit(value), std::signbit(expected)) << text;
}
template <class T, size_t N, class F> std::string formatted(F function, T value) {
    std::array<char, N + 2> bytes;
    bytes.fill('#');
    uint64_t length = 999;
    EXPECT_EQ(function(value, bytes.data() + 1, &length), GLOIN_STD_OK);
    EXPECT_LT(length, N);
    if (length >= N)
        return {};
    EXPECT_EQ(bytes[length + 1], '\0');
    EXPECT_EQ(bytes.front(), '#');
    EXPECT_EQ(bytes.back(), '#');
    return {bytes.data() + 1, static_cast<size_t>(length)};
}
template <class T, class F, class I>
void conversion(F function, I input, int32_t status, T expected = 0) {
    T value = 99;
    EXPECT_EQ(function(input, &value), status);
    EXPECT_EQ(value, expected);
    if constexpr (std::is_floating_point_v<T>)
        EXPECT_EQ(std::signbit(value), std::signbit(expected));
}
} // namespace

TEST(NumericRuntimeTest, IntegerLimitsAndLeadingZeros) {
    parse<int64_t>(gloin_std_parse_i64, "-9223372036854775808", GLOIN_STD_OK, INT64_MIN);
    parse<int64_t>(gloin_std_parse_i64, "9223372036854775807", GLOIN_STD_OK, INT64_MAX);
    parse<uint64_t>(gloin_std_parse_u64, "+18446744073709551615", GLOIN_STD_OK, UINT64_MAX);
    parse<int64_t>(gloin_std_parse_i64, "+00042", GLOIN_STD_OK, 42);
    parse<uint64_t>(gloin_std_parse_u64, std::string(100000, '0') + "42", GLOIN_STD_OK, 42);
    for (auto s : {"9223372036854775808", "-9223372036854775809"})
        parse<int64_t>(gloin_std_parse_i64, s, GLOIN_STD_OVERFLOW);
    parse<uint64_t>(gloin_std_parse_u64, "18446744073709551616", GLOIN_STD_OVERFLOW);
    parse<int64_t>(gloin_std_parse_i64, std::string(100000, '9'), GLOIN_STD_OVERFLOW);
}
TEST(NumericRuntimeTest, IntegerGrammarAndInvalidPrecedence) {
    for (const std::string s : {"", "-", "+", " 1", "1 ", "1.0", "0x1", "1_000", "1e2", "１２",
                                "999999999999999999999999x"}) {
        parse<int64_t>(gloin_std_parse_i64, s, GLOIN_STD_INVALID);
        parse<uint64_t>(gloin_std_parse_u64, s, GLOIN_STD_INVALID);
    }
    for (auto s : {"-0", "-1", "-999999999999999999999999"})
        parse<uint64_t>(gloin_std_parse_u64, s, GLOIN_STD_INVALID);
    parse<int64_t>(gloin_std_parse_i64, std::string("1\0", 2), GLOIN_STD_INVALID);
    parse<uint64_t>(gloin_std_parse_u64, std::string("1\0", 2), GLOIN_STD_INVALID);
    int64_t v = 99;
    EXPECT_EQ(gloin_std_parse_i64(nullptr, 0, &v), GLOIN_STD_INVALID);
    EXPECT_EQ(v, 0);
    EXPECT_EQ(gloin_std_parse_i64("42x", 2, &v), GLOIN_STD_OK);
    EXPECT_EQ(v, 42);
}
TEST(NumericRuntimeTest, FloatGrammarUsesCompleteCountedInput) {
    for (auto s : {"42", "+42.", "4.2e1", ".42E+2", "420e-1"}) {
        parse<float>(gloin_std_parse_f32, s, GLOIN_STD_OK, 42);
        parse<double>(gloin_std_parse_f64, s, GLOIN_STD_OK, 42);
    }
    for (const std::string s :
         {"", "+", "-", ".", "1e", "1e+", ".e1", " 1", "1 ", "1,25", "nan", "NaN", "inf",
          "-Infinity", "0x1p0", "1_000", "1e999x", "1e-999x", "１２"}) {
        parse<float>(gloin_std_parse_f32, s, GLOIN_STD_INVALID);
        parse<double>(gloin_std_parse_f64, s, GLOIN_STD_INVALID);
    }
    parse<double>(gloin_std_parse_f64, std::string("1\0", 2), GLOIN_STD_INVALID);
    double v = 99;
    EXPECT_EQ(gloin_std_parse_f64(nullptr, 0, &v), GLOIN_STD_INVALID);
    EXPECT_EQ(v, 0);
    EXPECT_EQ(gloin_std_parse_f64("1.25x", 4, &v), GLOIN_STD_OK);
    EXPECT_EQ(v, 1.25);
}
TEST(NumericRuntimeTest, DirectWidthParsingRoundsMidpointsWithoutDoubleRounding) {
    parse<float>(gloin_std_parse_f32, "1.000000059604644775390625", GLOIN_STD_OK, 1.0f);
    parse<float>(gloin_std_parse_f32, "1.000000059604644775390626", GLOIN_STD_OK,
                 std::nextafter(1.0f, 2.0f));
    parse<double>(gloin_std_parse_f64, "1.00000000000000011102230246251565404236316680908203125",
                  GLOIN_STD_OK, 1.0);
    parse<double>(gloin_std_parse_f64, "1.00000000000000011102230246251565404236316680908203126",
                  GLOIN_STD_OK, std::nextafter(1.0, 2.0));
}
TEST(NumericRuntimeTest, FloatExtremesSubnormalsAndSignedZero) {
    parse<float>(gloin_std_parse_f32, "3.40282346638528859811704183484516925440e38", GLOIN_STD_OK,
                 std::numeric_limits<float>::max());
    parse<double>(gloin_std_parse_f64, "1.7976931348623157e308", GLOIN_STD_OK,
                  std::numeric_limits<double>::max());
    parse<float>(gloin_std_parse_f32, "1e-45", GLOIN_STD_OK,
                 std::numeric_limits<float>::denorm_min());
    parse<double>(gloin_std_parse_f64, "5e-324", GLOIN_STD_OK,
                  std::numeric_limits<double>::denorm_min());
    for (auto s : {"1e9999", "-1e9999"}) {
        parse<float>(gloin_std_parse_f32, s, GLOIN_STD_OVERFLOW);
        parse<double>(gloin_std_parse_f64, s, GLOIN_STD_OVERFLOW);
    }
    for (auto s : {"1e-9999", "-1e-9999"}) {
        parse<float>(gloin_std_parse_f32, s, GLOIN_STD_UNDERFLOW);
        parse<double>(gloin_std_parse_f64, s, GLOIN_STD_UNDERFLOW);
    }
    for (auto s : {"-0", "-0.000e9999999999999999999", "-0e-99999"}) {
        parse<float>(gloin_std_parse_f32, s, GLOIN_STD_OK, -0.0f);
        parse<double>(gloin_std_parse_f64, s, GLOIN_STD_OK, -0.0);
    }
    parse<double>(gloin_std_parse_f64, "0." + std::string(100000, '0') + "1e100001", GLOIN_STD_OK,
                  1.0);
}
TEST(NumericRuntimeTest, BooleanSpellingIsExact) {
    parse<uint8_t>(gloin_std_parse_bool, "true", GLOIN_STD_OK, 1);
    parse<uint8_t>(gloin_std_parse_bool, "false", GLOIN_STD_OK, 0);
    for (auto s : {"", "True", "TRUE", "0", "1", "false ", " true"})
        parse<uint8_t>(gloin_std_parse_bool, s, GLOIN_STD_INVALID);
    parse<uint8_t>(gloin_std_parse_bool, std::string("true\0", 5), GLOIN_STD_INVALID);
}
TEST(NumericRuntimeTest, IntegerFormattingBoundariesAndBufferGuards) {
    for (int64_t n : {INT64_MIN, int64_t(-1), int64_t(0), int64_t(1), INT64_MAX})
        EXPECT_EQ((formatted<int64_t, 21>(gloin_std_format_i64, n)), std::to_string(n));
    for (uint64_t n : {uint64_t(0), uint64_t(1), UINT64_MAX})
        EXPECT_EQ((formatted<uint64_t, 21>(gloin_std_format_u64, n)), std::to_string(n));
}
TEST(NumericRuntimeTest, TwentyThousandFiniteFloatBitPatternsRoundTrip) {
    uint64_t state = 0x123456789abcdef;
    for (unsigned i = 0; i < 10000; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        double d = std::bit_cast<double>(state);
        if (std::isfinite(d))
            parse<double>(gloin_std_parse_f64, formatted<double, 32>(gloin_std_format_f64, d),
                          GLOIN_STD_OK, d);
        float f = std::bit_cast<float>(static_cast<uint32_t>(state));
        if (std::isfinite(f))
            parse<float>(gloin_std_parse_f32, formatted<float, 32>(gloin_std_format_f32, f),
                         GLOIN_STD_OK, f);
    }
    EXPECT_EQ((formatted<double, 32>(gloin_std_format_f64, -0.0)), "-0");
    EXPECT_EQ((formatted<float, 32>(gloin_std_format_f32, -0.0f)), "-0");
    EXPECT_EQ((formatted<double, 32>(gloin_std_format_f64, 1e100)), "1e+100");
}
TEST(NumericRuntimeTest, FixedFormattingTiesPrecisionAndMaximumBuffers) {
    auto fixed = [](double n, uint32_t p) {
        return formatted<double, 352>(
            [p](double v, char *b, uint64_t *l) { return gloin_std_format_f64_fixed(v, p, b, l); },
            n);
    };
    EXPECT_EQ(fixed(2.5, 0), "2");
    EXPECT_EQ(fixed(3.5, 0), "4");
    EXPECT_EQ(fixed(-0.0, 3), "-0.000");
    EXPECT_EQ(fixed(-0.125, 2), "-0.12");
    EXPECT_EQ(fixed(1.25, 18), "1.250000000000000000");
    const auto largest = fixed(-std::numeric_limits<double>::max(), 18);
    EXPECT_EQ(largest.size(), 329u);
    EXPECT_TRUE(largest.ends_with(".000000000000000000"));
    auto single = formatted<float, 64>(
        [](float v, char *b, uint64_t *l) { return gloin_std_format_f32_fixed(v, 18, b, l); },
        -std::numeric_limits<float>::max());
    EXPECT_EQ(single.size(), 59u);
    EXPECT_TRUE(single.ends_with(".000000000000000000"));
}
TEST(NumericRuntimeTest, NonfiniteFormatAndInvalidPrecisionClearOutputs) {
    char bytes[352];
    uint64_t length;
    for (double n : {INFINITY, -INFINITY, NAN}) {
        length = 99;
        bytes[0] = '#';
        EXPECT_EQ(gloin_std_format_f64(n, bytes, &length), GLOIN_STD_INVALID);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(bytes[0], '\0');
        EXPECT_EQ(gloin_std_format_f32(static_cast<float>(n), bytes, &length), GLOIN_STD_INVALID);
        EXPECT_EQ(gloin_std_format_f64_fixed(n, 18, bytes, &length), GLOIN_STD_INVALID);
    }
    for (uint32_t p : {19u, UINT32_MAX}) {
        length = 99;
        bytes[0] = '#';
        EXPECT_EQ(gloin_std_format_f64_fixed(1, p, bytes, &length), GLOIN_STD_INVALID);
        EXPECT_EQ(length, 0u);
        EXPECT_EQ(bytes[0], '\0');
        EXPECT_EQ(gloin_std_format_f32_fixed(1, p, bytes, &length), GLOIN_STD_INVALID);
    }
}
TEST(NumericRuntimeTest, IntegerConversionsCheckSignednessAndWidth) {
    conversion<int64_t>(gloin_std_i64_from_i32, INT32_MIN, GLOIN_STD_OK, INT32_MIN);
    conversion<int32_t>(gloin_std_i32_from_i64, int64_t(INT32_MAX), GLOIN_STD_OK, INT32_MAX);
    conversion<int32_t>(gloin_std_i32_from_i64, int64_t(INT32_MIN), GLOIN_STD_OK, INT32_MIN);
    conversion<int32_t>(gloin_std_i32_from_i64, int64_t(INT32_MAX) + 1, GLOIN_STD_OVERFLOW);
    conversion<int32_t>(gloin_std_i32_from_i64, int64_t(INT32_MIN) - 1, GLOIN_STD_OVERFLOW);
    conversion<uint64_t>(gloin_std_u64_from_i64, INT64_MIN, GLOIN_STD_OVERFLOW);
    conversion<uint64_t>(gloin_std_u64_from_i64, INT64_MAX, GLOIN_STD_OK, INT64_MAX);
    conversion<int64_t>(gloin_std_i64_from_u64, uint64_t(INT64_MAX), GLOIN_STD_OK, INT64_MAX);
    conversion<int64_t>(gloin_std_i64_from_u64, UINT64_MAX, GLOIN_STD_OVERFLOW);
}
TEST(NumericRuntimeTest, IntegerToFloatExactnessAndNearestTies) {
    auto exact = [](int64_t v, double *o) { return gloin_std_f64_from_i64(v, 0, o); };
    auto rounded = [](int64_t v, double *o) { return gloin_std_f64_from_i64(v, 1, o); };
    conversion<double>(exact, INT64_MIN, GLOIN_STD_OK, -0x1p63);
    conversion<double>(exact, INT64_MAX, GLOIN_STD_INEXACT);
    conversion<double>(exact, int64_t(9007199254740993), GLOIN_STD_INEXACT);
    conversion<double>(exact, int64_t(9007199254740994), GLOIN_STD_OK, 9007199254740994.0);
    conversion<double>(rounded, int64_t(9007199254740993), GLOIN_STD_OK, 9007199254740992.0);
    conversion<double>(rounded, int64_t(9007199254740995), GLOIN_STD_OK, 9007199254740996.0);
    conversion<double>(rounded, INT64_MAX, GLOIN_STD_OK, 0x1p63);
    conversion<double>([](uint64_t v, double *o) { return gloin_std_f64_from_u64(v, 0, o); },
                       UINT64_MAX, GLOIN_STD_INEXACT);
    conversion<double>([](uint64_t v, double *o) { return gloin_std_f64_from_u64(v, 1, o); },
                       UINT64_MAX, GLOIN_STD_OK, 0x1p64);
}
TEST(NumericRuntimeTest, FloatToIntegerChecksBeforeCastingAndTruncatesExplicitly) {
    for (int mode : {0, 1}) {
        auto signed_convert = [mode](double v, int64_t *o) {
            return gloin_std_i64_from_f64(v, mode, o);
        };
        auto unsigned_convert = [mode](double v, uint64_t *o) {
            return gloin_std_u64_from_f64(v, mode, o);
        };
        conversion<int64_t>(signed_convert, -0x1p63, GLOIN_STD_OK, INT64_MIN);
        conversion<int64_t>(signed_convert, 0x1p63, GLOIN_STD_OVERFLOW);
        conversion<int64_t>(signed_convert, std::nextafter(-0x1p63, -INFINITY), GLOIN_STD_OVERFLOW);
        conversion<int64_t>(signed_convert, std::nextafter(0x1p63, 0.0), GLOIN_STD_OK,
                            INT64_MAX - 1023);
        conversion<uint64_t>(unsigned_convert, 0x1p64, GLOIN_STD_OVERFLOW);
        conversion<uint64_t>(unsigned_convert, std::nextafter(0x1p64, 0.0), GLOIN_STD_OK,
                             UINT64_MAX - 2047);
        conversion<uint64_t>(unsigned_convert, -1.5, GLOIN_STD_OVERFLOW);
        conversion<uint64_t>(unsigned_convert, -0.5, mode ? GLOIN_STD_OK : GLOIN_STD_INEXACT);
        conversion<int64_t>(signed_convert, -42.75, mode ? GLOIN_STD_OK : GLOIN_STD_INEXACT,
                            mode ? -42 : 0);
        conversion<uint64_t>(unsigned_convert, -0.0, GLOIN_STD_OK);
    }
}
TEST(NumericRuntimeTest, FloatWidthConversionsPreserveZeroAndReportRangeAndPrecision) {
    conversion<double>(gloin_std_f64_from_f32, -0.0f, GLOIN_STD_OK, -0.0);
    conversion<double>(gloin_std_f64_from_f32, std::numeric_limits<float>::denorm_min(),
                       GLOIN_STD_OK, 0x1p-149);
    for (int mode : {0, 1}) {
        auto narrow = [mode](double v, float *o) { return gloin_std_f32_from_f64(v, mode, o); };
        conversion<float>(narrow, -0.0, GLOIN_STD_OK, -0.0f);
        conversion<float>(narrow, 0x1p-149, GLOIN_STD_OK, std::numeric_limits<float>::denorm_min());
        conversion<float>(narrow, 0x1p-150, GLOIN_STD_UNDERFLOW);
        conversion<float>(narrow, -0x1p-150, GLOIN_STD_UNDERFLOW);
        conversion<float>(narrow, 0.1, mode ? GLOIN_STD_OK : GLOIN_STD_INEXACT, mode ? 0.1f : 0.0f);
        conversion<float>(narrow,
                          std::nextafter(double(std::numeric_limits<float>::max()), INFINITY),
                          GLOIN_STD_OVERFLOW);
        conversion<float>(narrow, 1.0 + 0x1p-24, mode ? GLOIN_STD_OK : GLOIN_STD_INEXACT,
                          mode ? 1.0f : 0.0f);
    }
}
TEST(NumericRuntimeTest, NativeInvalidModesAndNonfiniteConversionsClearPayload) {
    for (double v : {INFINITY, -INFINITY, NAN}) {
        conversion<int64_t>([](double v, int64_t *o) { return gloin_std_i64_from_f64(v, 1, o); }, v,
                            GLOIN_STD_INVALID);
        conversion<uint64_t>([](double v, uint64_t *o) { return gloin_std_u64_from_f64(v, 0, o); },
                             v, GLOIN_STD_INVALID);
        conversion<float>([](double v, float *o) { return gloin_std_f32_from_f64(v, 1, o); }, v,
                          GLOIN_STD_INVALID);
        conversion<double>(gloin_std_f64_from_f32, static_cast<float>(v), GLOIN_STD_INVALID);
    }
    conversion<double>([](int64_t v, double *o) { return gloin_std_f64_from_i64(v, 2, o); },
                       int64_t(1), GLOIN_STD_INVALID);
    conversion<double>([](uint64_t v, double *o) { return gloin_std_f64_from_u64(v, -1, o); },
                       uint64_t(1), GLOIN_STD_INVALID);
    conversion<int64_t>([](double v, int64_t *o) { return gloin_std_i64_from_f64(v, 2, o); }, 1.0,
                        GLOIN_STD_INVALID);
    conversion<uint64_t>([](double v, uint64_t *o) { return gloin_std_u64_from_f64(v, 2, o); }, 1.0,
                         GLOIN_STD_INVALID);
    conversion<float>([](double v, float *o) { return gloin_std_f32_from_f64(v, 2, o); }, 1.0,
                      GLOIN_STD_INVALID);
}
TEST(NumericRuntimeTest, NumericTextIsLocaleIndependent) {
    const std::string original = std::setlocale(LC_NUMERIC, nullptr);
    // C always exists; a comma-decimal locale is exercised when provided by the host.
    for (auto locale : {"C", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
        if (!std::setlocale(LC_NUMERIC, locale))
            continue;
        parse<double>(gloin_std_parse_f64, "1.25", GLOIN_STD_OK, 1.25);
        parse<double>(gloin_std_parse_f64, "1,25", GLOIN_STD_INVALID);
        EXPECT_EQ((formatted<double, 32>(gloin_std_format_f64, 1.25)), "1.25");
    }
    std::setlocale(LC_NUMERIC, original.c_str());
}
