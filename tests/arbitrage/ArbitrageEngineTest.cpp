#include "arbitrage/ArbitrageEngine.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace kparb {
namespace {

class ArbitrageEngineTest : public ::testing::Test {
protected:
    void createEngine(std::uint32_t pairCount, EngineSettings settings = {}, FeeSchedule kalshiFees = FeeSchedule::zero(),
        FeeSchedule polymarketFees = FeeSchedule::zero())
    {
        channel = std::make_unique<LogChannel>(static_cast<std::uint16_t>(SourceIdentifier::Engine));
        engine = std::make_unique<ArbitrageEngine>(pairCount, kalshiFees, polymarketFees, settings, *channel);
    }

    void clearLadder(std::uint32_t pairIndex, LadderSlot slot, std::int64_t time = 1)
    {
        engine->applyUpdate(LadderUpdate{time, ladderIndexFor(pairIndex, slot), 0, 0, UpdateKind::ClearLadder, venueOf(slot)});
    }

    void setLevel(std::uint32_t pairIndex, LadderSlot slot, Ticks price, std::int32_t quantity, std::int64_t time = 1)
    {
        engine->applyUpdate(LadderUpdate{time, ladderIndexFor(pairIndex, slot), quantity, price, UpdateKind::SetLevel, venueOf(slot)});
    }

    void synchronizeAll(std::uint32_t pairIndex)
    {
        clearLadder(pairIndex, LadderSlot::KalshiBuyYes);
        clearLadder(pairIndex, LadderSlot::KalshiBuyNo);
        clearLadder(pairIndex, LadderSlot::PolymarketBuyYes);
        clearLadder(pairIndex, LadderSlot::PolymarketBuyNo);
    }

    std::vector<LogRecord> drainRecords(RecordType type)
    {
        std::vector<LogRecord> matching;
        LogRecord record{};
        while (channel->records().tryPop(record)) {
            if (record.header.type == type) {
                matching.push_back(record);
            }
        }
        return matching;
    }

    static Venue venueOf(LadderSlot slot)
    {
        return slot == LadderSlot::KalshiBuyYes || slot == LadderSlot::KalshiBuyNo ? Venue::Kalshi : Venue::Polymarket;
    }

    std::unique_ptr<LogChannel> channel;
    std::unique_ptr<ArbitrageEngine> engine;
};

TEST_F(ArbitrageEngineTest, DetectsKalshiYesPolymarketNoOpportunity)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 450, 1000);
    setLevel(0, LadderSlot::PolymarketBuyNo, 500, 300);

    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    const auto opportunity = opportunities[0].loadPayload<OpportunityRecord>();
    EXPECT_EQ(opportunity.direction, TradeDirection::BuyKalshiYesBuyPolymarketNo);
    EXPECT_EQ(opportunity.kalshiBestPrice, 450);
    EXPECT_EQ(opportunity.polymarketBestPrice, 500);
    EXPECT_EQ(opportunity.topEdgeTicks, 50);
    EXPECT_EQ(opportunity.executableQuantity, 300U);
    EXPECT_EQ(opportunity.expectedProfitTickQuantity, 50 * 300);
    EXPECT_TRUE(engine->isOpportunityOpen(0, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_FALSE(engine->isOpportunityOpen(0, TradeDirection::BuyKalshiNoBuyPolymarketYes));
}

TEST_F(ArbitrageEngineTest, DetectsKalshiNoPolymarketYesOpportunity)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::PolymarketBuyYes, 300, 100);
    setLevel(0, LadderSlot::KalshiBuyNo, 650, 100);

    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    EXPECT_EQ(opportunities[0].loadPayload<OpportunityRecord>().direction, TradeDirection::BuyKalshiNoBuyPolymarketYes);
}

TEST_F(ArbitrageEngineTest, NoOpportunityWhenCostReachesOneDollar)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 500, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 500, 100);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
}

TEST_F(ArbitrageEngineTest, FeesCanEliminateEdge)
{
    createEngine(1, EngineSettings{}, FeeSchedule::quadratic(0.07));
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 490, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 500, 100);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(0, LadderSlot::PolymarketBuyNo, 470, 100);
    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    EXPECT_EQ(opportunities[0].loadPayload<OpportunityRecord>().topEdgeTicks, 1000 - 490 - 470 - 18);
}

TEST_F(ArbitrageEngineTest, UnsynchronizedLaddersIgnoreLevels)
{
    createEngine(1);
    setLevel(0, LadderSlot::KalshiBuyYes, 100, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 100, 100);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
    EXPECT_EQ(engine->ladder(ladderIndexFor(0, LadderSlot::KalshiBuyYes)).occupiedLevelCount(), 0U);
}

