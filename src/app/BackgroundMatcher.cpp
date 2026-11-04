#include "app/BackgroundMatcher.h"

#include "common/EnvironmentFile.h"
#include "matching/ChatCompletionsClient.h"

#include <iomanip>
#include <sstream>
#include <iostream>

namespace kparb {

namespace {

class UnavailableLanguageModel final : public LanguageModelClient {
public:
    LanguageModelResponse complete(const std::vector<ChatMessage>&) override
    {
        return {false, "", "No language model API key configured"};
    }

    std::string modelName() const override
    {
        return "unavailable";
    }
};

}

BackgroundMatcher::BackgroundMatcher(const ApplicationConfig& applicationConfig, LogChannel& matcherChannel)
    : config(applicationConfig)
    , channel(matcherChannel)
    , store(applicationConfig.matchCachePath, applicationConfig.manualMatchesPath)
    , matcherSettings(applicationConfig.matcher)
{
    store.load();
    if (auto apiKey = readEnvironmentVariable(config.languageModelApiKeyVariable)) {
        ChatCompletionsSettings modelSettings = config.languageModel;
        modelSettings.apiKey = *apiKey;
        languageModel = std::make_unique<ChatCompletionsClient>(modelSettings);
        hasLanguageModel = true;
    } else {
        languageModel = std::make_unique<UnavailableLanguageModel>();
        matcherSettings.maximumLanguageModelRequests = 0;
    }
    matcher = std::make_unique<MarketMatcher>(matcherSettings, *languageModel, store, channel);
}

BackgroundMatcher::~BackgroundMatcher()
{
    stop();
}

void BackgroundMatcher::start(std::vector<MarketInfo> kalshiMarkets, std::vector<MarketInfo> polymarketMarkets)
{
    kalshi = std::move(kalshiMarkets);
    polymarket = std::move(polymarketMarkets);
    std::cout << "cached verdicts: " << store.verdictCount() << '\n';
    if (hasLanguageModel) {
        std::cout << "language model: " << config.languageModel.model << " at " << config.languageModel.baseUrl << '\n';
    } else {
        std::cout << "warning: " << config.languageModelApiKeyVariable
                  << " is not set, so only cached verdicts and manual matches are used\n";
    }
    worker = std::thread([this] { run(); });
}

void BackgroundMatcher::stop()
{
    matcher->requestStop();
    waitUntilFinished();
}

void BackgroundMatcher::waitUntilFinished()
{
    if (worker.joinable()) {
        worker.join();
    }
}

bool BackgroundMatcher::isFinished() const noexcept
{
    return finished.load(std::memory_order_acquire);
}

std::size_t BackgroundMatcher::acceptedPairCount() const
{
    const std::lock_guard<std::mutex> lock(pairsMutex);
    return pairs.size();
}

std::vector<MatchedPair> BackgroundMatcher::acceptedPairs() const
{
    const std::lock_guard<std::mutex> lock(pairsMutex);
    return pairs;
}

bool BackgroundMatcher::usesLanguageModel() const noexcept
{
    return hasLanguageModel;
}

void BackgroundMatcher::run()
{
    const auto printProgress = [](const MatcherProgress& progress, const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket,
                                   const MatchVerdict& verdict) {
        if (progress.candidatesProcessed == 1) {
            std::cout << "candidate pairs: " << progress.candidatesTotal << '\n';
        }
        const bool isInteresting = verdict.isSameEvent || verdict.reason.rfind("error:", 0) == 0
            || verdict.reason.rfind("waiting", 0) == 0;
        if (isInteresting || progress.candidatesProcessed % 100 == 0) {
            std::ostringstream line;
            line << "  [" << progress.candidatesProcessed << '/' << progress.candidatesTotal << " llm=" << progress.languageModelRequests
                 << " accepted=" << progress.pairsAccepted << "] " << (verdict.isSameEvent ? "MATCH " : "") << kalshiMarket.identifier
                 << " <-> " << polymarketMarket.eventIdentifier << " conf=" << std::fixed << std::setprecision(2) << verdict.confidence
                 << (verdict.isInverted ? " inverted" : "") << ' ' << verdict.reason << '\n';
            std::cout << line.str();
        }
    };
    const auto recordPair = [this](const MatchedPair& pair) {
        const std::lock_guard<std::mutex> lock(pairsMutex);
        pairs.push_back(pair);
    };
    try {
        matcher->match(kalshi, polymarket, printProgress, recordPair);
        const MatcherProgress& summary = matcher->lastProgress();
        std::cout << "matching finished: candidates=" << summary.candidatesTotal << " processed=" << summary.candidatesProcessed
                  << " language model requests=" << summary.languageModelRequests << " accepted pairs=" << acceptedPairCount() << '\n';
        if (summary.isLanguageModelDisabled) {
            std::cout << "language model was disabled for this cycle after " << config.matcher.maximumConsecutiveFailures
                      << " consecutive failures; the next cycle will retry\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "matching failed: " << error.what() << '\n';
    }
    finished.store(true, std::memory_order_release);
}

}
