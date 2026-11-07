#include "TestConstants.h"
#include "common/SpscQueue.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <thread>

namespace kparb {
namespace {

using namespace test_constants::queue_test;

TEST(SpscQueueTest, StartsEmpty)
{
    SpscQueue<int, smallCapacity> queue;
    int value = 0;
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.tryPop(value));
}

TEST(SpscQueueTest, PreservesOrder)
{
    SpscQueue<int, smallCapacity> queue;
    for (int value = 0; value < orderedElementCount; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    for (int expected = 0; expected < orderedElementCount; ++expected) {
        int value = emptySentinel;
        ASSERT_TRUE(queue.tryPop(value));
        EXPECT_EQ(value, expected);
    }
}

TEST(SpscQueueTest, RejectsPushWhenFull)
{
    SpscQueue<int, tinyCapacity> queue;
    for (int value = 0; value < static_cast<int>(tinyCapacity); ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    EXPECT_FALSE(queue.tryPush(rejectedValue));
    int value = 0;
    ASSERT_TRUE(queue.tryPop(value));
    EXPECT_TRUE(queue.tryPush(rejectedValue));
}

TEST(SpscQueueTest, WrapsAroundManyTimes)
{
    SpscQueue<std::uint64_t, tinyCapacity> queue;
    for (std::uint64_t value = 0; value < wraparoundIterations; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
        std::uint64_t popped = 0;
        ASSERT_TRUE(queue.tryPop(popped));
        EXPECT_EQ(popped, value);
    }
}

TEST(SpscQueueTest, BatchPopReturnsAvailableElements)
{
    SpscQueue<int, batchCapacity> queue;
    for (int value = 0; value < batchFillCount; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    std::array<int, firstBatchSize> batch{};
    EXPECT_EQ(queue.tryPopBatch(batch.data(), batch.size()), firstBatchSize);
    EXPECT_EQ(batch.front(), 0);
    EXPECT_EQ(batch.back(), static_cast<int>(firstBatchSize) - 1);
    std::array<int, batchCapacity> rest{};
    const std::size_t remaining = static_cast<std::size_t>(batchFillCount) - firstBatchSize;
    EXPECT_EQ(queue.tryPopBatch(rest.data(), rest.size()), remaining);
    EXPECT_EQ(rest[remaining - 1], batchFillCount - 1);
    EXPECT_EQ(queue.tryPopBatch(rest.data(), rest.size()), 0U);
}

TEST(SpscQueueTest, TwoThreadStressKeepsOrderWithoutLoss)
{
    constexpr std::uint64_t itemCount = stressItemCount;
    auto queue = std::make_unique<SpscQueue<std::uint64_t, stressCapacity>>();

    std::thread producer([&queue] {
        for (std::uint64_t value = 0; value < itemCount;) {
            if (queue->tryPush(value)) {
                ++value;
            }
        }
    });

    std::uint64_t expected = 0;
    std::array<std::uint64_t, stressBatchSize> batch{};
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
