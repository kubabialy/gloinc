#include "arena_runtime.h"
#include "stdlib_runtime.h"
#include "stdlib_runtime_internal.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

namespace {
class StandardRuntimeTest : public testing::Test {
  protected:
    FILE *stream = nullptr;
    void TearDown() override {
        if (stream)
            std::fclose(stream);
    }
    void source(const std::string &bytes) {
        if (stream)
            std::fclose(stream);
        stream = std::tmpfile();
        ASSERT_NE(stream, nullptr);
        ASSERT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), stream), bytes.size());
        std::rewind(stream);
    }
    void line(uint64_t limit, int32_t status, const std::string &expected) {
        std::vector<char> storage(limit + 3, '#');
        uint64_t length = 999;
        EXPECT_EQ(gloin::standard::input(stream, storage.data() + 1, limit, &length), status);
        ASSERT_LE(length, limit);
        EXPECT_EQ(length, expected.size());
        EXPECT_EQ(std::string(storage.data() + 1, length), expected);
        EXPECT_EQ(storage[length + 1], '\0');
        EXPECT_EQ(storage.front(), '#');
        EXPECT_EQ(storage.back(), '#');
    }
    void parse(const std::string &text, int32_t status, int32_t expected = 0) {
        int32_t value = 999;
        EXPECT_EQ(gloin_std_parse_i32(text.data(), text.size(), &value), status) << text;
        EXPECT_EQ(value, expected) << text;
    }
};
} // namespace

