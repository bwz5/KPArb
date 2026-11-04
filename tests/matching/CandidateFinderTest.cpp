#include "ingestion/MarketClassification.h"
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
    EXPECT_GE(candidates[0].score, candidates[1].score);
    EXPECT_EQ(kalshi[candidates[0].kalshiIndex].identifier, "k-fed");
    EXPECT_NEAR(candidates[0].score, 1.0, 1e-9);
}

MarketInfo sportsMarket(Venue venue, std::string identifier, std::string title, std::string marketType, double strike = 0.0, bool hasStrike = false)
{
    MarketInfo info = market(venue, std::move(identifier), std::move(title), 1'000'000);
    info.marketType = std::move(marketType);
    info.strike = strike;
    info.hasStrike = hasStrike;
    return info;
}

TEST(MarketClassificationTest, ClassifiesKalshiSeries)
{
    EXPECT_EQ(kalshiMarketType("KXEUROCUPGAME-26NOV041130SKYTTS"), "winner");
    EXPECT_EQ(kalshiMarketType("KXITFMATCH-26NOV04ABCDEF"), "winner");
    EXPECT_EQ(kalshiMarketType("KXNBASPREAD-26NOV04ATLWAS"), "spread");
    EXPECT_EQ(kalshiMarketType("KXNBATOTAL-26NOV04ATLWAS"), "total");
    EXPECT_EQ(kalshiMarketType("KXNBATEAMTOTAL-26NOV04ATLWAS"), "team_total");
    EXPECT_EQ(kalshiMarketType("KXNBA1HSPREAD-26NOV04ATLWAS"), "first_half_spread");
    EXPECT_EQ(kalshiMarketType("KXNBA2QTOTAL-26NOV04ATLWAS"), "q2_total");
    EXPECT_EQ(kalshiMarketType("KXNBAPTS-26NOV04ATLWAS"), "");
    EXPECT_EQ(kalshiMarketType("KXRAIN-26NOV04"), "");
    EXPECT_EQ(kalshiMarketType("KXUELSCORE-26NOV05SUNDZG"), "exact_score");
    EXPECT_EQ(kalshiMarketType("KXUEL1HBTTS-26NOV05SUNDZG"), "first_half_both_teams_to_score");
    EXPECT_EQ(kalshiMarketType("KXUCL1HSCORE-26NOV05ABCDEF"), "first_half_exact_score");
}

TEST(MarketClassificationTest, ClassifiesPolymarketSportsTypes)
{
    EXPECT_EQ(polymarketMarketType("moneyline"), "winner");
    EXPECT_EQ(polymarketMarketType("spreads"), "spread");
    EXPECT_EQ(polymarketMarketType("totals"), "total");
    EXPECT_EQ(polymarketMarketType("first_half_spreads"), "first_half_spread");
    EXPECT_EQ(polymarketMarketType("q2_totals"), "q2_total");
    EXPECT_EQ(polymarketMarketType("soccer_team_totals"), "team_total");
    EXPECT_EQ(polymarketMarketType("soccer_exact_score"), "exact_score");
    EXPECT_EQ(polymarketMarketType(""), "");
    EXPECT_TRUE(areMarketTypesCompatible("", "spread"));
    EXPECT_TRUE(areMarketTypesCompatible("winner", "winner"));
    EXPECT_FALSE(areMarketTypesCompatible("winner", "spread"));
    EXPECT_FALSE(areMarketTypesCompatible("winner", ""));
}

TEST(CandidateFilterTest, GameWinnerPairsWithMoneylineInsteadOfSpread)
{
    const std::vector<MarketInfo> polymarketMarkets{
        sportsMarket(Venue::Polymarket, "p-spread", "Spread: Turk Telekom (-7.5) Fraport Skyliners vs. Turk Telekom", "spread", 7.5, true),
        sportsMarket(Venue::Polymarket, "p-moneyline", "Fraport Skyliners vs. Turk Telekom", "winner"),
    };
    MarketInfo kalshiMarket = sportsMarket(Venue::Kalshi, "k-game", "Turk Telekom wins", "winner");
    kalshiMarket.rulesSummary = "If Turk Telekom wins the Fraport Skyliners vs Turk Telekom EuroCup basketball game, then the market resolves to Yes.";
    CandidateSettings settings;
    settings.minimumSimilarity = 0.1;
    const CandidateFinder finder(polymarketMarkets, settings);
    const auto candidates = finder.findCandidates({kalshiMarket});
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(polymarketMarkets[candidates[0].polymarketIndex].identifier, "p-moneyline");
}

