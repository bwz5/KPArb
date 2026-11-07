#include "TestConstants.h"
#include "AllocationCounter.h"
#include "ingestion/KalshiMessageParser.h"

#include <gtest/gtest.h>

#include <vector>

namespace kparb {
namespace {

using namespace test_constants::parser_test;
using test_constants::engine_test::firstPair;
using test_constants::engine_test::secondPair;

constexpr std::size_t firstLevelUpdate = clearUpdatesPerKalshiSnapshot;

constexpr Ticks askFromBidCents(Ticks bidCents)
{
    return complementPrice(static_cast<Ticks>(bidCents * ticksPerCent));
}

class KalshiMessageParserTest : public ::testing::Test {
protected:
    KalshiParseResult parse(std::string_view message)
    {
        updates.clear();
        auto collect = [this](const LadderUpdate& update) { updates.push_back(update); };
        return parser.parse(message, kalshiReceivedAtNanos, LadderUpdateSink(collect));
    }

    LadderDirectory directory{std::vector<PairSubscription>{
        {"FED-23DEC-T3.00", "yes-token", "no-token"},
        {"OTHER", "a", "b"},
    }};
    KalshiMessageParser parser{directory};
    std::vector<LadderUpdate> updates;
};

TEST_F(KalshiMessageParserTest, SnapshotClearsBothLaddersAndInvertsBids)
{
    const auto result = parse(R"({"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{"market_ticker":"FED-23DEC-T3.00","yes":[[8,300],[22,333]],"no":[[54,20],[56,146]]}})");
    EXPECT_EQ(result.kind, KalshiMessageKind::Snapshot);
    EXPECT_EQ(result.subscriptionIdentifier, kalshiSnapshotSubscription);
    EXPECT_EQ(result.sequenceNumber, kalshiSnapshotSequence);
    ASSERT_EQ(updates.size(), kalshiSnapshotUpdateCount);

