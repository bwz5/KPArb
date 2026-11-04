#include "logging/LogRecord.h"
#include "logging/RecordFormatting.h"
#include "logging/RecordTypes.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(LogRecordLayoutTest, RecordFillsExactlyOneSixtyFourByteBlock)
{
    EXPECT_EQ(sizeof(LogRecord), 64U);
    EXPECT_EQ(alignof(LogRecord), 64U);
}

TEST(LogRecordLayoutTest, PayloadRoundTrips)
{
    OpportunityRecord original{};
    original.pairIndex = 7;
    original.kalshiBestPrice = 420;
    original.polymarketBestPrice = 550;
    original.executableQuantity = 1200;
    original.direction = TradeDirection::BuyKalshiNoBuyPolymarketYes;

    LogRecord record{};
    record.storePayload(original);
    const auto restored = record.loadPayload<OpportunityRecord>();
    EXPECT_EQ(restored.pairIndex, 7U);
    EXPECT_EQ(restored.kalshiBestPrice, 420);
    EXPECT_EQ(restored.polymarketBestPrice, 550);
    EXPECT_EQ(restored.executableQuantity, 1200U);
    EXPECT_EQ(restored.direction, TradeDirection::BuyKalshiNoBuyPolymarketYes);
}

TEST(LogRecordLayoutTest, PairNamesSurviveChunking)
{
    const std::string longIdentifier(95, 'x');
    const auto records = makePairNameRecords(3, PairNameField::PolymarketIdentifier, longIdentifier + "end", true);
    ASSERT_EQ(records.size(), 3U);
    PairNameDirectory directory;
    for (const auto& record : records) {
        directory.absorb(record);
    }
    for (const auto& record : makePairNameRecords(3, PairNameField::KalshiTicker, "KXFED-26DEC-T4.00", true)) {
        directory.absorb(record);
    }
    EXPECT_EQ(directory.polymarketIdentifier(3), longIdentifier + "end");
    EXPECT_EQ(directory.kalshiTicker(3), "KXFED-26DEC-T4.00");
    EXPECT_EQ(directory.kalshiTicker(9), "pair#9");
}

}
}