TEST(CandidateFilterTest, StructuredStrikesMustMatch)
{
    const std::vector<MarketInfo> polymarketMarkets{
        sportsMarket(Venue::Polymarket, "p-total-2205", "Hawks vs. Wizards: O/U 220.5", "total", 220.5, true),
        sportsMarket(Venue::Polymarket, "p-total-2255", "Hawks vs. Wizards: O/U 225.5", "total", 225.5, true),
    };
    const MarketInfo kalshiMarket = sportsMarket(Venue::Kalshi, "k-total", "Hawks vs. Wizards: Over 225.5 points", "total", 225.5, true);
    CandidateSettings settings;
    settings.minimumSimilarity = 0.1;
    settings.requireCompatibleNumbers = false;
    const CandidateFinder finder(polymarketMarkets, settings);
    const auto candidates = finder.findCandidates({kalshiMarket});
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(polymarketMarkets[candidates[0].polymarketIndex].identifier, "p-total-2255");
}

TEST(CandidateFilterTest, EachPolymarketMarketKeepsOnlyTheClosestKalshiMarkets)
{
    const std::vector<MarketInfo> polymarketMarkets{market(Venue::Polymarket, "p-btc", "Bitcoin above $72,000 on November 4?", 1'000'000)};
    std::vector<MarketInfo> kalshiMarkets;
    for (int hour = 0; hour < 6; ++hour) {
        kalshiMarkets.push_back(market(Venue::Kalshi, "k-btc-" + std::to_string(hour), "Bitcoin above $72,000 on November 4?",
            1'000'000 + (hour - 2) * 3600));
    }
    CandidateSettings settings;
    settings.minimumSimilarity = 0.1;
    settings.candidatesPerPolymarketMarket = 2;
    const CandidateFinder finder(polymarketMarkets, settings);
    const auto candidates = finder.findCandidates(kalshiMarkets);
    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_EQ(kalshiMarkets[candidates[0].kalshiIndex].identifier, "k-btc-2");
    EXPECT_EQ(candidates[1].closeTimeDifferenceSeconds, 3600);
}

TEST(CandidateFilterTest, SharedTeamsBridgeCityAndNicknameAndRejectOtherGames)
{
    std::vector<MarketInfo> polymarketMarkets{
        sportsMarket(Venue::Polymarket, "p-cgy-sea", "Flames vs. Kraken", "winner"),
        sportsMarket(Venue::Polymarket, "p-sj-la", "Sharks vs. Kings", "winner"),
    };
    polymarketMarkets[0].teamKeys = {"nhl:CGY", "nhl:SEA"};
    polymarketMarkets[1].teamKeys = {"nhl:LA", "nhl:SJ"};
    MarketInfo kalshiMarket = sportsMarket(Venue::Kalshi, "k-cgy", "Calgary wins", "winner");
    kalshiMarket.rulesSummary = "If Calgary wins the Calgary vs Seattle NHL game originally scheduled for Nov 4, 2026, then the market resolves to Yes.";
    kalshiMarket.teamKeys = {"nhl:CGY", "nhl:SEA"};
    CandidateSettings settings;
    const CandidateFinder finder(polymarketMarkets, settings);
    const auto candidates = finder.findCandidates({kalshiMarket});
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(polymarketMarkets[candidates[0].polymarketIndex].identifier, "p-cgy-sea");
    EXPECT_GE(candidates[0].score, 2 * settings.sharedTeamBonus);
}

