#include "TestConstants.h"
#include "arbitrage/ArbitrageEngine.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace kparb {
namespace {

using namespace test_constants::engine_test;
using namespace test_constants::prices;
using test_constants::fees::kalshiTakerFeeRate;
using test_constants::fees::linearFeeExponent;
using test_constants::fees::politicsTakerFeeRate;

class ArbitrageEngineTest : public ::testing::Test {
protected:
    void createEngine(std::uint32_t pairCount, EngineSettings settings = {}, FeeSchedule kalshiFees = FeeSchedule::zero(),
        FeeSchedule polymarketFees = FeeSchedule::zero())
    {
        channel = std::make_unique<LogChannel>(static_cast<std::uint16_t>(SourceIdentifier::Engine));
        engine = std::make_unique<ArbitrageEngine>(pairCount, kalshiFees, polymarketFees, settings, *channel);
    }

    void clearLadder(std::uint32_t pairIndex, LadderSlot slot, std::int64_t time = defaultUpdateNanos)
    {
        engine->applyUpdate(LadderUpdate{time, ladderIndexFor(pairIndex, slot), 0, 0, UpdateKind::ClearLadder, venueOf(slot)});
    }

    void setLevel(std::uint32_t pairIndex, LadderSlot slot, Ticks price, std::int32_t quantity, std::int64_t time = defaultUpdateNanos)
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
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, discountedKalshiAsk, largeQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fiftyCents, partialQuantity);

    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    const auto opportunity = opportunities[0].loadPayload<OpportunityRecord>();
    const int expectedEdge = oneDollar - discountedKalshiAsk - fiftyCents;
    EXPECT_EQ(opportunity.direction, TradeDirection::BuyKalshiYesBuyPolymarketNo);
    EXPECT_EQ(opportunity.kalshiBestPrice, discountedKalshiAsk);
    EXPECT_EQ(opportunity.polymarketBestPrice, fiftyCents);
    EXPECT_EQ(opportunity.topEdgeTicks, expectedEdge);
    EXPECT_EQ(opportunity.executableQuantity, static_cast<std::uint32_t>(partialQuantity));
    EXPECT_EQ(opportunity.expectedProfitTickQuantity, expectedEdge * partialQuantity);
    EXPECT_TRUE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_FALSE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiNoBuyPolymarketYes));
}

TEST_F(ArbitrageEngineTest, NoResultRiskRaisesTheEdgeRequiredOnTheExposedLeg)
{
    createEngine(1);
    ASSERT_TRUE(engine->assignNoResultRisk(firstPair, tokenWorthDollarOnNoResult, tokenWorthlessOnNoResult, tennisRiskBasisPoints));
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, riskyKalshiAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, riskyPolymarketAsk, standardQuantity);
    EXPECT_FALSE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    setLevel(firstPair, LadderSlot::KalshiBuyNo, riskyKalshiAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyYes, riskyPolymarketAsk, standardQuantity);
    EXPECT_TRUE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiNoBuyPolymarketYes));
    EXPECT_FALSE(engine->assignNoResultRisk(pairOutsideEngine, tokenWorthlessOnNoResult, tokenWorthlessOnNoResult, tennisRiskBasisPoints));
}

TEST_F(ArbitrageEngineTest, DetectsKalshiNoPolymarketYesOpportunity)
{
    createEngine(1);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::PolymarketBuyYes, underpricedPolymarketYesAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::KalshiBuyNo, overpricedKalshiNoAsk, standardQuantity);

    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    EXPECT_EQ(opportunities[0].loadPayload<OpportunityRecord>().direction, TradeDirection::BuyKalshiNoBuyPolymarketYes);
}

TEST_F(ArbitrageEngineTest, NoOpportunityWhenCostReachesOneDollar)
{
    createEngine(1);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fiftyCents, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fiftyCents, standardQuantity);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
}

