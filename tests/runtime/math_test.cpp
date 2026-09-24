#include "math_runtime.h"
#include "stdlib_runtime.h"
#include <array>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <type_traits>

#pragma STDC FENV_ACCESS ON
namespace {
template <class T> int32_t unary(int op, T x, T *out) {
    if constexpr (std::is_same_v<T, float>)
        return gloin_math_unary_f32(op, x, out);
    else
        return gloin_math_unary_f64(op, x, out);
}
template <class T> int32_t binary(int op, T x, T y, T *out) {
    if constexpr (std::is_same_v<T, float>)
        return gloin_math_binary_f32(op, x, y, out);
    else
        return gloin_math_binary_f64(op, x, y, out);
}
template <class T> void u(int op, T x, int status, T expected = T(0)) {
    T out = T(99);
    ASSERT_EQ(unary(op, x, &out), status) << op << ": " << x;
    EXPECT_EQ(out, expected);
    EXPECT_EQ(std::signbit(out), std::signbit(expected));
}
template <class T> void b(int op, T x, T y, int status, T expected = T(0)) {
    T out = T(99);
    ASSERT_EQ(binary(op, x, y, &out), status) << op << ": " << x << ", " << y;
    EXPECT_EQ(out, expected);
    EXPECT_EQ(std::signbit(out), std::signbit(expected));
}
template <class T> void near(T actual, T expected, T scale = T(16)) {
    EXPECT_LE(std::abs(actual - expected),
              scale * std::numeric_limits<T>::epsilon() * std::max(T(1), std::abs(expected)));
}
struct SavedEnvironment {
    fenv_t env;
    int error = errno;
    SavedEnvironment() { std::fegetenv(&env); }
    ~SavedEnvironment() {
        std::fesetenv(&env);
        errno = error;
    }
};
} // namespace
TEST(MathRuntimeTest, ExactUnaryValuesBothWidths) {
    auto run = []<class T>() {
        u<T>(GLOIN_MATH_ABS, T(-2), 0, T(2));
        u<T>(GLOIN_MATH_FLOOR, T(-1.25), 0, T(-2));
        u<T>(GLOIN_MATH_CEIL, T(-1.25), 0, T(-1));
        u<T>(GLOIN_MATH_TRUNC, T(-1.75), 0, T(-1));
        u<T>(GLOIN_MATH_ROUND, T(-2.5), 0, T(-3));
        u<T>(GLOIN_MATH_SQRT, T(4), 0, T(2));
        u<T>(GLOIN_MATH_EXP, T(0), 0, T(1));
        u<T>(GLOIN_MATH_LOG, T(1), 0, T(0));
        u<T>(GLOIN_MATH_LOG10, T(100), 0, T(2));
        u<T>(GLOIN_MATH_SIN, T(0), 0, T(0));
        u<T>(GLOIN_MATH_COS, T(0), 0, T(1));
        u<T>(GLOIN_MATH_TAN, T(0), 0, T(0));
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, BinaryPowersQuadrantsAndHypotenuse) {
    auto run = []<class T>() {
        b<T>(GLOIN_MATH_POW, T(-2), T(3), 0, T(-8));
        b<T>(GLOIN_MATH_POW, T(-2), T(4), 0, T(16));
        b<T>(GLOIN_MATH_POW, T(4), T(.5), 0, T(2));
        b<T>(GLOIN_MATH_POW, T(2), T(-3), 0, T(.125));
        b<T>(GLOIN_MATH_POW, T(0), T(0), 0, T(1));
        b<T>(GLOIN_MATH_HYPOT, T(-3), T(4), 0, T(5));
        T result = 0;
        for (T y : {T(-1), T(1)})
            for (T x : {T(-1), T(1)}) {
                ASSERT_EQ(binary(GLOIN_MATH_ATAN2, y, x, &result), 0);
                near(result, (y < T(0) ? T(-1) : T(1)) * (x < T(0) ? T(3) : T(1)) *
                                 std::numbers::pi_v<T> / T(4));
            }
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, DomainsAndNonfiniteInputsReturnPositiveZero) {
    auto run = []<class T>() {
        u<T>(GLOIN_MATH_SQRT, T(-1), GLOIN_STD_INVALID);
        for (int op : {GLOIN_MATH_LOG, GLOIN_MATH_LOG10})
            for (T v : {T(-1), T(0), T(-0.0)})
                u<T>(op, v, GLOIN_STD_INVALID);
        b<T>(GLOIN_MATH_POW, T(-2), T(.5), GLOIN_STD_INVALID);
        b<T>(GLOIN_MATH_POW, T(-0.0), T(-1), GLOIN_STD_INVALID);
        for (T v : {std::numeric_limits<T>::infinity(), -std::numeric_limits<T>::infinity(),
                    std::numeric_limits<T>::quiet_NaN()}) {
            for (int op = 0; op <= GLOIN_MATH_TAN; ++op)
                u<T>(op, v, GLOIN_STD_INVALID);
            for (int op = 0; op <= GLOIN_MATH_HYPOT; ++op) {
                b<T>(op, v, T(1), GLOIN_STD_INVALID);
                b<T>(op, T(1), v, GLOIN_STD_INVALID);
            }
        }
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, SignedZerosArePreservedOrCanonicalizedAsDocumented) {
    auto run = []<class T>() {
        for (int op : {GLOIN_MATH_FLOOR, GLOIN_MATH_CEIL, GLOIN_MATH_TRUNC, GLOIN_MATH_ROUND,
                       GLOIN_MATH_SQRT, GLOIN_MATH_SIN, GLOIN_MATH_TAN})
            u<T>(op, T(-0.0), 0, T(-0.0));
        u<T>(GLOIN_MATH_ABS, T(-0.0), 0, T(0));
        u<T>(GLOIN_MATH_TRUNC, T(-.25), 0, T(-0.0));
        u<T>(GLOIN_MATH_CEIL, T(-.25), 0, T(-0.0));
        u<T>(GLOIN_MATH_ROUND, T(-.25), 0, T(-0.0));
        b<T>(GLOIN_MATH_POW, T(-0.0), T(3), 0, T(-0.0));
        b<T>(GLOIN_MATH_POW, T(-0.0), T(2), 0, T(0));
        b<T>(GLOIN_MATH_HYPOT, T(-0.0), T(-0.0), 0, T(0));
        for (T y : {T(0), T(-0.0)}) {
            b<T>(GLOIN_MATH_ATAN2, y, T(0), 0, y);
            T r = 0;
            ASSERT_EQ(binary(GLOIN_MATH_ATAN2, y, T(-0.0), &r), 0);
            near(r, std::copysign(std::numbers::pi_v<T>, y));
        }
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, RoundingHalfwayNeighborsAndLargeIntegers) {
    auto run = []<class T>() {
        for (T x : {T(.5), T(1.5), T(2.5), T(3.5)}) {
            T integer = std::floor(x);
            u<T>(GLOIN_MATH_ROUND, x, 0, integer + T(1));
            u<T>(GLOIN_MATH_ROUND, -x, 0, -integer - T(1));
            u<T>(GLOIN_MATH_ROUND, std::nextafter(x, T(0)), 0, integer);
            u<T>(GLOIN_MATH_ROUND, std::nextafter(x, T(10)), 0, integer + T(1));
        }
        T large = std::ldexp(T(1), std::numeric_limits<T>::digits);
        for (int op : {GLOIN_MATH_FLOOR, GLOIN_MATH_CEIL, GLOIN_MATH_TRUNC, GLOIN_MATH_ROUND}) {
            u<T>(op, large, 0, large);
            u<T>(op, std::numeric_limits<T>::max(), 0, std::numeric_limits<T>::max());
        }
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, OverflowUnderflowAndNonzeroSubnormals) {
    auto run = []<class T>() {
        u<T>(GLOIN_MATH_EXP, T(10000), GLOIN_STD_OVERFLOW);
        u<T>(GLOIN_MATH_EXP, T(-10000), GLOIN_STD_UNDERFLOW);
        b<T>(GLOIN_MATH_POW, T(2), T(std::numeric_limits<T>::max_exponent), GLOIN_STD_OVERFLOW);
        const int exponent = std::numeric_limits<T>::min_exponent - std::numeric_limits<T>::digits;
        b<T>(GLOIN_MATH_POW, T(2), T(exponent), 0, std::numeric_limits<T>::denorm_min());
        b<T>(GLOIN_MATH_POW, T(2), T(exponent - 1), GLOIN_STD_UNDERFLOW);
        b<T>(GLOIN_MATH_ATAN2, std::numeric_limits<T>::denorm_min(), std::numeric_limits<T>::max(),
             GLOIN_STD_UNDERFLOW);
        b<T>(GLOIN_MATH_HYPOT, std::numeric_limits<T>::max(), std::numeric_limits<T>::max(),
             GLOIN_STD_OVERFLOW);
        u<T>(GLOIN_MATH_SIN, std::numeric_limits<T>::denorm_min(), 0,
             std::numeric_limits<T>::denorm_min());
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, HypotScalesWithoutSpuriousIntermediateRangeFailures) {
    auto run = []<class T>() {
        T big = std::numeric_limits<T>::max() / T(4), r = 0;
        ASSERT_EQ(binary(GLOIN_MATH_HYPOT, big, big, &r), 0);
        EXPECT_LE(std::abs(r / big - T(1.4142135623730950488L)),
                  T(4) * std::numeric_limits<T>::epsilon());
        b<T>(GLOIN_MATH_HYPOT, std::numeric_limits<T>::denorm_min(), T(0), 0,
             std::numeric_limits<T>::denorm_min());
        T small = std::numeric_limits<T>::min();
        ASSERT_EQ(binary(GLOIN_MATH_HYPOT, small, small, &r), 0);
        near(r / small, T(1.4142135623730950488L), T(4));
    };
    run.operator()<float>();
    run.operator()<double>();
}
TEST(MathRuntimeTest, IndependentTranscendentalReferenceValues) {
    struct Reference {
        int op;
        long double x;
        long double expected;
    };
    const Reference values[] = {{GLOIN_MATH_SQRT, 2, 1.414213562373095048801688724L},
                                {GLOIN_MATH_EXP, 1, 2.718281828459045235360287471L},
                                {GLOIN_MATH_LOG, 2, .693147180559945309417232121L},
                                {GLOIN_MATH_LOG10, 2, .301029995663981195213738895L},
                                {GLOIN_MATH_SIN, 1, .841470984807896506652502322L},
                                {GLOIN_MATH_COS, 1, .540302305868139717400936607L},
                                {GLOIN_MATH_TAN, 1, 1.557407724654902230506974807L}};
    for (auto ref : values) {
        float f = 0;
        double d = 0;
        ASSERT_EQ(unary(ref.op, float(ref.x), &f), 0);
        near(f, float(ref.expected), 4.0f);
        ASSERT_EQ(unary(ref.op, double(ref.x), &d), 0);
        near(d, double(ref.expected), 4.0);
    }
}
TEST(MathRuntimeTest, GeometricAndLogarithmicIdentitiesAcrossFiniteInputs) {
    for (int i = -100; i <= 100; ++i) {
        double x = double(i) / 16, s = 0, c = 0;
        ASSERT_EQ(unary(GLOIN_MATH_SIN, x, &s), 0);
        ASSERT_EQ(unary(GLOIN_MATH_COS, x, &c), 0);
        near(s * s + c * c, 1.0);
        double positive = double(i + 101) / 8, l = 0, e = 0;
        ASSERT_EQ(unary(GLOIN_MATH_LOG, positive, &l), 0);
        ASSERT_EQ(unary(GLOIN_MATH_EXP, l, &e), 0);
        near(e, positive);
    }
}
TEST(MathRuntimeTest, ErrnoRoundingModeAndExceptionFlagsAreRestored) {
    SavedEnvironment saved;
    for (int rounding : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(rounding), 0);
        std::feclearexcept(FE_ALL_EXCEPT);
        std::feraiseexcept(FE_DIVBYZERO);
        const int flags = std::fetestexcept(FE_ALL_EXCEPT);
        errno = E2BIG;
        double r = 99;
        EXPECT_EQ(gloin_math_unary_f64(GLOIN_MATH_SQRT, 2, &r), 0);
        EXPECT_EQ(std::fegetround(), rounding);
        EXPECT_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
        EXPECT_EQ(errno, E2BIG);
        EXPECT_EQ(r, 1.4142135623730951);
        EXPECT_EQ(gloin_math_unary_f64(GLOIN_MATH_EXP, 10000, &r), GLOIN_STD_OVERFLOW);
        EXPECT_EQ(std::fegetround(), rounding);
        EXPECT_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
        EXPECT_EQ(errno, E2BIG);
        EXPECT_EQ(r, 0);
    }
}
TEST(MathRuntimeTest, InvalidSelectorsNeverExposeAnUninitializedResult) {
    for (int op : {-1, 12, 99, INT32_MAX}) {
        u<float>(op, 1, GLOIN_STD_INVALID);
        u<double>(op, 1, GLOIN_STD_INVALID);
    }
    for (int op : {-1, 3, 99, INT32_MAX}) {
        b<float>(op, 1, 2, GLOIN_STD_INVALID);
        b<double>(op, 1, 2, GLOIN_STD_INVALID);
    }
}
TEST(MathRuntimeTest, SimultaneousHostEnvironmentsRemainIndependent) {
    auto worker = [](int rounding) {
        SavedEnvironment saved;
        std::fesetround(rounding);
        errno = E2BIG;
        for (int i = 0; i < 1000; ++i) {
            double value = 0;
            if (gloin_math_unary_f64(GLOIN_MATH_ROUND, 2.5, &value) != 0 || value != 3 ||
                std::fegetround() != rounding || errno != E2BIG)
                return false;
        }
        return true;
    };
    auto a = std::async(std::launch::async, worker, FE_UPWARD);
    auto b = std::async(std::launch::async, worker, FE_DOWNWARD);
    EXPECT_TRUE(a.get());
    EXPECT_TRUE(b.get());
}
