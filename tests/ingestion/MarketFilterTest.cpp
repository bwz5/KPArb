#include "common/TimeParsing.h"
#include "ingestion/MarketFilter.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

MarketInfo marketClosingAt(std::string identifier, std::int64_t closeTime)
{
    MarketInfo market;
    market.identifier = std::move(identifier);
    market.closeTimeSeconds = closeTime;
    return market;
}

TEST(MarketFilterTest, KeepsOnlyMarketsResolvingInsideWindow)
{
    const std::int64_t now = 1'800'000'000;
    const auto window = resolutionWindowFrom(now, 2.0);
    EXPECT_EQ(window.latestSeconds - window.earliestSeconds, 2 * 86400);
    const auto kept = keepMarketsResolvingWithin(
        {
            marketClosingAt("past", now - 60),
            marketClosingAt("soon", now + 3600),
            marketClosingAt("edge", now + 2 * 86400),
            marketClosingAt("later", now + 2 * 86400 + 1),
            marketClosingAt("unknown", 0),
        },
        window);
    ASSERT_EQ(kept.size(), 2U);
    EXPECT_EQ(kept[0].identifier, "soon");
    EXPECT_EQ(kept[1].identifier, "edge");
}

TEST(MarketFilterTest, IsoFormattingRoundTrips)
{
    EXPECT_EQ(formatIsoTimestampSeconds(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(formatIsoTimestampSeconds(1730808000), "2024-11-05T12:00:00Z");
    EXPECT_EQ(formatIsoTimestampSeconds(951782400), "2000-02-29T00:00:00Z");
    for (std::int64_t seconds : {1796842800LL, 4102444799LL, 1709164800LL}) {
        EXPECT_EQ(parseIsoTimestampSeconds(formatIsoTimestampSeconds(seconds)), seconds);
    }
}

}
}
