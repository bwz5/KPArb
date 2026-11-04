#include "AllocationCounter.h"
#include "ingestion/KalshiMessageParser.h"

#include <gtest/gtest.h>

#include <vector>

namespace kparb {
namespace {

class KalshiMessageParserTest : public ::testing::Test {
protected:
    KalshiParseResult parse(std::string_view message)
    {
        updates.clear();
        auto collect = [this](const LadderUpdate& update) { updates.push_back(update); };
        return parser.parse(message, 77, LadderUpdateSink(collect));
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
    EXPECT_EQ(result.subscriptionIdentifier, 2);
    EXPECT_EQ(result.sequenceNumber, 1);
    ASSERT_EQ(updates.size(), 6U);

    EXPECT_EQ(updates[0].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(0, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[1].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[1].ladderIndex, ladderIndexFor(0, LadderSlot::KalshiBuyNo));

    EXPECT_EQ(updates[2].ladderIndex, ladderIndexFor(0, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(updates[2].price, 920);
    EXPECT_EQ(updates[2].quantity, 300 * 100);
    EXPECT_EQ(updates[2].kind, UpdateKind::SetLevel);
    EXPECT_EQ(updates[3].price, 780);

    EXPECT_EQ(updates[4].ladderIndex, ladderIndexFor(0, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[4].price, 460);
    EXPECT_EQ(updates[4].quantity, 20 * 100);
    EXPECT_EQ(updates[5].price, 440);
    EXPECT_EQ(updates[5].receivedAtNanos, 77);
    EXPECT_EQ(updates[5].venue, Venue::Kalshi);
}

TEST_F(KalshiMessageParserTest, SnapshotPrefersDollarPrices)
{
    parse(R"({"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{"market_ticker":"FED-23DEC-T3.00","yes":[[8,300]],"yes_dollars":[["0.085",300]]}})");
    ASSERT_EQ(updates.size(), 3U);
    EXPECT_EQ(updates[2].price, 915);
}

TEST_F(KalshiMessageParserTest, SnapshotAcceptsFixedPointStrings)
{
    parse(R"({"type":"orderbook_snapshot","sid":3,"seq":1,"msg":{"market_ticker":"OTHER","yes_dollars_fp":[["0.0800","300.00"]],"no_dollars_fp":[["0.5450","12.50"]]}})");
    ASSERT_EQ(updates.size(), 4U);
    EXPECT_EQ(updates[2].ladderIndex, ladderIndexFor(1, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(updates[2].price, 920);
    EXPECT_EQ(updates[2].quantity, 30000);
    EXPECT_EQ(updates[3].ladderIndex, ladderIndexFor(1, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[3].price, 455);
    EXPECT_EQ(updates[3].quantity, 1250);
}

TEST_F(KalshiMessageParserTest, DeltaBecomesSignedLevelChange)
{
    const auto result = parse(R"({"type":"orderbook_delta","sid":2,"seq":3,"msg":{"market_ticker":"FED-23DEC-T3.00","price":96,"delta":-54,"side":"yes"}})");
    EXPECT_EQ(result.kind, KalshiMessageKind::Delta);
    EXPECT_EQ(result.sequenceNumber, 3);
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].kind, UpdateKind::AddToLevel);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(0, LadderSlot::KalshiBuyNo));
    EXPECT_EQ(updates[0].price, 40);
    EXPECT_EQ(updates[0].quantity, -5400);
}

TEST_F(KalshiMessageParserTest, NoSideDeltaTargetsBuyYesLadder)
{
    parse(R"({"type":"orderbook_delta","sid":2,"seq":4,"msg":{"market_ticker":"OTHER","price_dollars":"0.300","delta_fp":"12.50","side":"no"}})");
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(1, LadderSlot::KalshiBuyYes));
    EXPECT_EQ(updates[0].price, 700);
    EXPECT_EQ(updates[0].quantity, 1250);
}

TEST_F(KalshiMessageParserTest, ControlMessagesAndUnknownMarkets)
{
    EXPECT_EQ(parse(R"({"id":1,"type":"subscribed","msg":{"channel":"orderbook_delta","sid":5}})").subscriptionIdentifier, 5);
    EXPECT_EQ(parse(R"({"id":1,"type":"error","msg":{"code":6,"msg":"Already subscribed"}})").errorCode, 6);
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
    parser.parse(snapshot, 1, LadderUpdateSink(count));
    parser.parse(delta, 1, LadderUpdateSink(count));

    const testing_support::AllocationScope scope;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        parser.parse(snapshot, iteration, LadderUpdateSink(count));
        parser.parse(delta, iteration, LadderUpdateSink(count));
    }
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
    EXPECT_EQ(produced, 1001U * 7U);
}

}
}
