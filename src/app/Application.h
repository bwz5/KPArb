#pragma once

#include "app/ApplicationConfig.h"
#include "ingestion/MarketInfo.h"
#include "logging/LogChannel.h"
#include "matching/MatchedPair.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace kparb {

enum class RunMode : std::uint8_t {
    DiscoverMatchAndTrade,
    MatchOnly,
    TradeSavedPairs,
};

enum class StreamOutcome : std::uint8_t {
    SessionEnded,
    ShutdownRequested,
    AuthenticationFailed,
    Failed,
};

struct RunOptions {
    RunMode mode = RunMode::DiscoverMatchAndTrade;
    std::filesystem::path configPath = "config/config.json";
    std::int64_t durationSeconds = 0;
};

struct LogChannels {
    LogChannel& application;
    LogChannel& engine;
    LogChannel& kalshiFeed;
    LogChannel& polymarketFeed;
    LogChannel& matcher;
};

struct DiscoveredMarkets {
    std::vector<MarketInfo> kalshi;
    std::vector<MarketInfo> polymarket;
};

class Application {
public:
    Application(ApplicationConfig applicationConfig, RunOptions runOptions);

    int run(const std::atomic<bool>& shutdownRequested);

private:
    using Clock = std::chrono::steady_clock;

    int runDiscoveryCycles(LogChannels& channels, Clock::time_point overallDeadline, const std::atomic<bool>& shutdownRequested);
    int runSavedPairs(LogChannels& channels, Clock::time_point overallDeadline, const std::atomic<bool>& shutdownRequested);
    DiscoveredMarkets discoverMarkets(LogChannel& applicationChannel);
    StreamOutcome streamAndDetect(const std::vector<MatchedPair>& pairs, LogChannels& channels,
        const std::function<bool()>& shouldEndSession, const std::atomic<bool>& shutdownRequested);

    ApplicationConfig config;
    RunOptions options;
};

}
