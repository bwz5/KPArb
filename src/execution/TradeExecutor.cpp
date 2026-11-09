#include "execution/TradeExecutor.h"

#include "common/Clock.h"
#include "common/CpuRelax.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace kparb {

namespace {

constexpr int idleSpinsBeforeYield = 256;

const char* venueLabel(Venue venue) noexcept
{
    return venue == Venue::Kalshi ? "kalshi" : "polymarket";
}

double dollarsFor(std::uint32_t contracts, Ticks averagePrice) noexcept
{
    return averagePrice == noPrice ? 0.0 : static_cast<double>(contracts) * ticksToDollars(averagePrice);
}

}

TradeExecutor::TradeExecutor(ExecutionSettings executionSettings, std::vector<PairRoute> pairRoutes, VenueOrderClient& kalshiOrders,
    VenueOrderClient& polymarketOrders, ExposureBook& exposureBook)
    : settings(std::move(executionSettings))
    , routes(std::move(pairRoutes))
    , kalshiClient(kalshiOrders)
    , polymarketClient(polymarketOrders)
    , exposure(exposureBook)
    , tradedThisEpisode(routes.size())
{
    if (!settings.journalPath.empty()) {
        if (settings.journalPath.has_parent_path()) {
            std::filesystem::create_directories(settings.journalPath.parent_path());
        }
        journalFile.open(settings.journalPath, std::ios::app);
    }
}

TradeExecutor::~TradeExecutor()
{
    stop();
}

void TradeExecutor::start(ExecutionSignalQueue& signalQueue)
{
    stopRequested.store(false, std::memory_order_release);
    worker = std::thread([this, &signalQueue] {
        ExecutionSignal signal;
        int idleSpins = 0;
        while (!stopRequested.load(std::memory_order_acquire)) {
            if (signalQueue.tryPop(signal)) {
                handle(signal, steadyNanoseconds());
                idleSpins = 0;
                continue;
            }
            if (++idleSpins < idleSpinsBeforeYield) {
                cpuRelax();
            } else {
                std::this_thread::yield();
            }
        }
    });
}

void TradeExecutor::stop()
{
    stopRequested.store(true, std::memory_order_release);
    if (worker.joinable()) {
        worker.join();
    }
}

VenueOrderClient& TradeExecutor::clientFor(Venue venue) noexcept
{
    return venue == Venue::Kalshi ? kalshiClient : polymarketClient;
}

void TradeExecutor::handle(const ExecutionSignal& signal, std::int64_t nowNanos)
{
    ++executorCounters.signalsReceived;
    if (signal.pairIndex >= routes.size()) {
        return;
    }
    bool& traded = tradedThisEpisode[signal.pairIndex][static_cast<std::size_t>(signal.direction)];
    if (signal.kind == SignalKind::Closed) {
        traded = false;
        return;
    }
    if (traded) {
        return;
    }
    if (settings.maximumSignalAgeNanos > 0 && nowNanos - signal.detectedAtNanos > settings.maximumSignalAgeNanos) {
        ++executorCounters.staleSignals;
        return;
    }
    exposure.advanceTo(nowNanos);
    const auto plan = planTrade(signal, routes[signal.pairIndex], settings.planner);
    if (!plan) {
        return;
    }
    const std::uint32_t contracts = std::min(plan->contracts,
        exposure.isTradingAllowed() ? exposure.affordableContracts(signal.pairIndex, plan->kalshiDollarsPerContract, plan->polymarketDollarsPerContract) : 0U);
    if (contracts == 0) {
        ++executorCounters.blockedByRisk;
        return;
    }
    traded = true;
    ++executorCounters.tradesAttempted;
    report = execute(signal, *plan, contracts);
    exposure.recordSpend(signal.pairIndex, report.kalshiDollars, report.polymarketDollars);
    if (report.lossDollars > 0.0) {
        exposure.recordLoss(report.lossDollars);
    }
    if (report.unhedgedContracts > 0) {
        ++executorCounters.leftUnhedged;
        exposure.halt("unhedged position on pair " + std::to_string(signal.pairIndex) + ": " + std::to_string(report.unhedgedContracts)
            + " contracts on " + venueLabel(plan->first.venue));
    } else if (report.firstFilled > 0 && report.unwoundContracts == 0) {
        ++executorCounters.fullyHedged;
    }
    journal(signal, report);
}

