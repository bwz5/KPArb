#include "AllocationCounter.h"
#include "arbitrage/ArbitrageEngine.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <vector>

namespace kparb {
namespace {

TEST(HotPathAllocationTest, EngineAppliesUpdatesWithoutAllocating)
{
    constexpr std::uint32_t pairCount = 64;
    auto channel = std::make_unique<LogChannel>(1);
    ArbitrageEngine engine(pairCount, FeeSchedule::quadratic(0.07), FeeSchedule::zero(), EngineSettings{}, *channel);
    auto queue = std::make_unique<LadderUpdateQueue>();

    std::vector<LadderUpdate> updates;
    for (std::uint32_t ladderIndex = 0; ladderIndex < pairCount * laddersPerPair; ++ladderIndex) {
        updates.push_back(LadderUpdate{0, ladderIndex, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi});
    }
    std::mt19937 generator(7);
    std::uniform_int_distribution<std::uint32_t> ladderDistribution(0, pairCount * laddersPerPair - 1);
    std::uniform_int_distribution<int> priceDistribution(300, 700);
    std::uniform_int_distribution<std::int32_t> quantityDistribution(0, 1000);
    for (int index = 0; index < 20000; ++index) {
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
    auto channel = std::make_unique<LogChannel>(1);
    const testing_support::AllocationScope scope;
    for (int index = 0; index < 1000; ++index) {
        EngineStatsRecord stats{};
        stats.updatesApplied = static_cast<std::uint64_t>(index);
        channel->write(stats);
    }
    EXPECT_EQ(scope.allocationsSinceStart(), 0U);
}

}
}