TEST_F(ArbitrageEngineTest, FeesCanEliminateEdge)
{
    const FeeSchedule kalshiFees = FeeSchedule::quadratic(kalshiTakerFeeRate);
    createEngine(1, EngineSettings{}, kalshiFees);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, feeTestKalshiAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fiftyCents, standardQuantity);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(firstPair, LadderSlot::PolymarketBuyNo, feeTestPolymarketAsk, standardQuantity);
    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    EXPECT_EQ(opportunities[0].loadPayload<OpportunityRecord>().topEdgeTicks,
        oneDollar - feeTestKalshiAsk - feeTestPolymarketAsk - kalshiFees.feeAt(feeTestKalshiAsk));
}

TEST_F(ArbitrageEngineTest, PerPairPolymarketFeesApply)
{
    createEngine(twoPairs);
    ASSERT_TRUE(engine->assignPolymarketFeeSchedule(secondPair, FeeSchedule::curve(politicsTakerFeeRate, linearFeeExponent)));
    synchronizeAll(firstPair);
    synchronizeAll(secondPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, slightlyCheapKalshiAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fiftyCents, standardQuantity);
    setLevel(secondPair, LadderSlot::KalshiBuyYes, slightlyCheapKalshiAsk, standardQuantity);
    setLevel(secondPair, LadderSlot::PolymarketBuyNo, fiftyCents, standardQuantity);
    EXPECT_TRUE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_FALSE(engine->isOpportunityOpen(secondPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_FALSE(engine->assignPolymarketFeeSchedule(pairOutsideEngine, FeeSchedule::zero()));
}

TEST_F(ArbitrageEngineTest, UnsynchronizedLaddersIgnoreLevels)
{
    createEngine(1);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, unsynchronizedAsk, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, unsynchronizedAsk, standardQuantity);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
    EXPECT_EQ(engine->ladder(ladderIndexFor(firstPair, LadderSlot::KalshiBuyYes)).occupiedLevelCount(), 0U);
}

TEST_F(ArbitrageEngineTest, VenueResetClosesOpportunityAndRequiresNewSnapshot)
{
    createEngine(1);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity);
    ASSERT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);

    engine->applyUpdate(LadderUpdate{venueResetNanos, 0, 0, 0, UpdateKind::ResetVenue, Venue::Kalshi});
    EXPECT_FALSE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
    EXPECT_EQ(engine->counters().synchronizedLadderCount, laddersPerPair - kalshiLaddersPerPair);

    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity);
    EXPECT_FALSE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));

    clearLadder(firstPair, LadderSlot::KalshiBuyYes);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity);
    EXPECT_TRUE(engine->isOpportunityOpen(firstPair, TradeDirection::BuyKalshiYesBuyPolymarketNo));
}

TEST_F(ArbitrageEngineTest, StaleQuotesAreSuppressed)
{
    EngineSettings settings;
    settings.maximumQuoteAgeNanos = maximumQuoteAgeNanos;
    createEngine(1, settings);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity, earlyQuoteNanos);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity, lateQuoteNanos);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity, refreshedQuoteNanos);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);
}

TEST_F(ArbitrageEngineTest, DepthWalkStopsWhereEdgeRunsOut)
{
    EngineSettings settings;
    settings.minimumEdgeTicks = depthMinimumEdgeTicks;
    createEngine(1, settings);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, depthKalshiFirstAsk, depthKalshiFirstQuantity);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, depthKalshiSecondAsk, depthKalshiSecondQuantity);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, depthKalshiThirdAsk, depthKalshiThirdQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, depthPolymarketFirstAsk, depthPolymarketFirstQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, depthPolymarketSecondAsk, depthPolymarketSecondQuantity);
    drainRecords(RecordType::Opportunity);

    setLevel(firstPair, LadderSlot::PolymarketBuyNo, depthPolymarketFirstAsk, depthPolymarketFirstRefreshedQuantity);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());

    setLevel(firstPair, LadderSlot::KalshiBuyYes, depthKalshiSecondAsk, depthKalshiSecondRefreshedQuantity);
    const auto opportunities = drainRecords(RecordType::Opportunity);
    ASSERT_EQ(opportunities.size(), 1U);
    const auto opportunity = opportunities[0].loadPayload<OpportunityRecord>();
    const std::int32_t firstFill = depthKalshiFirstQuantity;
    const std::int32_t secondFill = depthPolymarketFirstRefreshedQuantity - firstFill;
    const std::int32_t thirdFill = depthKalshiSecondRefreshedQuantity - secondFill;
    const int firstEdge = oneDollar - depthKalshiFirstAsk - depthPolymarketFirstAsk;
    const int secondEdge = oneDollar - depthKalshiSecondAsk - depthPolymarketFirstAsk;
    const int thirdEdge = oneDollar - depthKalshiSecondAsk - depthPolymarketSecondAsk;
    EXPECT_EQ(opportunity.executableQuantity, static_cast<std::uint32_t>(firstFill + secondFill + thirdFill));
    EXPECT_EQ(opportunity.expectedProfitTickQuantity, firstFill * firstEdge + secondFill * secondEdge + thirdFill * thirdEdge);
    EXPECT_EQ(opportunity.kalshiWorstPrice, depthKalshiSecondAsk);
    EXPECT_EQ(opportunity.polymarketWorstPrice, depthPolymarketSecondAsk);
    EXPECT_EQ(opportunity.levelsConsumed, depthLevelsConsumed);
}

