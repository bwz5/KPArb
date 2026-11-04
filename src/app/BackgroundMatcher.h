#pragma once

#include "app/ApplicationConfig.h"
#include "ingestion/MarketInfo.h"
#include "logging/LogChannel.h"
#include "matching/LanguageModelClient.h"
#include "matching/MarketMatcher.h"
#include "matching/MatchStore.h"
#include "matching/MatchedPair.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kparb {

class BackgroundMatcher {
public:
    BackgroundMatcher(const ApplicationConfig& applicationConfig, LogChannel& matcherChannel);
    ~BackgroundMatcher();

    BackgroundMatcher(const BackgroundMatcher&) = delete;
    BackgroundMatcher& operator=(const BackgroundMatcher&) = delete;

    void start(std::vector<MarketInfo> kalshiMarkets, std::vector<MarketInfo> polymarketMarkets);
    void stop();
    void waitUntilFinished();

    bool isFinished() const noexcept;
    std::size_t acceptedPairCount() const;
    std::vector<MatchedPair> acceptedPairs() const;
    bool usesLanguageModel() const noexcept;

private:
    void run();

    const ApplicationConfig& config;
    LogChannel& channel;
    MatchStore store;
    std::unique_ptr<LanguageModelClient> languageModel;
    MatcherSettings matcherSettings;
    std::unique_ptr<MarketMatcher> matcher;
    std::vector<MarketInfo> kalshi;
    std::vector<MarketInfo> polymarket;

    mutable std::mutex pairsMutex;
    std::vector<MatchedPair> pairs;
    std::atomic<bool> finished{false};
    bool hasLanguageModel = false;
    std::thread worker;
};

}
