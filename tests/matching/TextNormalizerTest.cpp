#include "matching/TextNormalizer.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(TextNormalizerTest, LowercasesAndDropsStopWords)
{
    const auto tokens = tokenizeForMatching("Will the Fed CUT rates in December?");
    const std::vector<std::string> expected{"federal", "cut", "rate", "december"};
    EXPECT_EQ(tokens, expected);
}

TEST(TextNormalizerTest, NormalizesNumbers)
{
    EXPECT_EQ(tokenizeForMatching("Bitcoin above $100,000"), (std::vector<std::string>{"bitcoin", "above", "100000"}));
    EXPECT_EQ(tokenizeForMatching("BTC over 100k"), (std::vector<std::string>{"bitcoin", "over", "100000"}));
    EXPECT_EQ(tokenizeForMatching("rate at 4.25%"), (std::vector<std::string>{"rate", "4.25"}));
    EXPECT_EQ(tokenizeForMatching("3.50 or 3.5"), (std::vector<std::string>{"3.5", "3.5"}));
}

TEST(TextNormalizerTest, ClassifiesTokens)
{
    EXPECT_TRUE(isNumericToken("4.25"));
    EXPECT_FALSE(isNumericToken("abc"));
    EXPECT_TRUE(isYearToken("2026"));
    EXPECT_FALSE(isYearToken("1999"));
    EXPECT_FALSE(isYearToken("100000"));
}

}
}
