#include "common/SpscQueue.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <thread>

namespace kparb {
namespace {

TEST(SpscQueueTest, StartsEmpty)
{
    SpscQueue<int, 8> queue;
    int value = 0;
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.tryPop(value));
}

TEST(SpscQueueTest, PreservesOrder)
{
    SpscQueue<int, 8> queue;
    for (int value = 0; value < 5; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    for (int expected = 0; expected < 5; ++expected) {
        int value = -1;
        ASSERT_TRUE(queue.tryPop(value));
        EXPECT_EQ(value, expected);
    }
}

TEST(SpscQueueTest, RejectsPushWhenFull)
{
    SpscQueue<int, 4> queue;
    for (int value = 0; value < 4; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    EXPECT_FALSE(queue.tryPush(99));
    int value = 0;
    ASSERT_TRUE(queue.tryPop(value));
    EXPECT_TRUE(queue.tryPush(99));
}

TEST(SpscQueueTest, WrapsAroundManyTimes)
{
    SpscQueue<std::uint64_t, 4> queue;
    for (std::uint64_t value = 0; value < 1000; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
        std::uint64_t popped = 0;
        ASSERT_TRUE(queue.tryPop(popped));
        EXPECT_EQ(popped, value);
    }
}

TEST(SpscQueueTest, BatchPopReturnsAvailableElements)
{
    SpscQueue<int, 16> queue;
    for (int value = 0; value < 10; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    std::array<int, 4> batch{};
    EXPECT_EQ(queue.tryPopBatch(batch.data(), batch.size()), 4U);
    EXPECT_EQ(batch[0], 0);
    EXPECT_EQ(batch[3], 3);
    std::array<int, 16> rest{};
    EXPECT_EQ(queue.tryPopBatch(rest.data(), rest.size()), 6U);
    EXPECT_EQ(rest[5], 9);
    EXPECT_EQ(queue.tryPopBatch(rest.data(), rest.size()), 0U);
}

TEST(SpscQueueTest, TwoThreadStressKeepsOrderWithoutLoss)
{
    constexpr std::uint64_t itemCount = 10'000'000;
    auto queue = std::make_unique<SpscQueue<std::uint64_t, 1024>>();

    std::thread producer([&queue] {
        for (std::uint64_t value = 0; value < itemCount;) {
            if (queue->tryPush(value)) {
                ++value;
            }
        }
    });

    std::uint64_t expected = 0;
    std::array<std::uint64_t, 64> batch{};
    bool isOrdered = true;
    while (expected < itemCount) {
        const std::size_t count = queue->tryPopBatch(batch.data(), batch.size());
        for (std::size_t index = 0; index < count; ++index) {
            isOrdered = isOrdered && batch[index] == expected;
            ++expected;
        }
    }
    producer.join();
    EXPECT_TRUE(isOrdered);
    EXPECT_EQ(expected, itemCount);
    EXPECT_TRUE(queue->isEmpty());
}

}
}
