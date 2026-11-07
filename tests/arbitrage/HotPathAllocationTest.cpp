#include "TestConstants.h"
#include "AllocationCounter.h"
#include "arbitrage/ArbitrageEngine.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <vector>

namespace kparb {
namespace {

using namespace test_constants::hot_path_test;
using test_constants::fees::kalshiTakerFeeRate;

TEST(HotPathAllocationTest, EngineAppliesUpdatesWithoutAllocating)
{
    auto channel = std::make_unique<LogChannel>(static_cast<std::uint16_t>(SourceIdentifier::Engine));
    ArbitrageEngine engine(pairCount, FeeSchedule::quadratic(kalshiTakerFeeRate), FeeSchedule::zero(), EngineSettings{}, *channel);
    auto queue = std::make_unique<LadderUpdateQueue>();

    std::vector<LadderUpdate> updates;
    for (std::uint32_t ladderIndex = 0; ladderIndex < pairCount * laddersPerPair; ++ladderIndex) {
        updates.push_back(LadderUpdate{0, ladderIndex, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi});
    }
    std::mt19937 generator(randomSeed);
    std::uniform_int_distribution<std::uint32_t> ladderDistribution(0, pairCount * laddersPerPair - 1);
    std::uniform_int_distribution<int> priceDistribution(lowestRandomAsk, highestRandomAsk);
    std::uniform_int_distribution<std::int32_t> quantityDistribution(0, largestRandomQuantity);
    for (int index = 0; index < randomUpdateCount; ++index) {
        updates.push_back(LadderUpdate{index, ladderDistribution(generator), quantityDistribution(generator),
            static_cast<Ticks>(priceDistribution(generator)), UpdateKind::SetLevel, Venue::Polymarket});
    }
    updates.push_back(LadderUpdate{1, 0, 0, 0, UpdateKind::ResetVenue, Venue::Kalshi});

    const testing_support::AllocationScope scope;
    std::size_t pushed = 0;
    std::size_t applied = 0;
    LogRecord record{};
    while (applied < updates.size()) {
        while (pushed < updates.size() && queue->tryPush(updates[pushed])) {
            ++pushed;
        }
        applied += engine.drainQueue<true>(*queue);
        while (channel->records().tryPop(record)) {
        }
    }
    engine.writeStats(0, 0);
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
    EXPECT_GT(engine.counters().opportunitiesOpened, 0U);
}

TEST(HotPathAllocationTest, LogChannelWriteDoesNotAllocate)
{
    auto channel = std::make_unique<LogChannel>(static_cast<std::uint16_t>(SourceIdentifier::Engine));
    const testing_support::AllocationScope scope;
    for (int index = 0; index < logWriteCount; ++index) {
        EngineStatsRecord stats{};
        stats.updatesApplied = static_cast<std::uint64_t>(index);
        channel->write(stats);
    }
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
}

}
}
