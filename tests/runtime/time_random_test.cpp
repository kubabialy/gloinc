#include "random_runtime.h"
#include "stdlib_runtime.h"
#include "time_runtime_internal.h"
#include <array>
#include <cerrno>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
namespace {
struct Fake {
    uint64_t ticks = 42;
    int32_t code = 0;
    int32_t error = 0;
    unsigned calls = 0;
};
int32_t read_fake(void *data, uint64_t *ticks, int32_t *error) {
    auto &fake = *static_cast<Fake *>(data);
    ++fake.calls;
    *ticks = fake.ticks;
    *error = fake.error;
    errno = ERANGE;
    return fake.code;
}
void expect_read(int32_t code, uint64_t expected, int32_t error = 0) {
    uint64_t ticks = 999;
    int32_t actual = 999;
    errno = E2BIG;
    EXPECT_EQ(gloin_time_monotonic(&ticks, &actual), code);
    EXPECT_EQ(ticks, expected);
    EXPECT_EQ(actual, error);
    EXPECT_EQ(errno, E2BIG);
}
} // namespace
TEST(TimeRandomRuntimeTest, PublishedSplitMix64VectorsAtFourSeeds) {
    const uint64_t seeds[] = {0, 1, 42, UINT64_MAX};
    const uint64_t expected[][6] = {
        {16294208416658607535ULL, 7960286522194355700ULL, 487617019471545679ULL,
         17909611376780542444ULL, 1961750202426094747ULL, 6038094601263162090ULL},
        {10451216379200822465ULL, 13757245211066428519ULL, 17911839290282890590ULL,
         8196980753821780235ULL, 8195237237126968761ULL, 14072917602864530048ULL},
        {13679457532755275413ULL, 2949826092126892291ULL, 5139283748462763858ULL,
         6349198060258255764ULL, 701532786141963250ULL, 16015981125662989062ULL},
        {16490336266968443936ULL, 16834447057089888969ULL, 4048727598324417001ULL,
         7862637804313477842ULL, 13015481187462834606ULL, 15212506146343009075ULL}};
    for (unsigned i = 0; i < 4; ++i) {
        uint64_t state = seeds[i];
        for (auto word : expected[i])
            EXPECT_EQ(gloin_random_splitmix64(&state), word);
    }
}
TEST(TimeRandomRuntimeTest, CounterWrapAndBothOutputEndpointsAreDefined) {
    uint64_t state = 7046029254386353131ULL;
    EXPECT_EQ(gloin_random_splitmix64(&state), 0u);
    EXPECT_EQ(state, 0u);
    EXPECT_EQ(gloin_random_splitmix64(&state), 16294208416658607535ULL);
    state = 3558559446808474027ULL;
    EXPECT_EQ(gloin_random_splitmix64(&state), UINT64_MAX);
    state = UINT64_MAX;
    gloin_random_splitmix64(&state);
    EXPECT_EQ(state, UINT64_C(0x9e3779b97f4a7c14));
}
TEST(TimeRandomRuntimeTest, CopiesAndInterleavedGeneratorsHaveIndependentState) {
    uint64_t a = 42, b = 42, unrelated = 99;
    for (unsigned i = 0; i < 10000; ++i) {
        EXPECT_EQ(gloin_random_splitmix64(&a), gloin_random_splitmix64(&b));
        gloin_random_splitmix64(&unrelated);
    }
    auto copied = a;
    EXPECT_EQ(gloin_random_splitmix64(&a), gloin_random_splitmix64(&copied));
}
TEST(TimeRandomRuntimeTest, NativeSecondsConversionIsExactAtTheU64Boundary) {
    uint64_t result = 99;
    EXPECT_EQ(gloin::time::checked_nanoseconds(0, 0, &result), 0);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(gloin::time::checked_nanoseconds(12, 345, &result), 0);
    EXPECT_EQ(result, 12000000345ULL);
    EXPECT_EQ(gloin::time::checked_nanoseconds(18446744073LL, 709551615, &result), 0);
    EXPECT_EQ(result, UINT64_MAX);
    EXPECT_EQ(gloin::time::checked_nanoseconds(18446744073LL, 709551616, &result),
              GLOIN_STD_OVERFLOW);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(gloin::time::checked_nanoseconds(INT64_MAX, 0, &result), GLOIN_STD_OVERFLOW);
    EXPECT_EQ(result, 0u);
}
TEST(TimeRandomRuntimeTest, InvalidNativeTimespecPartsNeverWrap) {
    for (auto [seconds, nanos] : std::array<std::pair<int64_t, int64_t>, 4>{
             {{-1, 0}, {0, -1}, {0, 1000000000}, {-1, 1000000000}}}) {
        uint64_t result = 99;
        EXPECT_EQ(gloin::time::checked_nanoseconds(seconds, nanos, &result), GLOIN_STD_INVALID);
        EXPECT_EQ(result, 0u);
    }
}
TEST(TimeRandomRuntimeTest, OsClockIsNondecreasingAndPreservesErrno) {
    gloin::time::ClockScope scope(nullptr);
    ASSERT_TRUE(scope.valid());
    uint64_t first = 0, last = 0;
    int32_t error = 99;
    errno = E2BIG;
    ASSERT_EQ(gloin_time_monotonic(&first, &error), 0);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(errno, E2BIG);
    for (unsigned i = 0; i < 100; ++i) {
        ASSERT_EQ(gloin_time_monotonic(&last, &error), 0);
        EXPECT_GE(last, first);
        first = last;
    }
}
TEST(TimeRandomRuntimeTest, CallbackSuccessIncludesZeroAndMaximumTicks) {
    Fake fake;
    GloinClockSource source{read_fake, &fake};
    gloin::time::ClockScope scope(&source);
    ASSERT_TRUE(scope.valid());
    expect_read(0, 42);
    fake.ticks = 0;
    expect_read(0, 0);
    fake.ticks = UINT64_MAX;
    fake.error = EIO;
    expect_read(0, UINT64_MAX);
    EXPECT_EQ(fake.calls, 3u);
}
TEST(TimeRandomRuntimeTest, CallbackFailuresNormalizePayloadsAndErrorCodes) {
    Fake fake;
    GloinClockSource source{read_fake, &fake};
    gloin::time::ClockScope scope(&source);
    ASSERT_TRUE(scope.valid());
    fake.code = GLOIN_STD_IO_ERROR;
    fake.error = EACCES;
    expect_read(GLOIN_STD_IO_ERROR, 0, EACCES);
    fake.error = 0;
    expect_read(GLOIN_STD_IO_ERROR, 0, EIO);
    fake.error = -1;
    expect_read(GLOIN_STD_INVALID, 0);
    fake.code = GLOIN_STD_OVERFLOW;
    fake.error = EIO;
    expect_read(GLOIN_STD_OVERFLOW, 0);
    for (int code : std::array<int, 4>{GLOIN_STD_INVALID, GLOIN_STD_EOF, 999, -1}) {
        fake.code = code;
        expect_read(GLOIN_STD_INVALID, 0);
    }
}
TEST(TimeRandomRuntimeTest, BindingCopiesDescriptorButBorrowsUserdata) {
    Fake fake;
    GloinClockSource source{read_fake, &fake};
    auto token = gloin_time_clock_push(&source);
    ASSERT_NE(token, nullptr);
    source.read = nullptr;
    source.userdata = nullptr;
    expect_read(0, 42);
    fake.ticks = 71;
    expect_read(0, 71);
    EXPECT_EQ(gloin_time_clock_pop(token), 0);
}
TEST(TimeRandomRuntimeTest, NestedBindingsRestoreAndRejectOutOfOrderPops) {
    Fake a{11}, b{22};
    GloinClockSource sa{read_fake, &a}, sb{read_fake, &b};
    auto outer = gloin_time_clock_push(&sa);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(std::async(std::launch::async, [outer] { return gloin_time_clock_pop(outer); }).get(),
              GLOIN_STD_INVALID);
    auto inner = gloin_time_clock_push(&sb);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(gloin_time_clock_pop(outer), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_time_clock_pop(nullptr), GLOIN_STD_INVALID);
    expect_read(0, 22);
    EXPECT_EQ(gloin_time_clock_pop(inner), 0);
    expect_read(0, 11);
    EXPECT_EQ(gloin_time_clock_pop(outer), 0);
}
TEST(TimeRandomRuntimeTest, InvalidBindingsLeaveThePreviousProviderUntouched) {
    Fake fake;
    GloinClockSource source{read_fake, &fake};
    gloin::time::ClockScope outer(&source);
    ASSERT_TRUE(outer.valid());
    GloinClockSource invalid{nullptr, &fake};
    EXPECT_EQ(gloin_time_clock_push(&invalid), nullptr);
    {
        gloin::time::ClockScope rejected(&invalid);
        EXPECT_FALSE(rejected.valid());
    }
    expect_read(0, 42);
    {
        gloin::time::ClockScope os(nullptr);
        ASSERT_TRUE(os.valid());
        uint64_t ticks = 0;
        int32_t error = 0;
        EXPECT_EQ(gloin_time_monotonic(&ticks, &error), 0);
    }
    EXPECT_EQ(fake.calls, 1u);
    expect_read(0, 42);
}
TEST(TimeRandomRuntimeTest, CppScopeRestoresDuringHostExceptionUnwinding) {
    Fake a{11}, b{22};
    GloinClockSource sa{read_fake, &a}, sb{read_fake, &b};
    gloin::time::ClockScope outer(&sa);
    ASSERT_TRUE(outer.valid());
    try {
        gloin::time::ClockScope inner(&sb);
        ASSERT_TRUE(inner.valid());
        expect_read(0, 22);
        throw std::runtime_error("host test");
    } catch (const std::runtime_error &) {
    }
    expect_read(0, 11);
}
TEST(TimeRandomRuntimeTest, ClockProvidersAndRandomStatesAreThreadLocalOrCallerOwned) {
    auto worker = [](uint64_t value) {
        Fake fake{value};
        GloinClockSource source{read_fake, &fake};
        gloin::time::ClockScope scope(&source);
        if (!scope.valid())
            return false;
        uint64_t state = 42, copy = 42;
        for (unsigned i = 0; i < 1000; ++i) {
            uint64_t ticks = 0;
            int32_t error = 0;
            if (gloin_time_monotonic(&ticks, &error) != 0 || ticks != value || error != 0 ||
                gloin_random_splitmix64(&state) != gloin_random_splitmix64(&copy))
                return false;
        }
        return fake.calls == 1000;
    };
    auto a = std::async(std::launch::async, worker, 11),
         b = std::async(std::launch::async, worker, 22);
    EXPECT_TRUE(a.get());
    EXPECT_TRUE(b.get());
}
