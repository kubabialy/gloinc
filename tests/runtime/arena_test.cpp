#include "arena_runtime.h"
#include "arena_runtime_internal.h"
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {
struct Backing {
    size_t calls = 0;
    size_t fail_at = std::numeric_limits<size_t>::max();
    std::unordered_set<void *> live;
    static void *allocate(void *context, size_t size) noexcept {
        auto &self = *static_cast<Backing *>(context);
        if (self.calls++ == self.fail_at)
            return nullptr;
        auto *pointer = std::malloc(size);
        if (pointer)
            self.live.insert(pointer);
        return pointer;
    }
    static void release(void *context, void *pointer) noexcept {
        auto &self = *static_cast<Backing *>(context);
        EXPECT_EQ(self.live.erase(pointer), 1u);
        std::free(pointer);
    }
    void *create() { return gloin::arena::create({this, allocate, release}); }
};
class ArenaRuntimeTest : public testing::Test {
  protected:
    Backing backing;
    void *arena = nullptr;
    void SetUp() override {
        arena = backing.create();
        ASSERT_NE(arena, nullptr);
    }
    void TearDown() override {
        gloin_arena_general_destroy(arena);
        EXPECT_TRUE(backing.live.empty());
    }
};
} // namespace

TEST_F(ArenaRuntimeTest, EmptyCreateResetAndDestroyAreLazy) {
    EXPECT_EQ(backing.calls, 1u);
    gloin_arena_general_reset(arena);
    EXPECT_EQ(backing.calls, 1u);
    gloin_arena_general_destroy(nullptr);
}

TEST_F(ArenaRuntimeTest, MixedSizesAlignmentsAndGrowthPreserveObjects) {
    struct Object {
        unsigned char *pointer;
        size_t size;
        unsigned char value;
    };
    std::vector<Object> objects;
    for (size_t i = 0; i < 3000; ++i) {
        const size_t alignment = size_t{1} << (i % 17);
        const size_t size = 1 + (i * 193) % 10007;
        auto *pointer =
            static_cast<unsigned char *>(gloin_arena_general_alloc(arena, size, alignment));
        ASSERT_NE(pointer, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(pointer) % alignment, 0u);
        const auto value = static_cast<unsigned char>(i);
        std::memset(pointer, value, size);
        objects.push_back({pointer, size, value});
    }
    for (const auto &object : objects)
        for (size_t i = 0; i < object.size; ++i)
            ASSERT_EQ(object.pointer[i], object.value);
    EXPECT_GT(backing.calls, 2u);
}

TEST_F(ArenaRuntimeTest, EmptyObjectsHaveDistinctAlignedAddresses) {
    std::unordered_set<void *> addresses;
    for (size_t i = 0; i < 1000; ++i) {
        void *pointer = gloin_arena_general_alloc(arena, 0, 256);
        ASSERT_NE(pointer, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(pointer) % 256, 0u);
        EXPECT_TRUE(addresses.insert(pointer).second);
    }
}

TEST_F(ArenaRuntimeTest, ResetReusesAllBlocksIncludingLargeAllocationsWithoutMalloc) {
    for (int round = 0; round < 3; ++round) {
        const auto before = backing.calls;
        for (auto size : {1u, 65536u, 5000000u, 31u, 2097152u}) {
            auto *pointer = gloin_arena_general_alloc(arena, size, 4096);
            ASSERT_NE(pointer, nullptr);
            std::memset(pointer, round, size);
        }
        if (round)
            EXPECT_EQ(backing.calls, before);
        const auto retained = backing.live.size();
        const auto calls = backing.calls;
        gloin_arena_general_reset(arena);
        EXPECT_EQ(backing.live.size(), retained);
        EXPECT_EQ(backing.calls, calls);
    }
}

TEST_F(ArenaRuntimeTest, InvalidAlignmentAndOverflowDoNotChangeAllocationState) {
    auto *first = static_cast<unsigned char *>(gloin_arena_general_alloc(arena, 1, 1));
    ASSERT_NE(first, nullptr);
    *first = 42;
    const auto calls = backing.calls;
    for (uint64_t alignment : {uint64_t{0}, uint64_t{3}, uint64_t{1} << 63})
        EXPECT_EQ(gloin_arena_general_alloc(arena, 1, alignment), nullptr);
    for (uint64_t size : {UINT64_MAX, uint64_t{1} << 63, (uint64_t{1} << 63) - 1})
        EXPECT_EQ(gloin_arena_general_alloc(arena, size, 4096), nullptr);
    EXPECT_EQ(backing.calls, calls);
    EXPECT_EQ(gloin_arena_general_alloc(arena, 1, 1), first + 1);
    EXPECT_EQ(*first, 42);
}

TEST_F(ArenaRuntimeTest, FailedGrowthLeavesCursorAndLiveObjectsIntact) {
    auto *first = static_cast<unsigned char *>(gloin_arena_general_alloc(arena, 1, 1));
    ASSERT_NE(first, nullptr);
    *first = 99;
    auto live = backing.live.size();
    backing.fail_at = backing.calls;
    EXPECT_EQ(gloin_arena_general_alloc(arena, 2000000, 64), nullptr);
    EXPECT_EQ(backing.live.size(), live);
    EXPECT_EQ(*first, 99);
    EXPECT_EQ(gloin_arena_general_alloc(arena, 1, 1), first + 1);
    EXPECT_NE(gloin_arena_general_alloc(arena, 2000000, 64), nullptr);
    EXPECT_EQ(*first, 99);
}

TEST_F(ArenaRuntimeTest, FirstBlockFailureCanBeRetried) {
    backing.fail_at = backing.calls;
    EXPECT_EQ(gloin_arena_general_alloc(arena, 8, 8), nullptr);
    EXPECT_EQ(backing.live.size(), 1u);
    EXPECT_NE(gloin_arena_general_alloc(arena, 8, 8), nullptr);
}

TEST_F(ArenaRuntimeTest, IndependentArenasDoNotShareState) {
    auto *other = gloin_arena_general_create();
    ASSERT_NE(other, nullptr);
    auto *value = static_cast<uint64_t *>(
        gloin_arena_general_alloc(other, sizeof(uint64_t), alignof(uint64_t)));
    ASSERT_NE(value, nullptr);
    *value = 123456789;
    ASSERT_NE(gloin_arena_general_alloc(arena, 100000, 32), nullptr);
    gloin_arena_general_reset(arena);
    EXPECT_EQ(*value, 123456789u);
    gloin_arena_general_destroy(other);
}

TEST_F(ArenaRuntimeTest, ControlAllocationFailureReturnsNullWithoutLeaking) {
    Backing backing;
    backing.fail_at = 0;
    EXPECT_EQ(backing.create(), nullptr);
    EXPECT_TRUE(backing.live.empty());
}
