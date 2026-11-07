#include "TestConstants.h"
#include "matching/EntityMatchRule.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace kparb {
namespace {

using namespace test_constants::entity_rule_test;
using namespace test_constants::time_units;

MarketInfo kalshiGame(std::string ticker, std::string subject)
{
    MarketInfo market;
    market.venue = Venue::Kalshi;
    market.identifier = std::move(ticker);
    market.eventIdentifier = market.identifier.substr(0, market.identifier.rfind('-'));
    market.teamKeys = {"cbb:ILL", "cbb:PENN"};
    market.subjectTeamKey = std::move(subject);
    return market;
}

MarketInfo polymarketGame(std::string slug)
{
    MarketInfo market;
    market.venue = Venue::Polymarket;
    market.identifier = "0xabc";
    market.eventIdentifier = std::move(slug);
    market.teamKeys = {"cbb:ILL", "cbb:PENN"};
    market.yesOutcomeEntityKey = "cbb:PENN";
    market.noOutcomeEntityKey = "cbb:ILL";
    return market;
}

TEST(EntityMatchRuleTest, ParsesVenueDates)
{
    EXPECT_EQ(kalshiTickerDay("KXNCAAMBGAME-26NOV05PENNILL-PENN"), polymarketSlugDay("cbb-penn-ill-2026-11-05"));
    EXPECT_EQ(*kalshiTickerDay("KXNBAGAME-26DEC01AAABBB-AAA") - *kalshiTickerDay("KXNBAGAME-26NOV30AAABBB-AAA"), oneDay);
    EXPECT_FALSE(kalshiTickerDay("KXNBAGAME-26XYZ01AAABBB-AAA"));
    EXPECT_FALSE(polymarketSlugDay("cbb-penn-ill-2026-11-05-spread-home-4pt5"));
}

TEST(EntityMatchRuleTest, FlagsPairsWhoseDatesDisagree)
{
    const MarketInfo kalshi = kalshiGame("KXCS2GAME-26NOV061130LPHDNK-DNK", "cbb:PENN");
    EXPECT_TRUE(haveConflictingDates(kalshi, polymarketGame("cs2-dnk2-lph-2026-10-16")));
    EXPECT_FALSE(haveConflictingDates(kalshi, polymarketGame("cs2-dnk2-lph-2026-11-06")));
    EXPECT_FALSE(haveConflictingDates(kalshi, polymarketGame("cs2-dnk2-lph-2026-11-07")));
    EXPECT_FALSE(haveConflictingDates(kalshi, polymarketGame("will-dnk-win-the-major")));
}

TEST(EntityMatchRuleTest, ConvertsEasternScheduleTimes)
{
    EXPECT_EQ(*kalshiTickerStartSeconds("KXCS2GAME-26NOV051000HONILL-ILL"),
        *polymarketSlugDay("x-2026-11-05") * secondsPerDay + kalshiFifteenHundredUtcHour * secondsPerHour);
    EXPECT_EQ(*kalshiTickerStartSeconds("KXKHLGAME-26JUL011130DINSPA-DIN"),
        *polymarketSlugDay("x-2026-07-01") * secondsPerDay + kalshiFifteenHundredUtcHour * secondsPerHour + halfHourSeconds);
    EXPECT_FALSE(kalshiTickerStartSeconds("KXNBAGAME-26NOV06UTADAL-DAL"));
    const std::int64_t novemberSixthOneAmUtc = *polymarketSlugDay("x-2026-11-06") * secondsPerDay + oneAmUtcHour * secondsPerHour;
    EXPECT_EQ(easternDay(novemberSixthOneAmUtc), *polymarketSlugDay("x-2026-11-05"));
}

TEST(EntityMatchRuleTest, RequiresTheSameScheduledStartWhenBothVenuesGiveOne)
{
    MarketInfo kalshi = kalshiGame("KXCS2GAME-26NOV051000HONILL-ILL", "cbb:PENN");
    MarketInfo polymarket = polymarketGame("cs2-hon1-ill4-2026-11-06");
    const std::int64_t novemberFifth = *polymarketSlugDay("x-2026-11-05") * secondsPerDay;
    const std::int64_t novemberSixth = novemberFifth + secondsPerDay;
    polymarket.scheduledStartSeconds = novemberSixth + noonUtcHour * secondsPerHour;
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, polymarket, false));
    polymarket.scheduledStartSeconds = novemberFifth + kalshiFifteenHundredUtcHour * secondsPerHour + halfHourSeconds;
    EXPECT_TRUE(isEntityResolvedMoneyline(kalshi, polymarket, false));

    MarketInfo untimedKalshi = kalshiGame("KXNFLGAME-26NOV05PENNILL-PENN", "cbb:PENN");
    MarketInfo nightGame = polymarketGame("nfl-penn-ill-2026-11-06");
    nightGame.scheduledStartSeconds = novemberSixth + oneAmUtcHour * secondsPerHour;
    EXPECT_TRUE(isEntityResolvedMoneyline(untimedKalshi, nightGame, false));
    nightGame.scheduledStartSeconds = novemberSixth + sixPmUtcHour * secondsPerHour;
    EXPECT_FALSE(isEntityResolvedMoneyline(untimedKalshi, nightGame, false));
}

