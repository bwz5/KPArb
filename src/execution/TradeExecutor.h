#pragma once

#include "arbitrage/ExecutionSignal.h"
#include "execution/ExposureBook.h"
#include "execution/OrderTypes.h"
#include "execution/TradePlanner.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace kparb {

enum class ExecutionMode : std::uint8_t {
    Disabled,
    Paper,
    Live,
};

struct ExecutionSettings {
    ExecutionMode mode = ExecutionMode::Disabled;
    RiskSettings risk;
    PlannerSettings planner;
    std::int64_t maximumSignalAgeNanos = 500'000'000;
    std::filesystem::path journalPath = "data/executions.jsonl";
};

struct ExecutorCounters {
    std::uint64_t signalsReceived = 0;
    std::uint64_t staleSignals = 0;
    std::uint64_t blockedByRisk = 0;
    std::uint64_t tradesAttempted = 0;
    std::uint64_t fullyHedged = 0;
    std::uint64_t chased = 0;
    std::uint64_t unwound = 0;
    std::uint64_t leftUnhedged = 0;
};

struct ExecutionReport {
    std::uint32_t requestedContracts = 0;
    std::uint32_t firstFilled = 0;
    std::uint32_t secondFilled = 0;
    std::uint32_t chasedFilled = 0;
    std::uint32_t unwoundContracts = 0;
    std::uint32_t unhedgedContracts = 0;
    double kalshiDollars = 0.0;
    double polymarketDollars = 0.0;
    double lossDollars = 0.0;
    std::string firstError;
    std::string secondError;
};

class TradeExecutor {
public:
    TradeExecutor(ExecutionSettings executionSettings, std::vector<PairRoute> pairRoutes, VenueOrderClient& kalshiOrders,
        VenueOrderClient& polymarketOrders, ExposureBook& exposureBook);
    ~TradeExecutor();

    TradeExecutor(const TradeExecutor&) = delete;
    TradeExecutor& operator=(const TradeExecutor&) = delete;

    void start(ExecutionSignalQueue& signalQueue);
    void stop();

    void handle(const ExecutionSignal& signal, std::int64_t nowNanos);

    const ExecutorCounters& counters() const noexcept;
    const ExecutionReport& lastReport() const noexcept;

private:
    ExecutionReport execute(const ExecutionSignal& signal, const TradePlan& plan, std::uint32_t contracts);
    OrderResult place(const LegPlan& leg, bool isUnwind, Ticks limitPrice, std::uint32_t contracts, const std::string& tag);
    VenueOrderClient& clientFor(Venue venue) noexcept;
    void journal(const ExecutionSignal& signal, const ExecutionReport& report);

    ExecutionSettings settings;
    std::vector<PairRoute> routes;
    VenueOrderClient& kalshiClient;
    VenueOrderClient& polymarketClient;
    ExposureBook& exposure;
    std::vector<std::array<bool, 2>> tradedThisEpisode;
    ExecutorCounters executorCounters;
    ExecutionReport report;
    std::uint64_t orderSequence = 0;
    std::ofstream journalFile;
    std::atomic<bool> stopRequested{false};
    std::thread worker;
};

}
