#include "stdlib_runtime.h"
#include "worker_runtime.h"
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace {
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool released = false;
    std::thread::id worker;
};
struct Input {
    Gate *gate;
    int value;
};
void wait_for_parent(const void *bytes, void *output) {
    *static_cast<int *>(output) = -1;
    auto &input = *static_cast<const Input *>(bytes);
    auto &gate = *input.gate;
    std::unique_lock lock(gate.mutex);
    gate.worker = std::this_thread::get_id();
    gate.started = true;
    gate.changed.notify_one();
    if (!gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.released; }))
        return;
    *static_cast<int *>(output) = input.value;
}
} // namespace

TEST(WorkerRuntimeTest, WorkerStartsIndependentlyAndRetainsCopiedArgumentsUntilJoined) {
    gloin::workers::Invocation invocation(1);
    Gate gate;
    Input input{&gate, 42};
    gloin::workers::Launch first{};
    gloin_worker_launch(&first, wait_for_parent, &input, sizeof(input), sizeof(int));
    ASSERT_FALSE(first.failed);
    {
        std::unique_lock lock(gate.mutex);
        EXPECT_TRUE(
            gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.started; }));
        EXPECT_NE(gate.worker, std::this_thread::get_id());
        input.value = 7;
        gate.released = true;
        gate.changed.notify_one();
    }
    gloin::workers::Launch next{};
    gloin_worker_launch(&next, wait_for_parent, &input, sizeof(input), sizeof(int));
    EXPECT_TRUE(next.failed);
    EXPECT_EQ(next.code, GLOIN_STD_WOULD_BLOCK);
    int answer = 0;
    gloin_worker_join(first.handle, &answer, sizeof(answer));
    EXPECT_EQ(answer, 42);
    gloin_worker_launch(&next, wait_for_parent, &input, sizeof(input), sizeof(int));
    ASSERT_FALSE(next.failed);
    gloin_worker_join(next.handle, &answer, sizeof(answer));
    EXPECT_EQ(answer, 7);
}
