#include "TestConstants.h"
#include "common/TimeParsing.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::calendar;

TEST(TimeParsingTest, ParsesUtcTimestamp)
{
    EXPECT_EQ(parseIsoTimestampSeconds("1970-01-01T00:00:00Z"), unixEpochSeconds);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00Z"), novemberFifth2024NoonUtcSeconds);
}

TEST(TimeParsingTest, ParsesFractionalSecondsAndOffsets)
{
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00.123Z"), novemberFifth2024NoonUtcSeconds);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T07:00:00-05:00"), novemberFifth2024NoonUtcSeconds);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00+00:00"), novemberFifth2024NoonUtcSeconds);
}

TEST(TimeParsingTest, ParsesDateOnly)
{
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05"), novemberFifth2024MidnightUtcSeconds);
}

TEST(TimeParsingTest, RejectsGarbage)
{
    EXPECT_FALSE(parseIsoTimestampSeconds("").has_value());
    EXPECT_FALSE(parseIsoTimestampSeconds("2024-13-01").has_value());
    EXPECT_FALSE(parseIsoTimestampSeconds("yesterday").has_value());
}

}
}
