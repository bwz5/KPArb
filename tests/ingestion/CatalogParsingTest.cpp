#include "TestConstants.h"
#include "ingestion/KalshiMarketCatalog.h"
#include "ingestion/PolymarketMarketCatalog.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace kparb {
namespace {

using namespace test_constants::catalog_test;
using test_constants::fees::linearFeeExponent;
using test_constants::time_units::secondsPerDay;

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
    ASSERT_EQ(markets.size(), kalshiFixtureMarketCount);
    EXPECT_EQ(markets[0].identifier, "KXFEDDECISION-26DEC-H0");
    EXPECT_EQ(markets[0].subtitle, "Hold rates");
    EXPECT_EQ(markets[0].closeTimeSeconds, kalshiFixtureCloseSeconds);
    EXPECT_NE(markets[0].rules.find("Source: Federal Reserve."), std::string::npos);
    EXPECT_EQ(markets[1].identifier, "KXBTC-26DEC31-T100000");
    EXPECT_EQ(markets[1].noOutcomeName, "Below $100,000");
    EXPECT_TRUE(markets[0].isAcceptingOrders);
    EXPECT_EQ(markets[0].openTimeSeconds, kalshiFixtureOpenSeconds);
    EXPECT_FALSE(markets[1].isAcceptingOrders);
}

TEST(CatalogParsingTest, ParsesOnlyOpenBinaryPolymarketMarkets)
{
    std::vector<MarketInfo> markets;
    const PolymarketPage page = parsePolymarketMarketsPage(readFixture("polymarket_markets_page.json"), markets);
    EXPECT_EQ(page.entryCount, polymarketFixtureEntryCount);
    EXPECT_TRUE(page.nextCursor.empty());
    ASSERT_EQ(markets.size(), 1U);
    const MarketInfo& market = markets[0];
    EXPECT_EQ(market.identifier, "0xabc123");
    EXPECT_EQ(market.eventIdentifier, "fed-hold-december-2026");
    EXPECT_EQ(market.yesOutcomeName, "Yes");
    EXPECT_EQ(market.noOutcomeName, "No");
    EXPECT_EQ(market.yesTokenIdentifier.front(), '1');
    EXPECT_EQ(market.noTokenIdentifier.front(), '2');
    EXPECT_EQ(market.closeTimeSeconds, polymarketFixtureCloseSeconds);
    EXPECT_DOUBLE_EQ(market.takerFeeRate, polymarketFixtureFeeRate);
    EXPECT_DOUBLE_EQ(market.takerFeeExponent, linearFeeExponent);
    EXPECT_TRUE(market.isAcceptingOrders);
    EXPECT_EQ(market.openTimeSeconds, polymarketFixtureOpenSeconds);
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

TEST(CatalogParsingTest, ParsesPolymarketSportsStructure)
{
    std::vector<MarketInfo> markets;
    const std::string body = R"json({"markets":[
        {"question":"KK Bosna vs. Balkan","conditionId":"0xmoney","outcomes":"[\"KK Bosna\",\"Balkan\"]","clobTokenIds":"[\"a\",\"b\"]",
         "sportsMarketType":"moneyline","gameStartTime":"2026-11-04 18:30:00+00","endDate":"2026-11-11T18:30:00Z",
         "description":"In the upcoming EuroCup game between KK Bosna and Balkan.\n\nThis market resolves to the winner.",
         "events":[{"title":"KK Bosna vs. Balkan"}]},
        {"question":"Spread: KK Bosna (-7.5)","conditionId":"0xspread","outcomes":"[\"KK Bosna\",\"Balkan\"]","clobTokenIds":"[\"c\",\"d\"]",
         "sportsMarketType":"spreads","line":-7.5,"endDate":"2026-11-04T18:30:00Z"}
    ]})json";
    parsePolymarketMarketsPage(body, markets);
    ASSERT_EQ(markets.size(), sportsMarketCount);
    EXPECT_EQ(markets[0].marketType, "winner");
    EXPECT_EQ(markets[0].eventTitle, "KK Bosna vs. Balkan");
    EXPECT_EQ(markets[0].rulesSummary, "In the upcoming EuroCup game between KK Bosna and Balkan.");
    EXPECT_EQ(markets[0].closeTimeSeconds, bosnaGameStartSeconds);
    EXPECT_EQ(markets[0].scheduledStartSeconds, bosnaGameStartSeconds);
    EXPECT_EQ(markets[0].resolutionDeadlineSeconds, bosnaGameStartSeconds + bosnaResolutionDays * secondsPerDay);
    EXPECT_FALSE(markets[0].hasStrike);
    EXPECT_EQ(markets[1].marketType, "spread");
    EXPECT_TRUE(markets[1].hasStrike);
    EXPECT_DOUBLE_EQ(markets[1].strike, bosnaSpreadLine);
    EXPECT_EQ(markets[1].closeTimeSeconds, markets[1].resolutionDeadlineSeconds);
}

TEST(CatalogParsingTest, ParsesKalshiStrikesAndSeriesType)
{
    std::vector<MarketInfo> markets;
    const std::string body = R"({"markets":[
        {"ticker":"KXNBATOTAL-26NOV04ATLWAS-225","event_ticker":"KXNBATOTAL-26NOV04ATLWAS","title":"Over 225.5 points",
         "rules_primary":"If Atlanta and Washington combine for more than 225.5 points, then the market resolves to Yes.",
         "strike_type":"greater","floor_strike":225.5,"status":"active"},
        {"ticker":"KXHIGHNY-26NOV04-T50","event_ticker":"KXHIGHNY-26NOV04","title":"High below 50","strike_type":"less","cap_strike":50},
        {"ticker":"KXGAME-X-TTS","event_ticker":"KXEUROCUPGAME-X","title":"Turk Telekom wins","strike_type":"structured"}
    ],"cursor":""})";
    parseKalshiMarketsPage(body, markets);
    ASSERT_EQ(markets.size(), strikeMarketCount);
    EXPECT_EQ(markets[0].marketType, "total");
    EXPECT_TRUE(markets[0].hasStrike);
    EXPECT_DOUBLE_EQ(markets[0].strike, atlantaWashingtonTotal);
    EXPECT_EQ(markets[0].rulesSummary, markets[0].rules);
    EXPECT_TRUE(markets[1].hasStrike);
    EXPECT_DOUBLE_EQ(markets[1].strike, newYorkHighCap);
    EXPECT_EQ(markets.back().marketType, "winner");
    EXPECT_FALSE(markets.back().hasStrike);
}

}
}
