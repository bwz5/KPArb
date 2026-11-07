#include "TestConstants.h"
#include "app/ApplicationConfig.h"
#include "app/MatchedPairFile.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace kparb {
namespace {

using namespace test_constants::config_test;
using namespace test_constants::time_units;
using test_constants::fees::kalshiTakerFeeRate;

TEST(ApplicationConfigTest, ExampleConfigParses)
{
    const auto config = loadApplicationConfig(std::string(KPARB_FIXTURE_DIRECTORY) + "/../../config/config.example.json");
    EXPECT_EQ(config.languageModel.baseUrl, "https://api.groq.com/openai/v1");
    EXPECT_EQ(config.kalshiFeed.connection.url, "wss://api.elections.kalshi.com/trade-api/ws/v2");
    EXPECT_EQ(config.matcher.candidates.maximumCloseTimeDifferenceSeconds, exampleCloseTimeDifferenceDays * secondsPerDay);
    EXPECT_DOUBLE_EQ(config.maximumDaysUntilResolution, exampleMaximumDaysUntilResolution);
    EXPECT_EQ(config.rematchIntervalMinutes, exampleRematchIntervalMinutes);
    EXPECT_EQ(config.pairsBeforeStreaming, examplePairsBeforeStreaming);
    EXPECT_EQ(config.maximumMinutesBeforeStreaming, exampleMaximumMinutesBeforeStreaming);
    EXPECT_EQ(config.pairsBetweenStreamRestarts, examplePairsBetweenStreamRestarts);
    EXPECT_EQ(config.engine.minimumEdgeTicks, defaultMinimumEdgeTicks);
    EXPECT_EQ(config.engine.statsIntervalNanos, exampleStatsIntervalSeconds * nanosecondsPerSecond);
    EXPECT_DOUBLE_EQ(config.kalshiTakerFeeRate, kalshiTakerFeeRate);
    EXPECT_EQ(config.polymarketMoneylineSearchDays, examplePolymarketMoneylineSearchDays);
    EXPECT_EQ(config.matcher.concurrentRequests, exampleConcurrentRequests);
    EXPECT_EQ(config.matcher.candidates.candidatesPerPolymarketMarket, exampleCandidatesPerPolymarketMarket);
    EXPECT_TRUE(config.matcher.candidates.requireCompatibleMarketTypes);
    EXPECT_TRUE(config.matcher.candidates.requireMatchingStrikes);
    EXPECT_DOUBLE_EQ(config.matcher.minimumConfidence, exampleMinimumConfidence);
}

TEST(ApplicationConfigTest, PartialConfigKeepsDefaults)
{
    const auto config = parseApplicationConfig(
        R"({"arbitrage":{"minimumEdgeTicks":)" + std::to_string(overriddenMinimumEdgeTicks) + "}}");
    EXPECT_EQ(config.engine.minimumEdgeTicks, overriddenMinimumEdgeTicks);
    EXPECT_EQ(config.languageModel.model, "openai/gpt-oss-120b");
    EXPECT_EQ(config.languageModel.fallbackModels, (std::vector<std::string>{"openai/gpt-oss-20b", "qwen/qwen3.8-27b"}));
    EXPECT_EQ(config.languageModel.reasoningEffort, "low");
    EXPECT_EQ(config.polymarketFeed.connection.textPingIntervalSeconds, defaultTextPingIntervalSeconds);
}

TEST(ApplicationConfigTest, EmptyConfigUsesDefaults)
{
    const auto config = parseApplicationConfig("  \n");
    EXPECT_EQ(config.engine.minimumEdgeTicks, defaultMinimumEdgeTicks);
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
    pair.confidence = savedPairConfidence;
    pair.polymarketTakerFeeRate = savedPairFeeRate;
    saveMatchedPairs(path, {pair});
    const auto loaded = loadMatchedPairs(path);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded[0].polymarketSlug, "slug");
    EXPECT_TRUE(loaded[0].isInverted);
    EXPECT_DOUBLE_EQ(loaded[0].polymarketTakerFeeRate, savedPairFeeRate);
    std::filesystem::remove(path);
}

}
}