TEST_F(ArbitrageEngineTest, VenueResetClosesOpportunityAndRequiresNewSnapshot)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 100);
    ASSERT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);

    engine->applyUpdate(LadderUpdate{5, 0, 0, 0, UpdateKind::ResetVenue, Venue::Kalshi});
    EXPECT_FALSE(engine->isOpportunityOpen(0, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_EQ(engine->counters().synchronizedLadderCount, 2U);

    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100);
    EXPECT_FALSE(engine->isOpportunityOpen(0, TradeDirection::BuyKalshiYesBuyPolymarketNo));

    clearLadder(0, LadderSlot::KalshiBuyYes);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100);
    EXPECT_TRUE(engine->isOpportunityOpen(0, TradeDirection::BuyKalshiYesBuyPolymarketNo));
}

TEST_F(ArbitrageEngineTest, StaleQuotesAreSuppressed)
{
    EngineSettings settings;
    settings.maximumQuoteAgeNanos = 1000;
    createEngine(1, settings);
    synchronizeAll(0);
    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 100, 10);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100, 5000);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 100, 5100);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);
}

TEST_F(ArbitrageEngineTest, DepthWalkStopsWhereEdgeRunsOut)
{
    EngineSettings settings;
    settings.minimumEdgeTicks = 10;
    createEngine(1, settings);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100);
    setLevel(0, LadderSlot::KalshiBuyYes, 420, 200);
    setLevel(0, LadderSlot::KalshiBuyYes, 480, 500);
    setLevel(0, LadderSlot::PolymarketBuyNo, 500, 150);
    setLevel(0, LadderSlot::PolymarketBuyNo, 530, 1000);
    drainRecords(RecordType::Opportunity);

    setLevel(0, LadderSlot::PolymarketBuyNo, 500, 160);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(0, LadderSlot::KalshiBuyYes, 420, 250);
    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    const auto opportunity = opportunities[0].loadPayload<OpportunityRecord>();
    EXPECT_EQ(opportunity.executableQuantity, 100U + 60U + 190U);
    EXPECT_EQ(opportunity.expectedProfitTickQuantity, 100 * 100 + 60 * 80 + 190 * 50);
    EXPECT_EQ(opportunity.kalshiWorstPrice, 420);
    EXPECT_EQ(opportunity.polymarketWorstPrice, 530);
    EXPECT_EQ(opportunity.levelsConsumed, 3);
}

TEST_F(ArbitrageEngineTest, RepeatedIdenticalStateIsNotReEmitted)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 100);
    setLevel(0, LadderSlot::PolymarketBuyNo, 900, 5);
    setLevel(0, LadderSlot::PolymarketBuyNo, 900, 7);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);

    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 50);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);
}

TEST_F(ArbitrageEngineTest, ClosingEmitsClosedRecord)
{
    createEngine(1);
    synchronizeAll(0);
    setLevel(0, LadderSlot::KalshiBuyYes, 400, 100, 10);
    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 100, 20);
    setLevel(0, LadderSlot::PolymarketBuyNo, 400, 0, 70);

    const auto closed = drainRecords(RecordType::OpportunityClosed);
    ASSERT_EQ(closed.size(), 1U);
    EXPECT_EQ(closed[0].loadPayload<OpportunityClosedRecord>().openDurationNanos, 50);
    EXPECT_EQ(engine->counters().openOpportunityCount, 0U);
    EXPECT_EQ(engine->counters().opportunitiesOpened, 1U);
}

TEST_F(ArbitrageEngineTest, PairsAreIndependent)
{
    createEngine(3);
    synchronizeAll(1);
    synchronizeAll(2);
    setLevel(1, LadderSlot::KalshiBuyYes, 400, 100);
    setLevel(2, LadderSlot::PolymarketBuyNo, 400, 100);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
}

TEST_F(ArbitrageEngineTest, DrainQueueAppliesBatchedUpdates)
{
    createEngine(2);
    auto queue = std::make_unique<LadderUpdateQueue>();
    for (std::uint32_t pairIndex = 0; pairIndex < 2; ++pairIndex) {
        for (std::uint32_t slot = 0; slot < laddersPerPair; ++slot) {
            ASSERT_TRUE(queue->tryPush(LadderUpdate{1, pairIndex * laddersPerPair + slot, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi}));
        }
    }
    ASSERT_TRUE(queue->tryPush(LadderUpdate{2, ladderIndexFor(1, LadderSlot::KalshiBuyNo), 100, 300, UpdateKind::SetLevel, Venue::Kalshi}));
    ASSERT_TRUE(queue->tryPush(LadderUpdate{3, ladderIndexFor(1, LadderSlot::PolymarketBuyYes), 100, 300, UpdateKind::SetLevel, Venue::Polymarket}));

    std::size_t total = 0;
    while (const std::size_t drained = engine->drainQueue<true>(*queue)) {
        total += drained;
    }
    EXPECT_EQ(total, 10U);
    EXPECT_TRUE(engine->isOpportunityOpen(1, TradeDirection::BuyKalshiNoBuyPolymarketYes));
}

}
}
