#include "common/DecimalParsing.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(DecimalParsingTest, ParsesPricesIntoTicks)
{
    EXPECT_EQ(parseScaledDecimal("0.523", 3, Rounding::Down), 523);
    EXPECT_EQ(parseScaledDecimal("0.5", 3, Rounding::Down), 500);
    EXPECT_EQ(parseScaledDecimal("1", 3, Rounding::Down), 1000);
    EXPECT_EQ(parseScaledDecimal(".07", 3, Rounding::Down), 70);
}

TEST(DecimalParsingTest, RoundsExtraDigits)
{
    EXPECT_EQ(parseScaledDecimal("0.5231", 3, Rounding::Down), 523);
    EXPECT_EQ(parseScaledDecimal("0.5231", 3, Rounding::Up), 524);
    EXPECT_EQ(parseScaledDecimal("0.5230", 3, Rounding::Up), 523);
}

TEST(DecimalParsingTest, ParsesQuantities)
{
    EXPECT_EQ(parseScaledDecimal("1234.56", 2, Rounding::Down), 123456);
    EXPECT_EQ(parseScaledDecimal("-12", 2, Rounding::Down), -1200);
}

TEST(DecimalParsingTest, RejectsMalformedText)
{
    EXPECT_FALSE(parseScaledDecimal("", 3, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal("abc", 3, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal("0.5x", 3, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal(".", 3, Rounding::Down).has_value());
}

}
}
