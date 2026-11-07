#include "TestConstants.h"
#include "ingestion/KalshiBookFeed.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/PolymarketBookFeed.h"
#include "ingestion/UrlEncoding.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace kparb {
namespace {

using namespace test_constants::ladder_directory_test;

TEST(LadderDirectoryTest, FansOutSharedMarketsToEveryPair)
{
    const LadderDirectory directory(std::vector<PairSubscription>{
        {"K1", "yes-a", "no-a"},
        {"K1", "yes-b", "no-b"},
        {"K2", "no-a", "yes-a"},
    });
    EXPECT_EQ(directory.pairCount(), subscribedPairCount);
    const auto* kalshiTargets = directory.findKalshiMarket("K1");
    ASSERT_NE(kalshiTargets, nullptr);
    ASSERT_EQ(kalshiTargets->size(), pairsSharingKalshiMarket);
    EXPECT_EQ(kalshiTargets->back().buyNoLadder, ladderIndexFor(secondPair, LadderSlot::KalshiBuyNo));

    const auto* tokenLadders = directory.findPolymarketToken("yes-a");
    ASSERT_NE(tokenLadders, nullptr);
    ASSERT_EQ(tokenLadders->size(), pairsSharingPolymarketToken);
    EXPECT_EQ(tokenLadders->front(), ladderIndexFor(firstPair, LadderSlot::PolymarketBuyYes));
    EXPECT_EQ(tokenLadders->back(), ladderIndexFor(thirdPair, LadderSlot::PolymarketBuyNo));
    EXPECT_EQ(directory.findPolymarketToken("missing"), nullptr);
    EXPECT_EQ(directory.kalshiTickers().size(), distinctKalshiTickers);
    EXPECT_EQ(directory.polymarketTokens().size(), distinctPolymarketTokens);
}

TEST(LadderDirectoryTest, SubscriptionMessagesAreChunked)
{
    const std::vector<std::string> tickers{"A", "B", "C"};
    const auto kalshiMessages = buildKalshiSubscriptionMessages(tickers, subscriptionChunkSize);
    ASSERT_EQ(kalshiMessages.size(), subscriptionMessageCount);
    EXPECT_EQ(kalshiMessages[0], R"({"cmd":"subscribe","id":1,"params":{"channels":["orderbook_delta"],"market_tickers":["A","B"]}})");

    const auto polymarketMessages = buildPolymarketSubscriptionMessages(tickers, subscriptionChunkSize);
    ASSERT_EQ(polymarketMessages.size(), subscriptionMessageCount);
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
