#include "arbitrage/ArbitrageEngine.h"

#include "common/Clock.h"
#include "common/CpuRelax.h"

#include <limits>

namespace kparb {

namespace {

constexpr std::uint32_t kalshiSlotFor(TradeDirection direction) noexcept
{
    return direction == TradeDirection::BuyKalshiYesBuyPolymarketNo
        ? static_cast<std::uint32_t>(LadderSlot::KalshiBuyYes)
        : static_cast<std::uint32_t>(LadderSlot::KalshiBuyNo);
}

constexpr std::uint32_t polymarketSlotFor(TradeDirection direction) noexcept
{
    return direction == TradeDirection::BuyKalshiYesBuyPolymarketNo
        ? static_cast<std::uint32_t>(LadderSlot::PolymarketBuyNo)
        : static_cast<std::uint32_t>(LadderSlot::PolymarketBuyYes);
}

constexpr TradeDirection directionForSlot(std::uint32_t slot) noexcept
{
    return slot == static_cast<std::uint32_t>(LadderSlot::KalshiBuyYes)
            || slot == static_cast<std::uint32_t>(LadderSlot::PolymarketBuyNo)
        ? TradeDirection::BuyKalshiYesBuyPolymarketNo
        : TradeDirection::BuyKalshiNoBuyPolymarketYes;
}

constexpr Ticks prefetchPriceAhead(Ticks price) noexcept
{
    constexpr std::size_t levelsPerCacheLine = cacheLineSize / sizeof(Quantity);
    const std::size_t ahead = static_cast<std::size_t>(price) + levelsPerCacheLine;
    return static_cast<Ticks>(ahead > ticksPerDollar ? ticksPerDollar : ahead);
}

constexpr bool slotBelongsTo(std::uint32_t slot, Venue venue) noexcept
{
    const bool isKalshiSlot = slot == static_cast<std::uint32_t>(LadderSlot::KalshiBuyYes)
        || slot == static_cast<std::uint32_t>(LadderSlot::KalshiBuyNo);
    return isKalshiSlot == (venue == Venue::Kalshi);
}

}

ArbitrageEngine::ArbitrageEngine(std::uint32_t pairCount, FeeSchedule kalshiFeeSchedule, FeeSchedule polymarketFeeSchedule,
    EngineSettings engineSettings, LogChannel& outputChannel)
    : totalPairs(pairCount)
    , kalshiFees(kalshiFeeSchedule)
    , polymarketFeeSchedules{polymarketFeeSchedule}
    , settings(engineSettings)
    , logChannel(outputChannel)
    , ladders(static_cast<std::size_t>(pairCount) * laddersPerPair)
    , pairQuotes(pairCount)
    , opportunityStates(static_cast<std::size_t>(pairCount) * 2)
{
}

bool ArbitrageEngine::assignPolymarketFeeSchedule(std::uint32_t pairIndex, const FeeSchedule& schedule)
{
    if (pairIndex >= totalPairs) {
        return false;
    }
    std::size_t scheduleIndex = 0;
    while (scheduleIndex < polymarketFeeSchedules.size() && !(polymarketFeeSchedules[scheduleIndex] == schedule)) {
        ++scheduleIndex;
    }
    if (scheduleIndex == polymarketFeeSchedules.size()) {
        if (polymarketFeeSchedules.size() >= maximumPolymarketFeeSchedules) {
            return false;
        }
        polymarketFeeSchedules.push_back(schedule);
    }
    pairQuotes[pairIndex].polymarketFeeScheduleIndex = static_cast<std::uint8_t>(scheduleIndex);
    return true;
}

void ArbitrageEngine::applyUpdate(const LadderUpdate& update) noexcept
{
    if (update.kind == UpdateKind::ResetVenue) {
        resetVenue(update.venue, update.receivedAtNanos);
        return;
    }
    if (update.ladderIndex >= ladders.size() || update.price > ticksPerDollar) [[unlikely]] {
        return;
    }
    AskLadder& target = ladders[update.ladderIndex];
    switch (update.kind) {
    case UpdateKind::ClearLadder:
        target.clear();
        if (!target.isSynchronized) {
            target.isSynchronized = true;
            ++engineCounters.synchronizedLadderCount;
        }
        break;
    case UpdateKind::SetLevel:
        if (!target.isSynchronized) [[unlikely]] {
            return;
        }
        target.setLevel(update.price, update.quantity > 0 ? static_cast<Quantity>(update.quantity) : 0);
        break;
    case UpdateKind::AddToLevel:
        if (!target.isSynchronized) [[unlikely]] {
            return;
        }
        target.addToLevel(update.price, update.quantity);
        break;
    case UpdateKind::ResetVenue:
        return;
    }
    ++engineCounters.updatesApplied;
    refreshQuote(update.ladderIndex, update.receivedAtNanos);
    evaluateDirection(pairIndexOfLadder(update.ladderIndex), directionForSlot(slotOfLadder(update.ladderIndex)),
        update.receivedAtNanos);
}

void ArbitrageEngine::refreshQuote(std::uint32_t ladderIndex, std::int64_t receivedAtNanos) noexcept
{
    const AskLadder& source = ladders[ladderIndex];
    PairQuote& quote = pairQuotes[pairIndexOfLadder(ladderIndex)];
    const std::uint32_t slot = slotOfLadder(ladderIndex);
    quote.bestAskPrice[slot] = source.isSynchronized ? source.bestPrice() : noPrice;
    quote.lastUpdateNanos[slot] = receivedAtNanos;
}

void ArbitrageEngine::resetVenue(Venue venue, std::int64_t receivedAtNanos) noexcept
{
    for (std::uint32_t pairIndex = 0; pairIndex < totalPairs; ++pairIndex) {
        for (std::uint32_t slot = 0; slot < laddersPerPair; ++slot) {
            if (!slotBelongsTo(slot, venue)) {
                continue;
            }
            const std::uint32_t ladderIndex = pairIndex * laddersPerPair + slot;
            AskLadder& target = ladders[ladderIndex];
            target.clear();
            if (target.isSynchronized) {
                target.isSynchronized = false;
                --engineCounters.synchronizedLadderCount;
            }
            refreshQuote(ladderIndex, receivedAtNanos);
        }
        evaluateDirection(pairIndex, TradeDirection::BuyKalshiYesBuyPolymarketNo, receivedAtNanos);
        evaluateDirection(pairIndex, TradeDirection::BuyKalshiNoBuyPolymarketYes, receivedAtNanos);
    }
}

void ArbitrageEngine::evaluateDirection(std::uint32_t pairIndex, TradeDirection direction, std::int64_t nowNanos) noexcept
{
    ++engineCounters.pairEvaluations;
    PairQuote& quote = pairQuotes[pairIndex];
    const std::uint32_t kalshiSlot = kalshiSlotFor(direction);
    const std::uint32_t polymarketSlot = polymarketSlotFor(direction);
    const Ticks kalshiPrice = quote.bestAskPrice[kalshiSlot];
    const Ticks polymarketPrice = quote.bestAskPrice[polymarketSlot];
    const auto directionIndex = static_cast<std::size_t>(direction);
    const FeeSchedule& polymarketFees = polymarketFeeSchedules[quote.polymarketFeeScheduleIndex];

    bool isProfitable = kalshiPrice != noPrice && polymarketPrice != noPrice
        && edgeAt(kalshiPrice, polymarketPrice, polymarketFees) >= settings.minimumEdgeTicks;
    if (isProfitable && settings.maximumQuoteAgeNanos > 0) {
        const std::int64_t oldestAllowed = nowNanos - settings.maximumQuoteAgeNanos;
        isProfitable = quote.lastUpdateNanos[kalshiSlot] >= oldestAllowed
            && quote.lastUpdateNanos[polymarketSlot] >= oldestAllowed;
    }

    OpportunityState& state = opportunityStates[static_cast<std::size_t>(pairIndex) * 2 + directionIndex];
    if (!isProfitable) [[likely]] {
        if (quote.isOpportunityOpen[directionIndex]) {
            quote.isOpportunityOpen[directionIndex] = false;
            --engineCounters.openOpportunityCount;
            OpportunityClosedRecord closed{};
            closed.triggerReceivedAtNanos = nowNanos;
            closed.openDurationNanos = nowNanos - state.openedAtNanos;
            closed.pairIndex = pairIndex;
            closed.direction = direction;
            logChannel.write(closed);
            state = OpportunityState{};
        }
        return;
    }

    const std::uint32_t pairLadderBase = pairIndex * laddersPerPair;
    const DepthResult depth = measureDepth(ladders[pairLadderBase + kalshiSlot], ladders[pairLadderBase + polymarketSlot], polymarketFees);
    const auto executableQuantity = static_cast<std::uint32_t>(
        depth.executableQuantity > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max()
                                                                            : depth.executableQuantity);

    const bool wasOpen = quote.isOpportunityOpen[directionIndex];
    if (wasOpen && state.kalshiPrice == kalshiPrice && state.polymarketPrice == polymarketPrice
        && state.executableQuantity == executableQuantity) {
        return;
    }
    if (!wasOpen) {
        quote.isOpportunityOpen[directionIndex] = true;
        state.openedAtNanos = nowNanos;
        ++engineCounters.opportunitiesOpened;
        ++engineCounters.openOpportunityCount;
    }
    state.kalshiPrice = kalshiPrice;
    state.polymarketPrice = polymarketPrice;
    state.executableQuantity = executableQuantity;

    OpportunityRecord opportunity{};
    opportunity.triggerReceivedAtNanos = nowNanos;
    opportunity.expectedProfitTickQuantity = depth.expectedProfitTickQuantity;
    opportunity.pairIndex = pairIndex;
    opportunity.executableQuantity = executableQuantity;
    opportunity.kalshiBestPrice = kalshiPrice;
    opportunity.polymarketBestPrice = polymarketPrice;
    opportunity.topEdgeTicks = static_cast<std::int16_t>(edgeAt(kalshiPrice, polymarketPrice, polymarketFees));
    opportunity.kalshiWorstPrice = depth.kalshiWorstPrice;
    opportunity.polymarketWorstPrice = depth.polymarketWorstPrice;
    opportunity.direction = direction;
    opportunity.levelsConsumed = depth.levelsConsumed;
    logChannel.write(opportunity);
}

ArbitrageEngine::DepthResult ArbitrageEngine::measureDepth(const AskLadder& kalshiLadder, const AskLadder& polymarketLadder,
    const FeeSchedule& polymarketFees) const noexcept
{
    DepthResult result;
    Ticks kalshiPrice = kalshiLadder.bestPrice();
    Ticks polymarketPrice = polymarketLadder.bestPrice();
    Quantity kalshiRemaining = kalshiLadder.bestQuantity();
    Quantity polymarketRemaining = polymarketLadder.bestQuantity();
    std::uint32_t levels = 0;

    while (kalshiPrice != noPrice && polymarketPrice != noPrice && levels < settings.maximumDepthLevels) {
        const std::int32_t edge = edgeAt(kalshiPrice, polymarketPrice, polymarketFees);
        if (edge < settings.minimumEdgeTicks) {
            break;
        }
        const Quantity taken = kalshiRemaining < polymarketRemaining ? kalshiRemaining : polymarketRemaining;
        result.executableQuantity += taken;
        result.expectedProfitTickQuantity += static_cast<std::int64_t>(edge) * taken;
        result.kalshiWorstPrice = kalshiPrice;
        result.polymarketWorstPrice = polymarketPrice;
        kalshiRemaining -= taken;
        polymarketRemaining -= taken;
        ++levels;

        if (kalshiRemaining == 0) {
            kalshiPrice = kalshiLadder.nextOccupiedPriceAfter(kalshiPrice);
            if (kalshiPrice != noPrice) {
                prefetchForRead(kalshiLadder.addressOfLevel(prefetchPriceAhead(kalshiPrice)));
                kalshiRemaining = kalshiLadder.quantityAt(kalshiPrice);
            }
        }
        if (polymarketRemaining == 0) {
            polymarketPrice = polymarketLadder.nextOccupiedPriceAfter(polymarketPrice);
            if (polymarketPrice != noPrice) {
                prefetchForRead(polymarketLadder.addressOfLevel(prefetchPriceAhead(polymarketPrice)));
                polymarketRemaining = polymarketLadder.quantityAt(polymarketPrice);
            }
        }
    }
    result.levelsConsumed = static_cast<std::uint8_t>(levels > 255 ? 255 : levels);
    return result;
}

void ArbitrageEngine::writeStats(std::uint32_t kalshiQueueDepth, std::uint32_t polymarketQueueDepth) noexcept
{
    EngineStatsRecord stats{};
    stats.updatesApplied = engineCounters.updatesApplied;
    stats.pairEvaluations = engineCounters.pairEvaluations;
    stats.opportunitiesOpened = engineCounters.opportunitiesOpened;
    stats.droppedLogRecords = logChannel.droppedRecordCount();
    stats.kalshiQueueDepth = kalshiQueueDepth;
    stats.polymarketQueueDepth = polymarketQueueDepth;
    stats.openOpportunityCount = engineCounters.openOpportunityCount;
    stats.synchronizedLadderCount = engineCounters.synchronizedLadderCount;
    logChannel.write(stats);
}

void ArbitrageEngine::run(LadderUpdateQueue& kalshiQueue, LadderUpdateQueue& polymarketQueue, const std::atomic<bool>& stopRequested) noexcept
{
    std::uint64_t loopCount = 0;
    std::int64_t nextStatsAt = steadyNanoseconds() + settings.statsIntervalNanos;
    while (!stopRequested.load(std::memory_order_relaxed)) {
        const std::size_t drained = drainQueue<true>(kalshiQueue) + drainQueue<true>(polymarketQueue);
        if (drained == 0) {
            cpuRelax();
        }
        if ((++loopCount & 0x3FF) == 0) {
            const std::int64_t now = steadyNanoseconds();
            if (now >= nextStatsAt) {
                writeStats(static_cast<std::uint32_t>(kalshiQueue.approximateSize()),
                    static_cast<std::uint32_t>(polymarketQueue.approximateSize()));
                nextStatsAt = now + settings.statsIntervalNanos;
            }
        }
    }
    while (drainQueue<true>(kalshiQueue) + drainQueue<true>(polymarketQueue) > 0) {
    }
    writeStats(static_cast<std::uint32_t>(kalshiQueue.approximateSize()),
        static_cast<std::uint32_t>(polymarketQueue.approximateSize()));
}

}
