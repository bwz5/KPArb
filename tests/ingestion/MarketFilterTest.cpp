#include "TestConstants.h"
#include "common/TimeParsing.h"
#include "ingestion/MarketFilter.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::calendar;
using namespace test_constants::market_filter_test;
using namespace test_constants::time_units;

MarketInfo marketClosingAt(std::string identifier, std::int64_t closeTime, std::int64_t openTime = alreadyOpenSeconds, bool isAcceptingOrders = true)
{
    MarketInfo market;
    market.identifier = std::move(identifier);
    market.closeTimeSeconds = closeTime;
    market.openTimeSeconds = openTime;
    market.isAcceptingOrders = isAcceptingOrders;
    return market;
}

TEST(MarketFilterTest, KeepsOnlyMarketsResolvingInsideWindow)
{
    const std::int64_t now = referenceNowSeconds;
    const auto window = resolutionWindowFrom(now, resolutionWindowDays);
    EXPECT_EQ(window.latestSeconds - window.earliestSeconds, resolutionWindowSeconds);
    const auto kept = keepOpenMarketsResolvingWithin(
        {
            marketClosingAt("past", now - secondsPerMinute),
            marketClosingAt("soon", now + secondsPerHour),
            marketClosingAt("edge", now + resolutionWindowSeconds),
            marketClosingAt("later", now + resolutionWindowSeconds + 1),
            marketClosingAt("unknown", unknownTimeSeconds),
        },
        window);
    ASSERT_EQ(kept.size(), keptMarketCount);
    EXPECT_EQ(kept[0].identifier, "soon");
    EXPECT_EQ(kept[1].identifier, "edge");
}

TEST(MarketFilterTest, DropsMarketsThatAreNotOpenForTradingYet)
{
    const std::int64_t now = referenceNowSeconds;
    const auto window = resolutionWindowFrom(now, resolutionWindowDays);
    const auto kept = keepOpenMarketsResolvingWithin(
        {
            marketClosingAt("open", now + secondsPerHour, now - secondsPerDay, true),
            marketClosingAt("opens-later", now + secondsPerHour, now + secondsPerMinute, true),
            marketClosingAt("paused", now + secondsPerHour, now - secondsPerDay, false),
            marketClosingAt("unknown-open-time", now + secondsPerHour, unknownTimeSeconds, true),
        },
        window);
    ASSERT_EQ(kept.size(), keptMarketCount);
    EXPECT_EQ(kept[0].identifier, "open");
    EXPECT_EQ(kept[1].identifier, "unknown-open-time");
}

TEST(MarketFilterTest, IsoFormattingRoundTrips)
{
    EXPECT_EQ(formatIsoTimestampSeconds(unixEpochSeconds), "1970-01-01T00:00:00Z");
    EXPECT_EQ(formatIsoTimestampSeconds(novemberFifth2024NoonUtcSeconds), "2024-11-05T12:00:00Z");
    EXPECT_EQ(formatIsoTimestampSeconds(leapDay2000MidnightUtcSeconds), "2000-02-29T00:00:00Z");
    for (std::int64_t seconds : {december2026EveningUtcSeconds, lastSecondOf2099UtcSeconds, leapDay2024MidnightUtcSeconds}) {
        EXPECT_EQ(parseIsoTimestampSeconds(formatIsoTimestampSeconds(seconds)), seconds);
    }
}

}
}
