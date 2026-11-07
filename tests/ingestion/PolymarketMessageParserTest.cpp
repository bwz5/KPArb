#include "TestConstants.h"
#include "AllocationCounter.h"
#include "ingestion/PolymarketMessageParser.h"

#include <gtest/gtest.h>

#include <vector>

namespace kparb {
namespace {

using namespace test_constants::parser_test;
using test_constants::engine_test::firstPair;
using test_constants::engine_test::secondPair;

class PolymarketMessageParserTest : public ::testing::Test {
protected:
    PolymarketParseResult parse(std::string_view message)
    {
        updates.clear();
        auto collect = [this](const LadderUpdate& update) { updates.push_back(update); };
        return parser.parse(message, polymarketReceivedAtNanos, LadderUpdateSink(collect));
    }

    LadderDirectory directory{std::vector<PairSubscription>{
        {"KALSHI-A", "yes-token", "no-token"},
        {"KALSHI-B", "no-token", "yes-token"},
    }};
    PolymarketMessageParser parser{directory};
    std::vector<LadderUpdate> updates;
};

TEST_F(PolymarketMessageParserTest, BookSnapshotUsesAsksOnly)
{
    const auto result = parse(R"({"event_type":"book","asset_id":"yes-token","market":"0x1","bids":[{"price":"0.48","size":"30"}],"asks":[{"price":"0.52","size":"25"},{"price":"0.53","size":"60.5"}],"timestamp":"123"})");
    EXPECT_EQ(result.snapshotCount, 1U);
    ASSERT_EQ(updates.size(), polymarketBookUpdateCount);
    EXPECT_EQ(updates[0].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(firstPair, LadderSlot::PolymarketBuyYes));
    EXPECT_EQ(updates[1].kind, UpdateKind::ClearLadder);
    EXPECT_EQ(updates[1].ladderIndex, ladderIndexFor(secondPair, LadderSlot::PolymarketBuyNo));
    EXPECT_EQ(updates[clearUpdatesPerPolymarketBook].price, polymarketFirstAsk);
    EXPECT_EQ(updates[clearUpdatesPerPolymarketBook].quantity, polymarketFirstAskQuantity);
    EXPECT_EQ(updates[polymarketSecondLevelUpdateIndex].price, polymarketSecondAsk);
    EXPECT_EQ(updates[polymarketSecondLevelUpdateIndex].quantity, polymarketSecondAskQuantity);
    EXPECT_EQ(updates.back().venue, Venue::Polymarket);
}

TEST_F(PolymarketMessageParserTest, InitialArrayOfBooks)
{
    const auto result = parse(R"([{"event_type":"book","asset_id":"yes-token","asks":[]},{"event_type":"book","asset_id":"no-token","asks":[{"price":"0.4","size":"1"}]}])");
    EXPECT_EQ(result.snapshotCount, polymarketBookCount);
    EXPECT_EQ(updates.size(), clearUpdatesPerPolymarketBook * polymarketBookCount + polymarketLaddersPerToken);
}

TEST_F(PolymarketMessageParserTest, PriceChangesApplySellSideLevels)
{
    parse(R"({"event_type":"price_change","market":"0x1","price_changes":[{"asset_id":"no-token","price":"0.61","size":"0","side":"SELL"},{"asset_id":"no-token","price":"0.39","size":"10","side":"BUY"}],"timestamp":"1"})");
    ASSERT_EQ(updates.size(), polymarketLaddersPerToken);
    EXPECT_EQ(updates[0].kind, UpdateKind::SetLevel);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(firstPair, LadderSlot::PolymarketBuyNo));
    EXPECT_EQ(updates[0].price, polymarketRemovedAsk);
    EXPECT_EQ(updates[0].quantity, 0);
    EXPECT_EQ(updates[1].ladderIndex, ladderIndexFor(secondPair, LadderSlot::PolymarketBuyYes));
}

TEST_F(PolymarketMessageParserTest, LegacyPriceChangeFormat)
{
    parse(R"({"event_type":"price_change","asset_id":"yes-token","changes":[{"price":"0.4","side":"SELL","size":"3300"}]})");
    ASSERT_EQ(updates.size(), polymarketLaddersPerToken);
    EXPECT_EQ(updates[0].ladderIndex, ladderIndexFor(firstPair, LadderSlot::PolymarketBuyYes));
    EXPECT_EQ(updates[1].ladderIndex, ladderIndexFor(secondPair, LadderSlot::PolymarketBuyNo));
    EXPECT_EQ(updates[0].price, polymarketLegacyAsk);
    EXPECT_EQ(updates[0].quantity, polymarketLegacyQuantity);
}

TEST_F(PolymarketMessageParserTest, IgnoresPongsAndUnknownTokens)
{
    EXPECT_FALSE(parse("PONG").isMalformed);
    parse(R"({"event_type":"book","asset_id":"unknown","asks":[{"price":"0.4","size":"1"}]})");
    parse(R"({"event_type":"last_trade_price","asset_id":"yes-token","price":"0.5"})");
    EXPECT_TRUE(updates.empty());
    EXPECT_TRUE(parse("{broken").isMalformed);
}

TEST_F(PolymarketMessageParserTest, ParsingDoesNotAllocateAfterWarmup)
{
    const std::string_view book = R"({"event_type":"book","asset_id":"yes-token","asks":[{"price":"0.52","size":"25"},{"price":"0.53","size":"60.5"}]})";
    const std::string_view change = R"({"event_type":"price_change","price_changes":[{"asset_id":"no-token","price":"0.61","size":"5","side":"SELL"}]})";
    std::size_t produced = 0;
    auto count = [&produced](const LadderUpdate&) { ++produced; };
    parser.parse(book, warmupReceivedAtNanos, LadderUpdateSink(count));
    parser.parse(change, warmupReceivedAtNanos, LadderUpdateSink(count));

    const testing_support::AllocationScope scope;
    for (int iteration = 0; iteration < allocationCheckIterations; ++iteration) {
        parser.parse(book, iteration, LadderUpdateSink(count));
        parser.parse(change, iteration, LadderUpdateSink(count));
    }
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
    EXPECT_EQ(produced, (allocationCheckIterations + 1) * polymarketUpdatesPerBookAndChange);
}

}
}
