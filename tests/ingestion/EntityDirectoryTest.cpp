#include "ingestion/EntityDirectory.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

const char* const entitiesJson = R"json({"leagues":[
    {"name":"nhl","kalshiSeriesPrefix":"KXNHL","polymarketSlugPrefix":"nhl-","entities":[
        {"code":"CGY","name":"Calgary Flames","aliases":["Calgary","Flames"]},
        {"code":"SEA","name":"Seattle Kraken","aliases":["Seattle","Kraken"]},
        {"code":"LA","name":"Los Angeles Kings","aliases":["Los Angeles","Kings"]},
        {"code":"NYI","name":"New York Islanders","aliases":["New York I","Islanders"]},
        {"code":"NYR","name":"New York Rangers","aliases":["New York R","Rangers"]},
        {"code":"STL","name":"St. Louis Blues","aliases":["St. Louis","Blues"]}]},
    {"name":"nba","kalshiSeriesPrefix":"KXNBA","polymarketSlugPrefix":"nba-","entities":[
        {"code":"SAC","name":"Sacramento Kings","aliases":["Sacramento","Kings"]},
        {"code":"LAL","name":"Los Angeles Lakers","aliases":["Los Angeles L","Lakers"]},
        {"code":"LAC","name":"Los Angeles Clippers","aliases":["Los Angeles C","Clippers"]}]},
    {"name":"sea","kalshiSeriesPrefix":"KXSERIEA","polymarketSlugPrefix":"sea-","entities":[
        {"code":"NAP","name":"SSC Napoli","aliases":["Napoli"]},
        {"code":"INT","name":"FC Internazionale Milano","aliases":["Inter"]},
        {"code":"H96","name":"Hellas 96","aliases":["Hellas"]}]},
    {"name":"kxserieaw","kalshiSeriesPrefix":"KXSERIEAW","polymarketSlugPrefix":"","entities":[]},
    {"name":"cbb","kalshiSeriesPrefix":"KXNCAAMB","polymarketSlugPrefix":"cbb-","entities":[
        {"code":"KU","name":"Kansas Jayhawks","aliases":["Kansas"]},
        {"code":"KSU","name":"Kansas State Wildcats","aliases":["Kansas St.","Kansas State"]},
        {"code":"MIA","name":"Miami Hurricanes","aliases":["Miami (FL)","Miami"]},
        {"code":"MOH","name":"Miami (OH) RedHawks","aliases":["Miami (OH)","Miami"]}]}
]})json";

