#include "TestConstants.h"
#include "common/DecimalParsing.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::decimal_test;

TEST(DecimalParsingTest, ParsesPricesIntoTicks)
{
    EXPECT_EQ(parseScaledDecimal("0.523", priceDecimalDigits, Rounding::Down), fiftyTwoPointThreeCentsTicks);
    EXPECT_EQ(parseScaledDecimal("0.5", priceDecimalDigits, Rounding::Down), fiftyCentsTicks);
    EXPECT_EQ(parseScaledDecimal("1", priceDecimalDigits, Rounding::Down), oneDollarTicks);
    EXPECT_EQ(parseScaledDecimal(".07", priceDecimalDigits, Rounding::Down), sevenCentsTicks);
}

TEST(DecimalParsingTest, RoundsExtraDigits)
{
    EXPECT_EQ(parseScaledDecimal("0.5231", priceDecimalDigits, Rounding::Down), fiftyTwoPointThreeCentsTicks);
    EXPECT_EQ(parseScaledDecimal("0.5231", priceDecimalDigits, Rounding::Up), fiftyTwoPointFourCentsTicks);
    EXPECT_EQ(parseScaledDecimal("0.5230", priceDecimalDigits, Rounding::Up), fiftyTwoPointThreeCentsTicks);
}

TEST(DecimalParsingTest, ParsesQuantities)
{
    EXPECT_EQ(parseScaledDecimal("1234.56", quantityDecimalDigits, Rounding::Down), twelveHundredThirtyFourPointFiveSixUnits);
    EXPECT_EQ(parseScaledDecimal("-12", quantityDecimalDigits, Rounding::Down), negativeTwelveUnits);
}

TEST(DecimalParsingTest, RejectsMalformedText)
{
    EXPECT_FALSE(parseScaledDecimal("", priceDecimalDigits, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal("abc", priceDecimalDigits, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal("0.5x", priceDecimalDigits, Rounding::Down).has_value());
    EXPECT_FALSE(parseScaledDecimal(".", priceDecimalDigits, Rounding::Down).has_value());
}

}
}
