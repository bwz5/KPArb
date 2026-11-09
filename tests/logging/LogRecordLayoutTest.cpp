#include "TestConstants.h"
#include "logging/LogRecord.h"
#include "logging/RecordFormatting.h"
#include "logging/RecordTypes.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::log_record_test;

TEST(LogRecordLayoutTest, RecordFillsExactlyOneSixtyFourByteBlock)
{
    EXPECT_EQ(sizeof(LogRecord), recordBytes);
    EXPECT_EQ(alignof(LogRecord), recordBytes);
}

TEST(LogRecordLayoutTest, PayloadRoundTrips)
{
    OpportunityRecord original{};
    original.pairIndex = pairIndex;
    original.kalshiBestPrice = kalshiBestPrice;
    original.polymarketBestPrice = polymarketBestPrice;
    original.executableQuantity = executableQuantity;
    original.direction = TradeDirection::BuyKalshiNoBuyPolymarketYes;

    LogRecord record{};
    record.storePayload(original);
    const auto restored = record.loadPayload<OpportunityRecord>();
    EXPECT_EQ(restored.pairIndex, pairIndex);
    EXPECT_EQ(restored.kalshiBestPrice, kalshiBestPrice);
    EXPECT_EQ(restored.polymarketBestPrice, polymarketBestPrice);
    EXPECT_EQ(restored.executableQuantity, executableQuantity);
    EXPECT_EQ(restored.direction, TradeDirection::BuyKalshiNoBuyPolymarketYes);
}

TEST(LogRecordLayoutTest, PairNamesSurviveChunking)
{
    const std::string longIdentifier(longIdentifierLength, 'x');
    const auto records = makePairNameRecords(namedPairIndex, PairNameField::PolymarketIdentifier, longIdentifier + "end", true);
    ASSERT_EQ(records.size(), chunksForLongIdentifier);
    PairNameDirectory directory;
    for (const auto& record : records) {
        directory.absorb(record);
    }
    for (const auto& record : makePairNameRecords(namedPairIndex, PairNameField::KalshiTicker, "KXFED-26DEC-T4.00", true)) {
        directory.absorb(record);
    }
    EXPECT_EQ(directory.polymarketIdentifier(namedPairIndex), longIdentifier + "end");
    EXPECT_EQ(directory.kalshiTicker(namedPairIndex), "KXFED-26DEC-T4.00");
    EXPECT_EQ(directory.kalshiTicker(unnamedPairIndex), "pair#" + std::to_string(unnamedPairIndex));
    EXPECT_TRUE(directory.isInverted(namedPairIndex));
    EXPECT_FALSE(directory.isInverted(unnamedPairIndex));
}

}
}
