#include "common/TimeParsing.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(TimeParsingTest, ParsesUtcTimestamp)
{
    EXPECT_EQ(parseIsoTimestampSeconds("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00Z"), 1730808000);
}

TEST(TimeParsingTest, ParsesFractionalSecondsAndOffsets)
{
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00.123Z"), 1730808000);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T07:00:00-05:00"), 1730808000);
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05T12:00:00+00:00"), 1730808000);
}

TEST(TimeParsingTest, ParsesDateOnly)
{
    EXPECT_EQ(parseIsoTimestampSeconds("2024-11-05"), 1730764800);
}

TEST(TimeParsingTest, RejectsGarbage)
{
    EXPECT_FALSE(parseIsoTimestampSeconds("").has_value());
    EXPECT_FALSE(parseIsoTimestampSeconds("2024-13-01").has_value());
    EXPECT_FALSE(parseIsoTimestampSeconds("yesterday").has_value());
}

}
}