    EXPECT_EQ(updates[0].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(firstPair, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[1].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[1].ladderIndex, ladderIndexFor(firstPair, LadderSlot::KalshiBuyNo));

    const LadderUpdate& firstNoAsk = updates[firstLevelUpdate];
    EXPECT_EQ(firstNoAsk.ladderIndex, ladderIndexFor(firstPair, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(firstNoAsk.price, askFromBidCents(kalshiFirstYesBidCents));
    EXPECT_EQ(firstNoAsk.quantity, static_cast<std::int32_t>(kalshiFirstYesBidContracts * quantityUnitsPerContract));
    EXPECT_EQ(firstNoAsk.kind, UpdateKind::SetLevel);
    EXPECT_EQ(updates[firstLevelUpdate + 1].price, askFromBidCents(kalshiSecondYesBidCents));

    const LadderUpdate& firstYesAsk = updates[firstLevelUpdate + kalshiSnapshotYesBidLevels];
    EXPECT_EQ(firstYesAsk.ladderIndex, ladderIndexFor(firstPair, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(firstYesAsk.price, askFromBidCents(kalshiFirstNoBidCents));
    EXPECT_EQ(firstYesAsk.quantity, static_cast<std::int32_t>(kalshiFirstNoBidContracts * quantityUnitsPerContract));
    EXPECT_EQ(updates.back().price, askFromBidCents(kalshiSecondNoBidCents));
    EXPECT_EQ(updates.back().receivedAtNanos, kalshiReceivedAtNanos);
    EXPECT_EQ(updates.back().venue, Venue::Kalshi);
}

TEST_F(KalshiMessageParserTest, SnapshotPrefersDollarPrices)
{
    parse(R"({"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{"market_ticker":"FED-23DEC-T3.00","yes":[[8,300]],"yes_dollars":[["0.085",300]]}})");
    ASSERT_EQ(updates.size(), clearUpdatesPerKalshiSnapshot + 1);
    EXPECT_EQ(updates[firstLevelUpdate].price, complementPrice(kalshiDollarYesBidTicks));
}

TEST_F(KalshiMessageParserTest, SnapshotAcceptsFixedPointStrings)
{
    parse(R"({"type":"orderbook_snapshot","sid":3,"seq":1,"msg":{"market_ticker":"OTHER","yes_dollars_fp":[["0.0800","300.00"]],"no_dollars_fp":[["0.5450","12.50"]]}})");
    ASSERT_EQ(updates.size(), clearUpdatesPerKalshiSnapshot + kalshiFixedPointLevels);
    EXPECT_EQ(updates[firstLevelUpdate].ladderIndex, ladderIndexFor(secondPair, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(updates[firstLevelUpdate].price, askFromBidCents(kalshiFirstYesBidCents));
    EXPECT_EQ(updates[firstLevelUpdate].quantity, static_cast<std::int32_t>(kalshiFirstYesBidContracts * quantityUnitsPerContract));
    EXPECT_EQ(updates.back().ladderIndex, ladderIndexFor(secondPair, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates.back().price, complementPrice(kalshiFixedPointNoBidTicks));
    EXPECT_EQ(updates.back().quantity, kalshiFixedPointNoBidQuantity);
}

TEST_F(KalshiMessageParserTest, DeltaBecomesSignedLevelChange)
{
    const auto result = parse(R"({"type":"orderbook_delta","sid":2,"seq":3,"msg":{"market_ticker":"FED-23DEC-T3.00","price":96,"delta":-54,"side":"yes"}})");
    EXPECT_EQ(result.kind, KalshiMessageKind::Delta);
    EXPECT_EQ(result.sequenceNumber, kalshiDeltaSequence);
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].kind, UpdateKind::AddToLevel);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(firstPair, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(updates[0].price, askFromBidCents(kalshiDeltaYesBidCents));
    EXPECT_EQ(updates[0].quantity, kalshiDeltaContracts * static_cast<std::int32_t>(quantityUnitsPerContract));
}

TEST_F(KalshiMessageParserTest, NoSideDeltaTargetsBuyYesLadder)
{
    parse(R"({"type":"orderbook_delta","sid":2,"seq":4,"msg":{"market_ticker":"OTHER","price_dollars":"0.300","delta_fp":"12.50","side":"no"}})");
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(secondPair, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[0].price, complementPrice(kalshiNoSideDeltaBidTicks));
    EXPECT_EQ(updates[0].quantity, kalshiNoSideDeltaQuantity);
}

TEST_F(KalshiMessageParserTest, ControlMessagesAndUnknownMarkets)
{
    EXPECT_EQ(parse(R"({"id":1,"type":"subscribed","msg":{"channel":"orderbook_delta","sid":5}})").subscriptionIdentifier, kalshiSubscribedIdentifier);
    EXPECT_EQ(parse(R"({"id":1,"type":"error","msg":{"code":6,"msg":"Already subscribed"}})").errorCode, kalshiAlreadySubscribedCode);
    EXPECT_EQ(parse(R"({"type":"orderbook_delta","sid":2,"seq":5,"msg":{"market_ticker":"NOPE","price":5,"delta":1,"side":"yes"}})").kind,
        KalshiMessageKind::UnknownMarket);
    EXPECT_EQ(parse("not json").kind, KalshiMessageKind::Malformed);
    EXPECT_EQ(parse(R"({"type":"ticker","msg":{}})").kind, KalshiMessageKind::Ignored);
    EXPECT_TRUE(updates.empty());
}

TEST_F(KalshiMessageParserTest, RejectsOutOfRangePrices)
{
    EXPECT_EQ(parse(R"({"type":"orderbook_delta","sid":2,"seq":6,"msg":{"market_ticker":"OTHER","price":150,"delta":1,"side":"yes"}})").kind,
        KalshiMessageKind::Malformed);
}

TEST_F(KalshiMessageParserTest, ParsingDoesNotAllocateAfterWarmup)
{
    const std::string_view delta = R"({"type":"orderbook_delta","sid":2,"seq":3,"msg":{"market_ticker":"FED-23DEC-T3.00","price":96,"delta":-54,"side":"yes"}})";
    const std::string_view snapshot = R"({"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{"market_ticker":"FED-23DEC-T3.00","yes":[[8,300],[22,333]],"no":[[54,20],[56,146]]}})";
    std::size_t produced = 0;
    auto count = [&produced](const LadderUpdate&) { ++produced; };
    parser.parse(snapshot, warmupReceivedAtNanos, LadderUpdateSink(count));
    parser.parse(delta, warmupReceivedAtNanos, LadderUpdateSink(count));

    const testing_support::AllocationScope scope;
    for (int iteration = 0; iteration < allocationCheckIterations; ++iteration) {
        parser.parse(snapshot, iteration, LadderUpdateSink(count));
        parser.parse(delta, iteration, LadderUpdateSink(count));
    }
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
    EXPECT_EQ(produced, (allocationCheckIterations + 1) * kalshiUpdatesPerSnapshotAndDelta);
}

}
}
