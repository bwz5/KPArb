#include "ingestion/KalshiMarketCatalog.h"
#include "ingestion/PolymarketMarketCatalog.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace kparb {
namespace {

std::string readFixture(const std::string& name)
{
    std::ifstream file(std::string(KPARB_FIXTURE_DIRECTORY) + "/" + name);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

TEST(CatalogParsingTest, ParsesKalshiBinaryMarketsAndCursor)
{
    std::vector<MarketInfo> markets;
    const std::string cursor = parseKalshiMarketsPage(readFixture("kalshi_markets_page.json"), markets);
    EXPECT_EQ(cursor, "CgwI2aOvqAYQ+NyHxwISHUtYRkVEREVDSVNJT04tMjZERUMtSDA=");
    ASSERT_EQ(markets.size(), 2U);
    EXPECT_EQ(markets[0].identifier, "KXFEDDECISION-26DEC-H0");
    EXPECT_EQ(markets[0].subtitle, "Hold rates");
    EXPECT_EQ(markets[0].closeTimeSeconds, 1796842800);
    EXPECT_NE(markets[0].rules.find("Source: Federal Reserve."), std::string::npos);
    EXPECT_EQ(markets[1].identifier, "KXBTC-26DEC31-T100000");
    EXPECT_EQ(markets[1].noOutcomeName, "Below $100,000");
}

TEST(CatalogParsingTest, ParsesOnlyOpenBinaryPolymarketMarkets)
{
    std::vector<MarketInfo> markets;
    const PolymarketPage page = parsePolymarketMarketsPage(readFixture("polymarket_markets_page.json"), markets);
    EXPECT_EQ(page.entryCount, 3U);
    EXPECT_TRUE(page.nextCursor.empty());
    ASSERT_EQ(markets.size(), 1U);
    const MarketInfo& market = markets[0];
    EXPECT_EQ(market.identifier, "0xabc123");
    EXPECT_EQ(market.eventIdentifier, "fed-hold-december-2026");
    EXPECT_EQ(market.yesOutcomeName, "Yes");
    EXPECT_EQ(market.noOutcomeName, "No");
    EXPECT_EQ(market.yesTokenIdentifier.front(), '1');
    EXPECT_EQ(market.noTokenIdentifier.front(), '2');
    EXPECT_EQ(market.closeTimeSeconds, 1796774400);
    EXPECT_DOUBLE_EQ(market.takerFeeRate, 0.04);
    EXPECT_DOUBLE_EQ(market.takerFeeExponent, 1.0);
}

TEST(CatalogParsingTest, ParsesPolymarketKeysetEnvelope)
{
    std::vector<MarketInfo> markets;
    const std::string body = R"({"markets":[{"question":"Q","conditionId":"0x1","outcomes":"[\"Yes\",\"No\"]","clobTokenIds":"[\"a\",\"b\"]"}],"next_cursor":"abc"})";
    const PolymarketPage page = parsePolymarketMarketsPage(body, markets);
    EXPECT_EQ(page.entryCount, 1U);
    EXPECT_EQ(page.nextCursor, "abc");
    ASSERT_EQ(markets.size(), 1U);
    EXPECT_EQ(markets[0].noTokenIdentifier, "b");
}

}
}
