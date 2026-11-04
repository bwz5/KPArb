#include "app/ApplicationConfig.h"
#include "app/MatchedPairFile.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace kparb {
namespace {

TEST(ApplicationConfigTest, ExampleConfigParses)
{
    const auto config = loadApplicationConfig(std::string(KPARB_FIXTURE_DIRECTORY) + "/../../config/config.example.json");
    EXPECT_EQ(config.languageModel.baseUrl, "https://api.groq.com/openai/v1");
    EXPECT_EQ(config.kalshiFeed.connection.url, "wss://api.elections.kalshi.com/trade-api/ws/v2");
    EXPECT_EQ(config.matcher.candidates.maximumCloseTimeDifferenceSeconds, 2 * 86400);
    EXPECT_DOUBLE_EQ(config.maximumDaysUntilResolution, 2.0);
    EXPECT_EQ(config.rematchIntervalMinutes, 60);
    EXPECT_EQ(config.pairsBeforeStreaming, 500U);
    EXPECT_EQ(config.pairsBetweenStreamRestarts, 250U);
    EXPECT_EQ(config.engine.minimumEdgeTicks, 5);
    EXPECT_EQ(config.engine.statsIntervalNanos, 10'000'000'000);
    EXPECT_DOUBLE_EQ(config.kalshiTakerFeeRate, 0.07);
}

TEST(ApplicationConfigTest, PartialConfigKeepsDefaults)
{
    const auto config = parseApplicationConfig(R"({"arbitrage":{"minimumEdgeTicks":12}})");
    EXPECT_EQ(config.engine.minimumEdgeTicks, 12);
    EXPECT_EQ(config.languageModel.model, "openai/gpt-oss-120b");
    EXPECT_EQ(config.languageModel.reasoningEffort, "low");
    EXPECT_EQ(config.polymarketFeed.connection.textPingIntervalSeconds, 10);
}

TEST(ApplicationConfigTest, MissingConfigFileUsesDefaults)
{
    const auto config = loadApplicationConfig("/nonexistent/config.json");
    EXPECT_EQ(config.matchCachePath, "data/match_cache.json");
}

TEST(MatchedPairFileTest, RoundTrips)
{
    const auto path = std::filesystem::temp_directory_path() / "kparb_pairs_test.json";
    MatchedPair pair;
    pair.kalshiTicker = "K";
    pair.polymarketIdentifier = "P";
    pair.polymarketSlug = "slug";
    pair.polymarketYesToken = "Y";
    pair.polymarketNoToken = "N";
    pair.isInverted = true;
    pair.confidence = 0.9;
    pair.polymarketTakerFeeRate = 0.04;
    saveMatchedPairs(path, {pair});
    const auto loaded = loadMatchedPairs(path);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded[0].polymarketSlug, "slug");
    EXPECT_TRUE(loaded[0].isInverted);
    EXPECT_DOUBLE_EQ(loaded[0].polymarketTakerFeeRate, 0.04);
    std::filesystem::remove(path);
}

}
}