MarketInfo kalshiMarket(std::string eventTicker, std::string ticker, std::string title)
{
    MarketInfo market;
    market.venue = Venue::Kalshi;
    market.eventIdentifier = std::move(eventTicker);
    market.identifier = std::move(ticker);
    market.title = std::move(title);
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

TEST(EntityDirectoryTest, BothVenuesEndUpWithTheSameEntityNames)
{
    const EntityDirectory entities = EntityDirectory::parse(entitiesJson);
    EXPECT_EQ(entities.entityCount(), 16U);
    MarketInfo kalshi = kalshiMarket("KXNHLGAME-26NOV04CGYSEA", "KXNHLGAME-26NOV04CGYSEA-CGY", "Calgary wins");
    kalshi.rulesSummary = "If Calgary wins the Calgary vs Seattle NHL game originally scheduled for Nov 4, 2026, then the market resolves to Yes.";
    kalshi.yesOutcomeName = "Calgary";
    MarketInfo polymarket = polymarketMarket("nhl-cal-sea-2026-11-04", "Flames vs. Kraken");
    polymarket.yesOutcomeName = "Flames";
    polymarket.noOutcomeName = "Kraken";
    entities.annotate(kalshi);
    entities.annotate(polymarket);
    EXPECT_EQ(kalshi.title, "Calgary Flames wins");
    EXPECT_EQ(kalshi.rulesSummary,
        "If Calgary Flames wins the Calgary Flames vs Seattle Kraken NHL game originally scheduled for Nov 4, 2026, then the market resolves to Yes.");
    EXPECT_EQ(kalshi.yesOutcomeName, "Calgary Flames");
    EXPECT_EQ(polymarket.title, "Calgary Flames vs. Seattle Kraken");
    EXPECT_EQ(polymarket.yesOutcomeName, "Calgary Flames");
    EXPECT_EQ(kalshi.teamKeys, (std::vector<std::string>{"nhl:CGY", "nhl:SEA"}));
    EXPECT_EQ(polymarket.teamKeys, kalshi.teamKeys);
    EXPECT_EQ(kalshi.entityLeague, "nhl");
}

TEST(EntityDirectoryTest, LongestAliasWinsAndCanonicalNamesAreNotRewrittenTwice)
{
    const EntityDirectory entities = EntityDirectory::parse(entitiesJson);
    const EntityLeague* nhl = entities.leagueOf(polymarketMarket("nhl-x", ""));
    const EntityLeague* nba = entities.leagueOf(polymarketMarket("nba-x", ""));
    const EntityLeague* cbb = entities.leagueOf(polymarketMarket("cbb-x", ""));
    ASSERT_NE(nhl, nullptr);
    ASSERT_NE(nba, nullptr);
    ASSERT_NE(cbb, nullptr);
    EXPECT_EQ(entities.canonicalize(*nhl, "Los Angeles Kings at Los Angeles").text, "Los Angeles Kings at Los Angeles Kings");
    EXPECT_EQ(entities.canonicalize(*nhl, "New York I vs New York R").text, "New York Islanders vs New York Rangers");
    EXPECT_EQ(entities.canonicalize(*nhl, "St. Louis Blues, St. Louis").text, "St. Louis Blues, St. Louis Blues");
    EXPECT_EQ(entities.canonicalize(*nba, "Los Angeles L vs Los Angeles C").text, "Los Angeles Lakers vs Los Angeles Clippers");
    EXPECT_EQ(entities.canonicalize(*nba, "Kings vs. Lakers").text, "Sacramento Kings vs. Los Angeles Lakers");
    EXPECT_EQ(entities.canonicalize(*cbb, "Kansas St. at Kansas").text, "Kansas State Wildcats at Kansas Jayhawks");
    EXPECT_EQ(entities.canonicalize(*cbb, "Miami (OH) at Miami (FL); Miami?").text, "Miami (OH) RedHawks at Miami Hurricanes; Miami?");
    EXPECT_EQ(entities.canonicalize(*nhl, "Kingston vs. Seattleites").text, "Kingston vs. Seattleites");
}

TEST(EntityDirectoryTest, LeaguesAreScopedByTheLongestPrefix)
{
    const EntityDirectory entities = EntityDirectory::parse(entitiesJson);
    MarketInfo women = kalshiMarket("KXSERIEAWGAME-26NOV08NAPINT", "KXSERIEAWGAME-26NOV08NAPINT-NAP", "Napoli wins");
    entities.annotate(women);
    EXPECT_EQ(women.title, "Napoli wins");
    EXPECT_EQ(women.entityLeague, "kxserieaw");
    EXPECT_TRUE(women.teamKeys.empty());
    MarketInfo men = kalshiMarket("KXSERIEAGAME-26NOV08NAPINT", "KXSERIEAGAME-26NOV08NAPINT-NAP", "Napoli wins");
    entities.annotate(men);
    EXPECT_EQ(men.title, "SSC Napoli wins");
    EXPECT_EQ(men.teamKeys, (std::vector<std::string>{"sea:INT", "sea:NAP"}));
    MarketInfo unknown = kalshiMarket("KXRAIN-26NOV04", "KXRAIN-26NOV04-T1", "Rain in Napoli");
    entities.annotate(unknown);
    EXPECT_EQ(unknown.title, "Rain in Napoli");
    EXPECT_TRUE(unknown.entityLeague.empty());
}

TEST(EntityDirectoryTest, FindsTheSubjectEntityOfSpreadsAndTeamTotals)
{
    const EntityDirectory entities = EntityDirectory::parse(entitiesJson);
    MarketInfo kalshiSpread = kalshiMarket("KXNHLSPREAD-26NOV04CGYSEA", "KXNHLSPREAD-26NOV04CGYSEA-CGY2", "Calgary wins by over 1.5 goals?");
    MarketInfo kalshiTotal = kalshiMarket("KXNHLTOTAL-26NOV04CGYSEA", "KXNHLTOTAL-26NOV04CGYSEA-6", "Calgary at Seattle: Total goals");
    MarketInfo digitCode = kalshiMarket("KXSERIEAGAME-26NOV08H96NAP", "KXSERIEAGAME-26NOV08H96NAP-H96", "Hellas wins");
    MarketInfo polymarketSpread = polymarketMarket("nhl-cal-sea-2026-11-04-spread-away-1pt5", "Spread: Flames (-1.5)");
    polymarketSpread.subtitle = "Flames (-1.5)";
    MarketInfo polymarketGame = polymarketMarket("nhl-cal-sea-2026-11-04", "Flames vs. Kraken");
    MarketInfo polymarketPeriodSpread = polymarketMarket("nhl-cal-sea-2026-11-04-1p-spread", "1P Spread: Kraken (-0.5)");
    polymarketPeriodSpread.subtitle = "1P Spread -0.5";
    for (MarketInfo* market : {&kalshiSpread, &kalshiTotal, &digitCode, &polymarketSpread, &polymarketGame, &polymarketPeriodSpread}) {
        entities.annotate(*market);
    }
    EXPECT_EQ(kalshiSpread.subjectTeamKey, "nhl:CGY");
    EXPECT_EQ(kalshiTotal.subjectTeamKey, "");
    EXPECT_EQ(digitCode.subjectTeamKey, "sea:H96");
    EXPECT_EQ(polymarketSpread.subjectTeamKey, "nhl:CGY");
    EXPECT_EQ(polymarketGame.subjectTeamKey, "");
    EXPECT_EQ(polymarketPeriodSpread.subjectTeamKey, "nhl:SEA");
}

TEST(EntityDirectoryTest, AmbiguousAliasesAreDropped)
{
    const EntityDirectory entities = EntityDirectory::parse(entitiesJson);
    MarketInfo market = polymarketMarket("cbb-mia-ku-2026-11-04", "Miami vs. Kansas");
    entities.annotate(market);
    EXPECT_EQ(market.title, "Miami vs. Kansas Jayhawks");
    EXPECT_EQ(market.teamKeys, (std::vector<std::string>{"cbb:KU"}));
}

TEST(EntityDirectoryTest, MissingFileMeansNoEntities)
{
    const EntityDirectory entities = EntityDirectory::load("/nonexistent/entities.json");
    EXPECT_EQ(entities.leagueCount(), 0U);
    std::vector<MarketInfo> markets{kalshiMarket("KXNHLGAME-26NOV04CGYSEA", "KXNHLGAME-26NOV04CGYSEA-CGY", "Calgary wins")};
    entities.annotate(markets);
    EXPECT_TRUE(markets[0].teamKeys.empty());
    EXPECT_EQ(markets[0].title, "Calgary wins");
}

TEST(EntityDirectoryTest, ShippedEntityFileLinksBothVenues)
{
    const EntityDirectory entities = EntityDirectory::load(std::string(KPARB_FIXTURE_DIRECTORY) + "/../../config/entities.json");
    EXPECT_GE(entities.leagueCount(), 4U);
    EXPECT_GE(entities.entityCount(), 126U);
    MarketInfo kalshi = kalshiMarket("KXNFLGAME-26NOV05JACBAL", "KXNFLGAME-26NOV05JACBAL-JAC", "Jacksonville at Baltimore Winner?");
    MarketInfo polymarket = polymarketMarket("nfl-jax-bal-2026-11-06-player-props", "Jaguars vs. Ravens - Player Props");
    entities.annotate(kalshi);
    entities.annotate(polymarket);
    EXPECT_EQ(kalshi.title, "Jacksonville Jaguars at Baltimore Ravens Winner?");
    EXPECT_EQ(polymarket.title, "Jacksonville Jaguars vs. Baltimore Ravens - Player Props");
    EXPECT_EQ(kalshi.teamKeys, (std::vector<std::string>{"nfl:BAL", "nfl:JAC"}));
    EXPECT_EQ(polymarket.teamKeys, kalshi.teamKeys);
    MarketInfo hockey = kalshiMarket("KXNHLGAME-26NOV04CGYSEA", "KXNHLGAME-26NOV04CGYSEA-CGY", "Calgary at Seattle Winner?");
    entities.annotate(hockey);
    EXPECT_EQ(hockey.title, "Calgary Flames at Seattle Kraken Winner?");
}

}
}
