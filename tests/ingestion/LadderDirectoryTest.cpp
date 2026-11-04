#include "ingestion/KalshiBookFeed.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/PolymarketBookFeed.h"
#include "ingestion/UrlEncoding.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace kparb {
namespace {

TEST(LadderDirectoryTest, FansOutSharedMarketsToEveryPair)
{
    const LadderDirectory directory(std::vector<PairSubscription>{
        {"K1", "yes-a", "no-a"},
        {"K1", "yes-b", "no-b"},
        {"K2", "no-a", "yes-a"},
    });
    EXPECT_EQ(directory.pairCount(), 3U);
    const auto* kalshiTargets = directory.findKalshiMarket("K1");
    ASSERT_NE(kalshiTargets, nullptr);
    ASSERT_EQ(kalshiTargets->size(), 2U);
    EXPECT_EQ((*kalshiTargets)[1].buyNoLadder, ladderIndexFor(1, LadderSlot::KalshiBuyNo));

    const auto* tokenLadders = directory.findPolymarketToken("yes-a");
    ASSERT_NE(tokenLadders, nullptr);
    ASSERT_EQ(tokenLadders->size(), 2U);
    EXPECT_EQ((*tokenLadders)[0], ladderIndexFor(0, LadderSlot::PolymarketBuyYes));
    EXPECT_EQ((*tokenLadders)[1], ladderIndexFor(2, LadderSlot::PolymarketBuyNo));
    EXPECT_EQ(directory.findPolymarketToken("missing"), nullptr);
    EXPECT_EQ(directory.kalshiTickers().size(), 2U);
    EXPECT_EQ(directory.polymarketTokens().size(), 4U);
}

TEST(LadderDirectoryTest, SubscriptionMessagesAreChunked)
{
    const std::vector<std::string> tickers{"A", "B", "C"};
    const auto kalshiMessages = buildKalshiSubscriptionMessages(tickers, 2);
    ASSERT_EQ(kalshiMessages.size(), 2U);
    EXPECT_EQ(kalshiMessages[0], R"({"cmd":"subscribe","id":1,"params":{"channels":["orderbook_delta"],"market_tickers":["A","B"]}})");

    const auto polymarketMessages = buildPolymarketSubscriptionMessages(tickers, 2);
    ASSERT_EQ(polymarketMessages.size(), 2U);
    EXPECT_EQ(polymarketMessages[0], R"({"assets_ids":["A","B"],"type":"market"})");
    EXPECT_EQ(polymarketMessages[1], R"({"assets_ids":["C"],"operation":"subscribe"})");
}

TEST(LadderDirectoryTest, UrlHelpers)
{
    EXPECT_EQ(extractUrlPath("wss://api.elections.kalshi.com/trade-api/ws/v2"), "/trade-api/ws/v2");
    EXPECT_EQ(extractUrlPath("https://host/path?query=1"), "/path");
    EXPECT_EQ(extractUrlPath("https://host"), "/");
    EXPECT_EQ(encodeUrlComponent("a+b=c/d"), "a%2Bb%3Dc%2Fd");
    EXPECT_EQ(extractUrlOrigin("wss://api.elections.kalshi.com/trade-api/ws/v2"), "https://api.elections.kalshi.com");
    EXPECT_EQ(extractUrlOrigin("https://host:443/path"), "https://host");
    EXPECT_EQ(extractUrlOrigin("http://host:8080/path"), "http://host:8080");
}

}
}
