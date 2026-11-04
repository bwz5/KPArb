#include "ingestion/TeamDirectory.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

const char* const teamsJson = R"({"leagues":[
    {"name":"nhl","kalshiSeriesPrefix":"KXNHL","polymarketSlugPrefix":"nhl-",
     "teams":{"CGY":["Flames"],"SEA":["Kraken"],"LA":["Kings"],"SJ":["Sharks"],"TOR":["Maple Leafs"]}},
    {"name":"nba","kalshiSeriesPrefix":"KXNBA","polymarketSlugPrefix":"nba-",
     "teams":{"SAC":["Kings"],"POR":["Trail Blazers","Blazers"]}}
]})";

MarketInfo kalshiMarket(std::string eventTicker)
{
    MarketInfo market;
    market.venue = Venue::Kalshi;
    market.eventIdentifier = std::move(eventTicker);
    return market;
}

MarketInfo polymarketMarket(std::string slug, std::string title)
{
    MarketInfo market;
    market.venue = Venue::Polymarket;
    market.eventIdentifier = std::move(slug);
    market.title = std::move(title);
    return market;
}

TEST(TeamDirectoryTest, ReadsTeamCodesFromKalshiEventTickers)
{
    const TeamDirectory teams = TeamDirectory::parse(teamsJson);
    EXPECT_EQ(teams.leagueCount(), 2U);
    EXPECT_EQ(teams.teamKeysFor(kalshiMarket("KXNHLGAME-26NOV04CGYSEA")), (std::vector<std::string>{"nhl:CGY", "nhl:SEA"}));
    EXPECT_EQ(teams.teamKeysFor(kalshiMarket("KXNHLTOTAL-26NOV04SJLA")), (std::vector<std::string>{"nhl:LA", "nhl:SJ"}));
    EXPECT_EQ(teams.teamKeysFor(kalshiMarket("KXNBASPREAD-26NOV041930SACPOR")), (std::vector<std::string>{"nba:POR", "nba:SAC"}));
    EXPECT_TRUE(teams.teamKeysFor(kalshiMarket("KXNHLGAME-26NOV04CGYXYZ")).empty());
    EXPECT_TRUE(teams.teamKeysFor(kalshiMarket("KXRAIN-26NOV04")).empty());
}

TEST(TeamDirectoryTest, FindsNicknamesWithinTheSlugLeague)
{
    const TeamDirectory teams = TeamDirectory::parse(teamsJson);
    EXPECT_EQ(teams.teamKeysFor(polymarketMarket("nhl-cal-sea-2026-11-04", "Flames vs. Kraken")),
        (std::vector<std::string>{"nhl:CGY", "nhl:SEA"}));
    EXPECT_EQ(teams.teamKeysFor(polymarketMarket("nba-sac-por-2026-11-04", "Spread: Kings (-4.5)")), (std::vector<std::string>{"nba:SAC"}));
    EXPECT_EQ(teams.teamKeysFor(polymarketMarket("nba-sac-por-2026-11-04", "Blazers vs. Kings")),
        (std::vector<std::string>{"nba:POR", "nba:SAC"}));
    EXPECT_EQ(teams.teamKeysFor(polymarketMarket("nhl-tor-sj-2026-11-04", "Maple Leafs vs. Sharks")),
        (std::vector<std::string>{"nhl:SJ", "nhl:TOR"}));
    EXPECT_TRUE(teams.teamKeysFor(polymarketMarket("cfb-sacst-ida-2026-11-04", "Sacramento State Kings")).empty());
    EXPECT_TRUE(teams.teamKeysFor(polymarketMarket("nhl-x-y-2026-11-04", "Kingston vs. Seattleites")).empty());
}

TEST(TeamDirectoryTest, FindsTheSubjectTeamOfSpreadsAndTeamTotals)
{
    const TeamDirectory teams = TeamDirectory::parse(teamsJson);
    MarketInfo kalshiSpread = kalshiMarket("KXNHLSPREAD-26NOV04CGYSEA");
    kalshiSpread.identifier = "KXNHLSPREAD-26NOV04CGYSEA-CGY2";
    EXPECT_EQ(teams.subjectTeamKeyFor(kalshiSpread), "nhl:CGY");
    MarketInfo kalshiTotal = kalshiMarket("KXNHLTOTAL-26NOV04CGYSEA");
    kalshiTotal.identifier = "KXNHLTOTAL-26NOV04CGYSEA-6";
    EXPECT_EQ(teams.subjectTeamKeyFor(kalshiTotal), "");
    MarketInfo polymarketSpread = polymarketMarket("nhl-cal-sea-2026-11-04-spread-away-1pt5", "Spread: Flames (-1.5)");
    polymarketSpread.subtitle = "Flames (-1.5)";
    EXPECT_EQ(teams.subjectTeamKeyFor(polymarketSpread), "nhl:CGY");
    EXPECT_EQ(teams.subjectTeamKeyFor(polymarketMarket("nhl-cal-sea-2026-11-04", "Flames vs. Kraken")), "");
    MarketInfo polymarketPeriodSpread = polymarketMarket("nhl-cal-sea-2026-11-04-1p-spread", "1P Spread: Kraken (-0.5)");
    polymarketPeriodSpread.subtitle = "1P Spread -0.5";
    EXPECT_EQ(teams.subjectTeamKeyFor(polymarketPeriodSpread), "nhl:SEA");
}

TEST(TeamDirectoryTest, MissingFileMeansNoTeams)
{
    const TeamDirectory teams = TeamDirectory::load("/nonexistent/teams.json");
    EXPECT_EQ(teams.leagueCount(), 0U);
    std::vector<MarketInfo> markets{kalshiMarket("KXNHLGAME-26NOV04CGYSEA")};
    teams.annotate(markets);
    EXPECT_TRUE(markets[0].teamKeys.empty());
}

TEST(TeamDirectoryTest, ShippedTeamFileCoversTheMajorLeagues)
{
    const TeamDirectory teams = TeamDirectory::load(std::string(KPARB_FIXTURE_DIRECTORY) + "/../../config/teams.json");
    EXPECT_EQ(teams.leagueCount(), 4U);
    EXPECT_EQ(teams.teamKeysFor(kalshiMarket("KXNFLGAME-26NOV05JACBAL")), (std::vector<std::string>{"nfl:BAL", "nfl:JAC"}));
    EXPECT_EQ(teams.teamKeysFor(polymarketMarket("nfl-jax-bal-2026-11-06-player-props", "Jaguars vs. Ravens - Player Props")),
        (std::vector<std::string>{"nfl:BAL", "nfl:JAC"}));
}

}
}
