#include "TestConstants.h"
#include "ingestion/UpdatePublisher.h"

#include <gtest/gtest.h>

#include <memory>

namespace kparb {
namespace {

using namespace test_constants::publisher_test;

TEST(UpdatePublisherTest, OverflowSignalsAndInsertsResetBeforeNextUpdate)
{
    auto queue = std::make_unique<LadderUpdateQueue>();
    auto channel = std::make_unique<LogChannel>(channelSource);
    UpdatePublisher publisher(Venue::Polymarket, *queue, *channel);

    const LadderUpdate update{updateNanos, ladderIndex, quantity, price, UpdateKind::SetLevel, Venue::Polymarket};
    for (std::size_t index = 0; index < ladderUpdateQueueCapacity; ++index) {
        publisher.publish(update);
    }
    EXPECT_FALSE(publisher.takeOverflowSignal());
    publisher.publish(update);
    EXPECT_TRUE(publisher.takeOverflowSignal());
    EXPECT_FALSE(publisher.takeOverflowSignal());

    LadderUpdate popped{};
    ASSERT_TRUE(queue->tryPop(popped));
    publisher.publish(update);
    LadderUpdate last{};
    std::size_t remaining = 0;
    while (queue->tryPop(popped)) {
        last = popped;
        ++remaining;
    }
    EXPECT_EQ(remaining, ladderUpdateQueueCapacity);
    EXPECT_EQ(last.kind, UpdateKind::ResetVenue);
    EXPECT_EQ(last.venue, Venue::Polymarket);
    EXPECT_TRUE(publisher.takeOverflowSignal());

    publisher.publish(update);
    ASSERT_TRUE(queue->tryPop(popped));
    EXPECT_EQ(popped.kind, UpdateKind::ResetVenue);
    ASSERT_TRUE(queue->tryPop(popped));
    EXPECT_EQ(popped.kind, UpdateKind::SetLevel);
    EXPECT_FALSE(queue->tryPop(popped));

    publisher.publish(update);
    ASSERT_TRUE(queue->tryPop(popped));
    EXPECT_EQ(popped.kind, UpdateKind::SetLevel);
}

}
}
