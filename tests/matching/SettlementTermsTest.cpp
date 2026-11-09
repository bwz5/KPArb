#include "TestConstants.h"
#include "matching/SettlementTerms.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::settlement_test;

MarketInfo market(Venue venue, std::string title, std::string rules)
{
    MarketInfo info;
    info.venue = venue;
    info.title = std::move(title);
    info.rules = std::move(rules);
    return info;
}

const char* const kalshiGameRules
    = "If Oklahoma City wins the Oklahoma City vs Los Angeles C Pro Basketball game originally scheduled for Nov 13, 2026, then the market "
      "resolves to Yes.\nIf the game is postponed but begins within 48 hours of its originally scheduled start time, the market will remain "
      "open and resolve based on the official final result. If the game is cancelled or not started within 48 hours of its originally "
      "scheduled start, all markets will resolve to a fair price.";

const char* const kalshiSoccerRules
    = "If Tottenham wins the Tottenham vs Ipswich Town professional EPL soccer game originally scheduled for Nov 21, 2026 after 90 minutes "
      "plus stoppage time (does not include extra time or penalties), then the market resolves to Yes.\nIf the game is cancelled or "
      "rescheduled to over 48 hours away, the market will resolve to a fair price in accordance with the rules.";

TEST(SettlementTermsTest, TwoWayGameCarriesFiftyFiftyNoResultRisk)
{
    const MarketInfo kalshi = market(Venue::Kalshi, "Oklahoma City wins", kalshiGameRules);
    const MarketInfo polymarket = market(Venue::Polymarket, "Thunder vs. Clippers",
        "If the Thunder win, the market will resolve to \"Thunder\".\nIf the game is canceled, does not start by Wednesday, November 15, "
        "2026, 11:59 PM ET, or is abandoned with no official result by that time, this market will resolve 50-50.\nThe result will be "
        "determined based on the final score including any overtime periods.");
    const SettlementComparison comparison = compareSettlementTerms(kalshi, polymarket);
    EXPECT_TRUE(comparison.isCompatible);
    EXPECT_TRUE(comparison.carriesNoResultRisk);
    EXPECT_DOUBLE_EQ(comparison.polymarketYesNoResultValue, fiftyFiftyValue);
    EXPECT_DOUBLE_EQ(comparison.polymarketNoNoResultValue, fiftyFiftyValue);
    EXPECT_EQ(readSettlementTerms(kalshi).noResult, NoResultSettlement::FairPrice);
}

TEST(SettlementTermsTest, SoccerYesNoMarketPaysNoWhenCancelled)
{
    const MarketInfo kalshi = market(Venue::Kalshi, "Tottenham wins", kalshiSoccerRules);
    const MarketInfo polymarket = market(Venue::Polymarket, "Will Tottenham Hotspur FC win on 2026-11-21?",
        "If Tottenham Hotspur FC wins, this market will resolve to \"Yes\".\nOtherwise, this market will resolve to \"No\".\nIf the game is "
        "canceled, does not start by Monday, November 23, 2026, 11:59 PM ET, or is abandoned with no official result by that time, this "
        "market will resolve to \"No\".\nThis market refers only to the outcome within the first 90 minutes of regular play plus stoppage time.");
    const SettlementComparison comparison = compareSettlementTerms(kalshi, polymarket);
    EXPECT_TRUE(comparison.isCompatible);
    EXPECT_TRUE(comparison.carriesNoResultRisk);
    EXPECT_DOUBLE_EQ(comparison.polymarketYesNoResultValue, worthlessOnNoResult);
    EXPECT_DOUBLE_EQ(comparison.polymarketNoNoResultValue, fullValueOnNoResult);
}

TEST(SettlementTermsTest, RegulationTimeDoesNotPairWithExtraTime)
{
    const MarketInfo kalshi = market(Venue::Kalshi, "Tottenham wins", kalshiSoccerRules);
    const MarketInfo polymarket = market(Venue::Polymarket, "Will Tottenham Hotspur FC advance?",
        "This market will resolve to \"Yes\" if Tottenham Hotspur FC advances, including extra time and penalties.");
    const SettlementComparison comparison = compareSettlementTerms(kalshi, polymarket);
    EXPECT_FALSE(comparison.isCompatible);
    EXPECT_NE(comparison.conflict.find("regulation"), std::string::npos);
}

TEST(SettlementTermsTest, MapMarketDoesNotPairWithFullMatch)
{
    const MarketInfo kalshi = market(Venue::Kalshi, "Will TTG Esports win the NoTime vs. TTG Esports CS2 match?",
        "The following market refers to the full match.\nIf the relevant match is cancelled before play begins, the market will resolve to "
        "the fair market price.");
    const MarketInfo polymarket = market(Venue::Polymarket, "Counter-Strike: NoTime vs TTG Esports - Map 1 Winner",
        "If Map 1 is not completed for any reason, this market will resolve 50-50.");
    const SettlementComparison comparison = compareSettlementTerms(kalshi, polymarket);
    EXPECT_FALSE(comparison.isCompatible);
    EXPECT_NE(comparison.conflict.find("map 1"), std::string::npos);
    EXPECT_EQ(partialScopeOf("Will the Chiefs lead at the end of the 1st Half?"), "first half");
    EXPECT_EQ(partialScopeOf("Setka Cup: Ivanov vs Petrov"), "");
}

TEST(SettlementTermsTest, MarketsWithoutCancellationClausesCarryNoRisk)
{
    const MarketInfo kalshi = market(Venue::Kalshi, "Will the Fed cut rates in December 2026?", "If the Fed cuts rates, the market resolves to Yes.");
    const MarketInfo polymarket = market(Venue::Polymarket, "Fed cuts rates in December 2026?", "Resolves to Yes if the Fed cuts rates.");
    const SettlementComparison comparison = compareSettlementTerms(kalshi, polymarket);
    EXPECT_TRUE(comparison.isCompatible);
    EXPECT_FALSE(comparison.carriesNoResultRisk);
}

}
}