OrderResult TradeExecutor::place(const LegPlan& leg, bool isUnwind, Ticks limitPrice, std::uint32_t contracts, const std::string& tag)
{
    OrderRequest request;
    request.venue = leg.venue;
    request.marketIdentifier = isUnwind ? leg.unwindMarketIdentifier : leg.marketIdentifier;
    request.side = isUnwind ? leg.unwindSide : leg.side;
    request.contracts = contracts;
    request.limitPrice = limitPrice;
    request.clientOrderIdentifier = "kparb-" + std::to_string(steadyNanoseconds()) + "-" + std::to_string(++orderSequence) + "-" + tag;
    if (contracts == 0 || limitPrice == noPrice) {
        OrderResult skipped;
        skipped.errorMessage = "no price to trade at";
        return skipped;
    }
    return clientFor(leg.venue).placeImmediateOrder(request);
}

ExecutionReport TradeExecutor::execute(const ExecutionSignal& signal, const TradePlan& plan, std::uint32_t contracts)
{
    static_cast<void>(signal);
    ExecutionReport result;
    result.requestedContracts = contracts;
    const auto addSpend = [&result](Venue venue, std::uint32_t filled, Ticks price) {
        (venue == Venue::Kalshi ? result.kalshiDollars : result.polymarketDollars) += dollarsFor(filled, price);
    };

    const OrderResult first = place(plan.first, false, plan.first.limitPrice, contracts, "first");
    result.firstFilled = first.filledContracts;
    result.firstError = first.errorMessage;
    addSpend(plan.first.venue, first.filledContracts, first.averagePrice);
    if (first.filledContracts == 0) {
        return result;
    }

    const OrderResult second = place(plan.second, false, plan.second.limitPrice, first.filledContracts, "second");
    result.secondFilled = second.filledContracts;
    result.secondError = second.errorMessage;
    addSpend(plan.second.venue, second.filledContracts, second.averagePrice);
    std::uint32_t residual = first.filledContracts - std::min(first.filledContracts, second.filledContracts);

    if (residual > 0) {
        ++executorCounters.chased;
        const OrderResult chase = place(plan.second, false, plan.second.chaseLimitPrice, residual, "chase");
        result.chasedFilled = std::min(residual, chase.filledContracts);
        addSpend(plan.second.venue, result.chasedFilled, chase.averagePrice);
        residual -= result.chasedFilled;
    }
    if (residual > 0) {
        ++executorCounters.unwound;
        const OrderResult unwind = place(plan.first, true, plan.first.unwindLimitPrice, residual, "unwind");
        result.unwoundContracts = std::min(residual, unwind.filledContracts);
        addSpend(plan.first.venue, result.unwoundContracts, unwind.averagePrice);
        if (result.unwoundContracts > 0 && first.averagePrice != noPrice && unwind.averagePrice != noPrice) {
            const std::int64_t lossTicks = static_cast<std::int64_t>(first.averagePrice) + unwind.averagePrice - ticksPerDollar;
            result.lossDollars += std::max<std::int64_t>(0, lossTicks) * static_cast<double>(result.unwoundContracts) / ticksPerDollar;
        }
        residual -= result.unwoundContracts;
    }
    result.unhedgedContracts = residual;
    return result;
}

void TradeExecutor::journal(const ExecutionSignal& signal, const ExecutionReport& executed)
{
    if (!journalFile) {
        return;
    }
    const PairRoute& route = routes[signal.pairIndex];
    nlohmann::json line{
        {"wall_time_ns", wallClockNanoseconds()},
        {"mode", settings.mode == ExecutionMode::Live ? "live" : "paper"},
        {"kalshi_ticker", route.kalshiTicker},
        {"direction", signal.direction == TradeDirection::BuyKalshiYesBuyPolymarketNo ? "kalshi_yes+polymarket_no" : "kalshi_no+polymarket_yes"},
        {"signal_kalshi_price", ticksToDollars(signal.kalshiPrice)},
        {"signal_polymarket_price", ticksToDollars(signal.polymarketPrice)},
        {"signal_edge", ticksToDollars(signal.edgeTicks)},
        {"requested", executed.requestedContracts},
        {"first_filled", executed.firstFilled},
        {"second_filled", executed.secondFilled},
        {"chased", executed.chasedFilled},
        {"unwound", executed.unwoundContracts},
        {"unhedged", executed.unhedgedContracts},
        {"kalshi_dollars", executed.kalshiDollars},
        {"polymarket_dollars", executed.polymarketDollars},
        {"unwind_loss_dollars", executed.lossDollars},
        {"first_error", executed.firstError},
        {"second_error", executed.secondError},
        {"halted", exposure.haltReason()},
    };
    journalFile << line.dump() << '\n';
    journalFile.flush();
}

const ExecutorCounters& TradeExecutor::counters() const noexcept
{
    return executorCounters;
}

const ExecutionReport& TradeExecutor::lastReport() const noexcept
{
    return report;
}

}
