#include "matching/CandidateFinder.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

MarketInfo market(Venue venue, std::string identifier, std::string title, std::int64_t closeTime, std::string subtitle = "")
{
    MarketInfo info;
    info.venue = venue;
    info.identifier = std::move(identifier);
    info.title = std::move(title);
    info.subtitle = std::move(subtitle);
    info.closeTimeSeconds = closeTime;
    return info;
}

class CandidateFinderTest : public ::testing::Test {
protected:
    std::vector<MarketInfo> polymarket{
        market(Venue::Polymarket, "p-fed", "Will the Fed cut rates in December 2026?", 1'000'000),
        market(Venue::Polymarket, "p-btc-100", "Will Bitcoin be above $100,000 on December 31?", 1'000'000),
        market(Venue::Polymarket, "p-btc-120", "Will Bitcoin be above $120,000 on December 31?", 1'000'000),
        market(Venue::Polymarket, "p-late", "Will the Fed cut rates in March 2027?", 9'000'000),
        market(Venue::Polymarket, "p-other", "Who will win the Super Bowl?", 1'000'000),
    };
};

TEST_F(CandidateFinderTest, FindsSimilarMarketsWithinDateWindow)
{
    const std::vector<MarketInfo> kalshi{market(Venue::Kalshi, "k-fed", "Fed decision in December 2026?", 1'100'000, "Cut rates")};
    CandidateSettings settings;
    settings.minimumSimilarity = 0.3;
    const CandidateFinder finder(polymarket, settings);
    const auto candidates = finder.findCandidates(kalshi);
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(polymarket[candidates[0].polymarketIndex].identifier, "p-fed");
    for (const auto& candidate : candidates) {
        EXPECT_NE(polymarket[candidate.polymarketIndex].identifier, "p-late");
    }
}

TEST_F(CandidateFinderTest, RejectsMismatchedNumericThresholds)
{
    const std::vector<MarketInfo> kalshi{market(Venue::Kalshi, "k-btc", "Bitcoin above $100,000 on Dec 31, 2026?", 1'000'000)};
    CandidateSettings settings;
    settings.minimumSimilarity = 0.2;
    const CandidateFinder finder(polymarket, settings);
    const auto candidates = finder.findCandidates(kalshi);
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(polymarket[candidates[0].polymarketIndex].identifier, "p-btc-100");
    for (const auto& candidate : candidates) {
        EXPECT_NE(polymarket[candidate.polymarketIndex].identifier, "p-btc-120");
    }
}

TEST_F(CandidateFinderTest, RespectsCandidateLimitAndSortsGlobally)
{
    const std::vector<MarketInfo> kalshi{
        market(Venue::Kalshi, "k-other", "Super Bowl halftime show performer?", 1'000'000),
        market(Venue::Kalshi, "k-fed", "Will the Fed cut rates in December 2026?", 1'000'000),
    };
    CandidateSettings settings;
    settings.minimumSimilarity = 0.1;
    settings.candidatesPerMarket = 1;
    const CandidateFinder finder(polymarket, settings);
    const auto candidates = finder.findCandidates(kalshi);
    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_GE(candidates[0].similarity, candidates[1].similarity);
    EXPECT_EQ(kalshi[candidates[0].kalshiIndex].identifier, "k-fed");
    EXPECT_NEAR(candidates[0].similarity, 1.0, 1e-9);
}

}
}