TEST(EntityMatchRuleTest, AcceptsTwoWayWinnerMarketsNamingTheSameTeams)
{
    const MarketInfo polymarket = polymarketGame("cbb-penn-ill-2026-11-05");
    EXPECT_TRUE(isEntityResolvedMoneyline(kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-PENN", "cbb:PENN"), polymarket, false));
    EXPECT_TRUE(isEntityResolvedMoneyline(kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-ILL", "cbb:ILL"), polymarket, false));
    EXPECT_TRUE(isEntityResolvedMoneyline(kalshiGame("KXNCAAMBGAME-26NOV04PENNILL-ILL", "cbb:ILL"), polymarket, false));
    const MatchVerdict verdict = entityRuleVerdict(kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-ILL", "cbb:ILL"), polymarket);
    EXPECT_TRUE(verdict.isSameEvent);
    EXPECT_TRUE(verdict.isInverted);
    EXPECT_EQ(verdict.model, "entity-rule");
}

TEST(EntityMatchRuleTest, RejectsMarketsThatAreNotPlainWinners)
{
    const MarketInfo polymarket = polymarketGame("cbb-penn-ill-2026-11-05");
    const MarketInfo kalshi = kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-PENN", "cbb:PENN");
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, polymarket, true));
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshiGame("KXNCAAMBSPREAD-26NOV05PENNILL-PENN4", "cbb:PENN"), polymarket, false));
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, polymarketGame("cbb-penn-ill-2026-11-05-spread-home-4pt5"), false));
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, polymarketGame("cbb-penn-ill-2026-11-08"), false));
    MarketInfo otherOpponent = polymarketGame("cbb-penn-ill-2026-11-05");
    otherOpponent.teamKeys = {"cbb:DUKE", "cbb:PENN"};
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, otherOpponent, false));
    MarketInfo yesNo = polymarketGame("cbb-penn-ill-2026-11-05");
    yesNo.noOutcomeEntityKey.clear();
    EXPECT_FALSE(isEntityResolvedMoneyline(kalshi, yesNo, false));
}

TEST(EntityMatchRuleTest, SkipsKalshiMarketsWithMoreThanOneRuleMatch)
{
    const std::vector<MarketInfo> kalshi{
        kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-PENN", "cbb:PENN"),
        kalshiGame("KXNCAAMBGAME-26NOV05PENNILL-TIE", ""),
        kalshiGame("KXNCAAMBGAME-26NOV06PENNILL-PENN", "cbb:PENN"),
    };
    const std::vector<MarketInfo> polymarket{
        polymarketGame("cbb-penn-ill-2026-11-05"),
        polymarketGame("cbb-penn-ill-2026-11-06"),
    };
    const std::vector<CandidatePair> candidates{
        {firstKalshiMarket, sameDayPolymarketMarket, anyCandidateScore, sameCloseTime},
        {nextDayKalshiMarket, sameDayPolymarketMarket, anyCandidateScore, sameCloseTime},
        {nextDayKalshiMarket, nextDayPolymarketMarket, anyCandidateScore, sameCloseTime},
    };
    const std::vector<bool> matches = findEntityRuleMatches(kalshi, polymarket, candidates);
    EXPECT_TRUE(std::none_of(matches.begin(), matches.end(), [](bool isMatch) { return isMatch; }));
}

}
}