TEST(CandidateFilterTest, MarketsInEntityScopeMustNameTheSameEntity)
{
    MarketInfo kalshiWomen = sportsMarket(Venue::Kalshi, "k-women", "Napoli wins", "winner");
    kalshiWomen.entityLeague = "kxserieaw";
    MarketInfo polymarketMen = sportsMarket(Venue::Polymarket, "p-men", "SSC Napoli vs. FC Internazionale Milano", "winner");
    polymarketMen.entityLeague = "sea";
    polymarketMen.teamKeys = {"sea:INT", "sea:NAP"};
    MarketInfo kalshiProp = sportsMarket(Venue::Kalshi, "k-prop", "LeBron James: 25+ points", "");
    kalshiProp.entityLeague = "nba";
    MarketInfo polymarketProp = sportsMarket(Venue::Polymarket, "p-prop", "LeBron James: Points O/U 24.5", "");
    polymarketProp.entityLeague = "nba";
    MarketInfo polymarketTennis = sportsMarket(Venue::Polymarket, "p-tennis", "Sinner vs. Alcaraz", "winner");
    MarketInfo kalshiMen = kalshiWomen;
    kalshiMen.entityLeague = "sea";
    kalshiMen.teamKeys = {"sea:INT", "sea:NAP"};
    EXPECT_FALSE(areEntitiesCompatible(kalshiWomen, polymarketMen, 0));
    EXPECT_TRUE(areEntitiesCompatible(kalshiMen, polymarketMen, 2));
    EXPECT_TRUE(areEntitiesCompatible(kalshiProp, polymarketProp, 0));
    EXPECT_TRUE(areEntitiesCompatible(kalshiMen, polymarketTennis, 0));
    polymarketTennis.teamKeys = {"atp:SIN"};
    EXPECT_FALSE(areEntitiesCompatible(kalshiMen, polymarketTennis, 0));
}

TEST(CandidateFilterTest, PolymarketLinesNeedAKalshiStrike)
{
    MarketInfo kalshiLeader = market(Venue::Kalshi, "k-leader", "Austin Reaves: Points leader", 1'000'000);
    MarketInfo kalshiThreshold = market(Venue::Kalshi, "k-10", "Austin Reaves: 10+ points", 1'000'000);
    kalshiThreshold.strike = 9.5;
    kalshiThreshold.hasStrike = true;
    MarketInfo polymarketLine = market(Venue::Polymarket, "p-line", "Austin Reaves: Points O/U 9.5", 1'000'000);
    polymarketLine.strike = 9.5;
    polymarketLine.hasStrike = true;
    const MarketInfo polymarketRain = market(Venue::Polymarket, "p-rain", "Will it rain in Washington, DC?", 1'000'000);
    EXPECT_FALSE(areStrikesCompatible(kalshiLeader, polymarketLine));
    EXPECT_TRUE(areStrikesCompatible(kalshiThreshold, polymarketLine));
    EXPECT_TRUE(areStrikesCompatible(kalshiThreshold, polymarketRain));
}

TEST(CandidateFilterTest, SpreadSubjectTeamsMustAgree)
{
    MarketInfo kalshiSpread = market(Venue::Kalshi, "k-chi", "Chicago wins by over 2.5 goals", 1'000'000);
    kalshiSpread.subjectTeamKey = "nhl:CHI";
    MarketInfo panthersSpread = market(Venue::Polymarket, "p-fla", "Spread: Panthers (-2.5)", 1'000'000);
    panthersSpread.subjectTeamKey = "nhl:FLA";
    MarketInfo blackhawksSpread = market(Venue::Polymarket, "p-chi", "Spread: Blackhawks (-2.5)", 1'000'000);
    blackhawksSpread.subjectTeamKey = "nhl:CHI";
    const MarketInfo moneyline = market(Venue::Polymarket, "p-ml", "Blackhawks vs. Panthers", 1'000'000);
    EXPECT_FALSE(areSubjectTeamsCompatible(kalshiSpread, panthersSpread));
    EXPECT_TRUE(areSubjectTeamsCompatible(kalshiSpread, blackhawksSpread));
    EXPECT_TRUE(areSubjectTeamsCompatible(kalshiSpread, moneyline));
}

TEST(CandidateFilterTest, PlayerPropsDoNotGetTheTeamBonus)
{
    std::vector<MarketInfo> polymarketMarkets{sportsMarket(Venue::Polymarket, "p-margin", "Exact Margin: Bucks by 16-20", "exact_margin")};
    polymarketMarkets[0].teamKeys = {"nba:LAC", "nba:MIL"};
    MarketInfo kalshiMarket = sportsMarket(Venue::Kalshi, "k-prop", "Paul George: 20+ points", "");
    kalshiMarket.teamKeys = {"nba:LAC", "nba:MIL"};
    const CandidateFinder finder(polymarketMarkets, CandidateSettings{});
    EXPECT_TRUE(finder.findCandidates({kalshiMarket}).empty());
}

}
}