TEST_F(StandardRuntimeTest, ValidDecimalSignsLeadingZerosAndBoundaries) {
    for (auto text : {"0", "-0", "+0", "00000"})
        parse(text, GLOIN_STD_OK, 0);
    parse("+00042", GLOIN_STD_OK, 42);
    parse("-00042", GLOIN_STD_OK, -42);
    parse("2147483647", GLOIN_STD_OK, INT32_MAX);
    parse("-2147483648", GLOIN_STD_OK, INT32_MIN);
}
TEST_F(StandardRuntimeTest, InvalidLexicalFormsHaveNoPartialResult) {
    for (auto text : {"", "+", "-", " 1", "1 ", "1\n", "1\r", "1\t", "0x10", "0b1", "1_000", "1.0",
                      "1e2", "--1", "+-1", "++1", "NaN", "１２", "é"})
        parse(text, GLOIN_STD_INVALID);
}
TEST_F(StandardRuntimeTest, OverflowAtBothBoundsAndVeryLongDigits) {
    for (auto text : {"2147483648", "+2147483648", "-2147483649", "4294967296"})
        parse(text, GLOIN_STD_OVERFLOW);
    parse(std::string(100000, '9'), GLOIN_STD_OVERFLOW);
    parse(std::string(100000, '0') + "1", GLOIN_STD_OK, 1);
}
TEST_F(StandardRuntimeTest, InvalidByteTakesPrecedenceOverOverflow) {
    parse(std::string(100, '9') + "x", GLOIN_STD_INVALID);
    parse("x" + std::string(100, '9'), GLOIN_STD_INVALID);
}
TEST_F(StandardRuntimeTest, CountedInputRejectsEmbeddedNulAndDoesNotReadPastLength) {
    parse(std::string("12\0", 3), GLOIN_STD_INVALID);
    parse(std::string("\0", 1), GLOIN_STD_INVALID);
    const char bytes[] = {'4', '2', 'x'};
    int32_t value = 0;
    EXPECT_EQ(gloin_std_parse_i32(bytes, 2, &value), GLOIN_STD_OK);
    EXPECT_EQ(value, 42);
    EXPECT_EQ(gloin_std_parse_i32(nullptr, 0, &value), GLOIN_STD_INVALID);
    EXPECT_EQ(value, 0);
}
TEST_F(StandardRuntimeTest, FormattingBoundariesTerminatesWithinTwelveBytes) {
    for (int32_t value : {INT32_MIN, -100, -1, 0, 1, 10, INT32_MAX}) {
        std::array<char, 14> bytes;
        bytes.fill('#');
        uint64_t length = 999;
        gloin_std_format_i32(value, bytes.data() + 1, &length);
        EXPECT_EQ(std::string(bytes.data() + 1, length), std::to_string(value));
        EXPECT_EQ(bytes[length + 1], '\0');
        EXPECT_EQ(bytes.front(), '#');
        EXPECT_EQ(bytes.back(), '#');
    }
}
TEST_F(StandardRuntimeTest, TenThousandDeterministicRoundTrips) {
    uint64_t state = 17;
    for (unsigned i = 0; i < 10000; ++i) {
        state = (state * 1664525 + 1013904223) % 4294967296ULL;
        const int32_t value = static_cast<int32_t>(static_cast<int64_t>(state) - 2147483648LL);
        char bytes[12];
        uint64_t length = 0;
        gloin_std_format_i32(value, bytes, &length);
        EXPECT_EQ(std::string(bytes, length), std::to_string(value));
        int32_t parsed = 0;
        EXPECT_EQ(gloin_std_parse_i32(bytes, length, &parsed), GLOIN_STD_OK);
        ASSERT_EQ(parsed, value);
    }
}
TEST_F(StandardRuntimeTest, EmptyEofDiffersFromEmptyLineAndFinalUnterminatedLine) {
    source("\n42");
    line(8, GLOIN_STD_OK, "");
    line(8, GLOIN_STD_OK, "42");
    line(8, GLOIN_STD_EOF, "");
    line(8, GLOIN_STD_EOF, "");
}
TEST_F(StandardRuntimeTest, CrLfIsStrippedButStandaloneCarriageReturnsAreData) {
    source("a\r\nb\rc\n\r\r\nlast\r");
    line(10, GLOIN_STD_OK, "a");
    line(10, GLOIN_STD_OK, "b\rc");
    line(10, GLOIN_STD_OK, "\r");
    line(10, GLOIN_STD_OK, "last\r");
    line(10, GLOIN_STD_EOF, "");
}
TEST_F(StandardRuntimeTest, InputPreservesNulUtf8AndNonUtf8Bytes) {
    const std::string bytes = std::string("A\0B", 3) + " café " + char(0xff);
    source(bytes + "\n");
    line(32, GLOIN_STD_OK, bytes);
}
TEST_F(StandardRuntimeTest, LimitsCountPayloadBytesExcludingCrLf) {
    source("abc\r\nabc\nabcd\nabc\r");
    line(3, GLOIN_STD_OK, "abc");
    line(3, GLOIN_STD_OK, "abc");
    line(3, GLOIN_STD_TOO_LONG, "");
    line(3, GLOIN_STD_TOO_LONG, "");
    line(3, GLOIN_STD_EOF, "");
}
TEST_F(StandardRuntimeTest, ZeroLimitAcceptsOnlyEmptyLinesAndDrainsOthers) {
    source("\n\r\nx\n\r");
    line(0, GLOIN_STD_OK, "");
    line(0, GLOIN_STD_OK, "");
    line(0, GLOIN_STD_TOO_LONG, "");
    line(0, GLOIN_STD_TOO_LONG, "");
    line(0, GLOIN_STD_EOF, "");
}
TEST_F(StandardRuntimeTest, OversizedMegabyteLineIsDrainedWithBoundedStorage) {
    source(std::string(1024 * 1024, 'x') + "\r\nnext\n");
    line(4, GLOIN_STD_TOO_LONG, "");
    line(4, GLOIN_STD_OK, "next");
    line(4, GLOIN_STD_EOF, "");
}
TEST_F(StandardRuntimeTest, IoFailureClearsTheOutput) {
    stream = std::fopen("/dev/null", "w");
    ASSERT_NE(stream, nullptr);
    line(8, GLOIN_STD_IO_ERROR, "");
}
TEST_F(StandardRuntimeTest, ZeroInitializedArenaBytesIncludeReusedStorage) {
    void *arena = gloin_arena_general_create();
    ASSERT_NE(arena, nullptr);
    for (unsigned round = 0; round < 3; ++round) {
        auto *bytes = static_cast<unsigned char *>(gloin_arena_general_alloc(arena, 100000, 1));
        ASSERT_NE(bytes, nullptr);
        gloin_arena_zero_bytes(bytes, 100000);
        for (unsigned i = 0; i < 100000; ++i)
            ASSERT_EQ(bytes[i], 0);
        std::memset(bytes, 0xab, 100000);
        gloin_arena_general_reset(arena);
    }
    gloin_arena_general_destroy(arena);
}

TEST_F(StandardRuntimeTest, StringCopyPreservesAllBytesWithoutTerminatorOrOverrun) {
    std::array<char, 256> source;
    for (unsigned i = 0; i < source.size(); ++i)
        source[i] = static_cast<char>(i);
    std::array<char, 258> destination;
    destination.fill('#');
    gloin_strings_copy(source.data(), source.size(), destination.data() + 1);
    EXPECT_EQ(std::memcmp(source.data(), destination.data() + 1, source.size()), 0);
    EXPECT_EQ(destination.front(), '#');
    EXPECT_EQ(destination.back(), '#');
    // An exactly sized range also catches accidental terminator writes under ASan.
    std::vector<char> exact(source.size());
    gloin_strings_copy(source.data(), source.size(), exact.data());
    EXPECT_EQ(std::memcmp(source.data(), exact.data(), source.size()), 0);
}

TEST_F(StandardRuntimeTest, EmptyStringCopyNeverDereferencesEitherPointer) {
    char sentinel = '#';
    gloin_strings_copy(nullptr, 0, nullptr);
    gloin_strings_copy(nullptr, 0, &sentinel);
    gloin_strings_copy(&sentinel, 0, nullptr);
    EXPECT_EQ(sentinel, '#');
}
