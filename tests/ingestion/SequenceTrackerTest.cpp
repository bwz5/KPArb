#include "TestConstants.h"
#include "ingestion/KalshiMessageParser.h"
#include "ingestion/SequenceTracker.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace kparb {
namespace {

using namespace test_constants::sequence_test;

TEST(SequenceTrackerTest, AcceptsConsecutiveSequencesPerSubscription)
{
    SequenceTracker tracker;
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, firstSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(otherSubscription, skippedToSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, subscriptionUpdateSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, followingSequence));
}

TEST(SequenceTrackerTest, ReportsGapsAndStartsOverAfterReset)
{
    SequenceTracker tracker;
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, firstSequence));
    EXPECT_FALSE(tracker.acceptAndCheckContinuity(mergedSubscription, skippedToSequence));
    tracker.reset();
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, followingSequence));
}

TEST(SequenceTrackerTest, IgnoresMessagesWithoutTrackableSequence)
{
    SequenceTracker tracker;
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, firstSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, unsequenced));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(untrackedSubscription, skippedToSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, subscriptionUpdateSequence));
}

TEST(SequenceTrackerTest, SubscriptionUpdateAcknowledgementsConsumeASequenceNumber)
{
    const LadderDirectory directory(std::vector<PairSubscription>{{"KXTEST-26NOV05AAABBB-AAA", "yes", "no"}});
    KalshiMessageParser parser(directory);
    auto ignore = [](const LadderUpdate&) {};
    const std::string acknowledgement = R"({"type":"ok","id":2,"sid":)" + std::to_string(mergedSubscription) + R"(,"seq":)"
        + std::to_string(subscriptionUpdateSequence) + R"(,"msg":{"market_tickers":["KXTEST-26NOV05AAABBB-AAA"]}})";
    const KalshiParseResult result = parser.parse(acknowledgement, 0, LadderUpdateSink(ignore));
    EXPECT_EQ(result.kind, KalshiMessageKind::Ignored);
    EXPECT_EQ(result.subscriptionIdentifier, mergedSubscription);
    EXPECT_EQ(result.sequenceNumber, subscriptionUpdateSequence);

    SequenceTracker tracker;
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, firstSequence));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(result.subscriptionIdentifier, result.sequenceNumber));
    EXPECT_TRUE(tracker.acceptAndCheckContinuity(mergedSubscription, followingSequence));
}

}
}
