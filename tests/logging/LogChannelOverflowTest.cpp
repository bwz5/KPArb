#include "logging/LogChannel.h"
#include "logging/RecordTypes.h"

#include <gtest/gtest.h>

#include <memory>

namespace kparb {
namespace {

TEST(LogChannelOverflowTest, DropsAndCountsWhenFull)
{
    auto channel = std::make_unique<LogChannel>(static_cast<std::uint16_t>(SourceIdentifier::Engine));
    FeedEventRecord event{};
    event.venue = Venue::Kalshi;
    event.eventCode = FeedEventCode::Connected;

    for (std::size_t index = 0; index < logChannelCapacity; ++index) {
        ASSERT_TRUE(channel->write(event));
    }
    EXPECT_FALSE(channel->write(event));
    EXPECT_FALSE(channel->write(event));
    EXPECT_EQ(channel->droppedRecordCount(), 2U);

    LogRecord record{};
    ASSERT_TRUE(channel->records().tryPop(record));
    EXPECT_EQ(record.header.type, RecordType::FeedEvent);
    EXPECT_EQ(record.header.sourceIdentifier, static_cast<std::uint16_t>(SourceIdentifier::Engine));
    EXPECT_EQ(record.header.sequenceNumber, 0U);
    EXPECT_TRUE(channel->write(event));
}

}
}