TEST_F(ArbitrageEngineTest, RepeatedIdenticalStateIsNotReEmitted)
{
    createEngine(1);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, expensiveAsk, smallQuantity);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, expensiveAsk, otherSmallQuantity);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);

    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, refreshedQuantity);
    EXPECT_EQ(drainRecords(RecordType::Opportunity).size(), 1U);
}

TEST_F(ArbitrageEngineTest, ClosingEmitsClosedRecord)
{
    createEngine(1);
    synchronizeAll(firstPair);
    setLevel(firstPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity, kalshiQuoteNanos);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity, openedNanos);
    setLevel(firstPair, LadderSlot::PolymarketBuyNo, fortyCents, removedQuantity, closedNanos);

    const auto closed = drainRecords(RecordType::OpportunityClosed);
    ASSERT_EQ(closed.size(), 1U);
    EXPECT_EQ(closed[0].loadPayload<OpportunityClosedRecord>().openDurationNanos, closedNanos - openedNanos);
    EXPECT_EQ(engine->counters().openOpportunityCount, 0U);
    EXPECT_EQ(engine->counters().opportunitiesOpened, 1U);
}

TEST_F(ArbitrageEngineTest, PairsAreIndependent)
{
    createEngine(threePairs);
    synchronizeAll(secondPair);
    synchronizeAll(thirdPair);
    setLevel(secondPair, LadderSlot::KalshiBuyYes, fortyCents, standardQuantity);
    setLevel(thirdPair, LadderSlot::PolymarketBuyNo, fortyCents, standardQuantity);
    EXPECT_TRUE(drainRecords(RecordType::Opportunity).empty());
}

TEST_F(ArbitrageEngineTest, DrainQueueAppliesBatchedUpdates)
{
    createEngine(twoPairs);
    auto queue = std::make_unique<LadderUpdateQueue>();
    for (std::uint32_t pairIndex = 0; pairIndex < twoPairs; ++pairIndex) {
        for (std::uint32_t slot = 0; slot < laddersPerPair; ++slot) {
            ASSERT_TRUE(queue->tryPush(LadderUpdate{firstQueuedNanos, pairIndex * laddersPerPair + slot, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi}));
        }
    }
    ASSERT_TRUE(queue->tryPush(LadderUpdate{secondQueuedNanos, ladderIndexFor(secondPair, LadderSlot::KalshiBuyNo), standardQuantity, queuedAsk,
        UpdateKind::SetLevel, Venue::Kalshi}));
    ASSERT_TRUE(queue->tryPush(LadderUpdate{thirdQueuedNanos, ladderIndexFor(secondPair, LadderSlot::PolymarketBuyYes), standardQuantity, queuedAsk,
        UpdateKind::SetLevel, Venue::Polymarket}));

    std::size_t total = 0;
    while (const std::size_t drained = engine->drainQueue<true>(*queue)) {
        total += drained;
    }
    EXPECT_EQ(total, twoPairs * laddersPerPair + queuedLevelUpdates);
    EXPECT_TRUE(engine->isOpportunityOpen(secondPair, TradeDirection::BuyKalshiNoBuyPolymarketYes));
}

}
}
