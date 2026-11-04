#pragma once

#include "arbitrage/AskLadder.h"
#include "arbitrage/FeeSchedule.h"
#include "arbitrage/LadderUpdate.h"
#include "common/Prefetch.h"
#include "logging/LogChannel.h"
#include "logging/RecordTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace kparb {

struct EngineSettings {
    std::int32_t minimumEdgeTicks = 1;
    std::int64_t maximumQuoteAgeNanos = 0;
    std::uint32_t maximumDepthLevels = 32;
    std::int64_t statsIntervalNanos = 10'000'000'000;
};

struct EngineCounters {
    std::uint64_t updatesApplied = 0;
    std::uint64_t pairEvaluations = 0;
    std::uint64_t opportunitiesOpened = 0;
    std::uint32_t openOpportunityCount = 0;
    std::uint32_t synchronizedLadderCount = 0;
};

class ArbitrageEngine final {
public:
    static constexpr std::size_t updateBatchSize = 32;
    static constexpr std::size_t prefetchDistance = 4;

    ArbitrageEngine(std::uint32_t pairCount, FeeSchedule kalshiFeeSchedule, FeeSchedule polymarketFeeSchedule,
        EngineSettings engineSettings, LogChannel& outputChannel);

    void applyUpdate(const LadderUpdate& update) noexcept;

    template <bool usePrefetch>
    std::size_t drainQueue(LadderUpdateQueue& queue) noexcept
    {
        std::array<LadderUpdate, updateBatchSize> batch;
        const std::size_t count = queue.tryPopBatch(batch.data(), updateBatchSize);
        if constexpr (usePrefetch) {
            const std::size_t warmupCount = count < prefetchDistance ? count : prefetchDistance;
            for (std::size_t index = 0; index < warmupCount; ++index) {
                prefetchUpdateTargets(batch[index]);
            }
        }
        for (std::size_t index = 0; index < count; ++index) {
            if constexpr (usePrefetch) {
                if (index + prefetchDistance < count) {
                    prefetchUpdateTargets(batch[index + prefetchDistance]);
                }
            }
            applyUpdate(batch[index]);
        }
        return count;
    }

    template <bool usePrefetch>
    void applyBatch(const LadderUpdate* updates, std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < count; ++index) {
            if constexpr (usePrefetch) {
                if (index + prefetchDistance < count) {
                    prefetchUpdateTargets(updates[index + prefetchDistance]);
                }
            }
            applyUpdate(updates[index]);
        }
    }

    void run(LadderUpdateQueue& kalshiQueue, LadderUpdateQueue& polymarketQueue, const std::atomic<bool>& stopRequested) noexcept;

    void writeStats(std::uint32_t kalshiQueueDepth, std::uint32_t polymarketQueueDepth) noexcept;

    const AskLadder& ladder(std::uint32_t ladderIndex) const noexcept
    {
        return ladders[ladderIndex];
    }

    const EngineCounters& counters() const noexcept
    {
        return engineCounters;
    }

    std::uint32_t pairCount() const noexcept
    {
        return totalPairs;
    }

    bool isOpportunityOpen(std::uint32_t pairIndex, TradeDirection direction) const noexcept
    {
        return pairQuotes[pairIndex].isOpportunityOpen[static_cast<std::size_t>(direction)];
    }

private:
    struct alignas(64) PairQuote {
        std::array<std::int64_t, laddersPerPair> lastUpdateNanos{};
        std::array<Ticks, laddersPerPair> bestAskPrice{noPrice, noPrice, noPrice, noPrice};
        std::array<bool, 2> isOpportunityOpen{};
    };

    struct OpportunityState {
        std::int64_t openedAtNanos = 0;
        std::uint32_t executableQuantity = 0;
        Ticks kalshiPrice = noPrice;
        Ticks polymarketPrice = noPrice;
    };

    struct DepthResult {
        std::int64_t expectedProfitTickQuantity = 0;
        std::uint64_t executableQuantity = 0;
        Ticks kalshiWorstPrice = noPrice;
        Ticks polymarketWorstPrice = noPrice;
        std::uint8_t levelsConsumed = 0;
    };

    void prefetchUpdateTargets(const LadderUpdate& update) const noexcept
    {
        if (update.ladderIndex >= ladders.size() || update.price > ticksPerDollar) {
            return;
        }
        const AskLadder& target = ladders[update.ladderIndex];
        prefetchForWrite(&target);
        prefetchForWrite(target.addressOfOccupancyWord(update.price));
        prefetchForWrite(target.addressOfLevel(update.price));
        prefetchForWrite(&pairQuotes[pairIndexOfLadder(update.ladderIndex)]);
    }

    void resetVenue(Venue venue, std::int64_t receivedAtNanos) noexcept;
    void refreshQuote(std::uint32_t ladderIndex, std::int64_t receivedAtNanos) noexcept;
    void evaluateDirection(std::uint32_t pairIndex, TradeDirection direction, std::int64_t nowNanos) noexcept;
    DepthResult measureDepth(const AskLadder& kalshiLadder, const AskLadder& polymarketLadder) const noexcept;

    std::int32_t edgeAt(Ticks kalshiPrice, Ticks polymarketPrice) const noexcept
    {
        return static_cast<std::int32_t>(ticksPerDollar) - kalshiPrice - polymarketPrice
            - kalshiFees.feeAt(kalshiPrice) - polymarketFees.feeAt(polymarketPrice);
    }

    std::uint32_t totalPairs;
    FeeSchedule kalshiFees;
    FeeSchedule polymarketFees;
    EngineSettings settings;
    LogChannel& logChannel;
    EngineCounters engineCounters;
    std::vector<AskLadder> ladders;
    std::vector<PairQuote> pairQuotes;
    std::vector<OpportunityState> opportunityStates;
};

}
