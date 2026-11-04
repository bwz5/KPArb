#pragma once

#include "ingestion/MarketInfo.h"
#include "logging/LogChannel.h"
#include "logging/RecordTypes.h"
#include "matching/CandidateFinder.h"
#include "matching/LanguageModelClient.h"
#include "matching/MatchStore.h"
#include "matching/MatchedPair.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

struct MatcherSettings {
    CandidateSettings candidates;
    double minimumConfidence = 0.85;
    std::size_t maximumLanguageModelRequests = 20000;
    std::size_t concurrentRequests = 8;
    std::size_t saveInterval = 20;
    std::size_t maximumRulesCharacters = 800;
    std::size_t maximumConsecutiveFailures = 5;
    std::int64_t maximumQuotaWaitSeconds = 900;
};

struct MatcherProgress {
    std::size_t candidatesTotal = 0;
    std::size_t candidatesProcessed = 0;
    std::size_t languageModelRequests = 0;
    std::size_t pairsAccepted = 0;
    std::size_t quotaWaits = 0;
    std::size_t polarityCorrections = 0;
    bool isLanguageModelDisabled = false;
};

class MarketMatcher {
public:
    using ProgressCallback = std::function<void(const MatcherProgress&, const MarketInfo& kalshiMarket,
        const MarketInfo& polymarketMarket, const MatchVerdict& verdict)>;
    using PairCallback = std::function<void(const MatchedPair&)>;

    MarketMatcher(MatcherSettings matcherSettings, LanguageModelClient& languageModel, MatchStore& matchStore, LogChannel& eventChannel);

    std::vector<MatchedPair> match(const std::vector<MarketInfo>& kalshiMarkets, const std::vector<MarketInfo>& polymarketMarkets,
        const ProgressCallback& onProgress = {}, const PairCallback& onPairAccepted = {});

    void requestStop() noexcept;

    std::vector<ChatMessage> buildMessages(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket) const;
    static std::optional<MatchVerdict> parseVerdict(std::string_view content);

    const MatcherProgress& lastProgress() const noexcept;

private:
    struct MatchRun {
        const std::vector<MarketInfo>& kalshiMarkets;
        const std::vector<MarketInfo>& polymarketMarkets;
        const std::vector<CandidatePair>& candidates;
        const ProgressCallback& onProgress;
        const PairCallback& onPairAccepted;
        std::vector<MatchedPair> pairs;
        std::set<std::pair<std::string, std::string>> accepted;
        std::size_t unsavedVerdicts = 0;
        std::size_t consecutiveFailures = 0;
    };

    void askLanguageModel(MatchRun& run, const CandidatePair& candidate);
    void processPendingCandidates(MatchRun& run, const std::vector<std::size_t>& pending);
    void acceptIfMatched(MatchRun& run, const CandidatePair& candidate, const MatchVerdict& verdict);
    void reportProgress(MatchRun& run, const CandidatePair& candidate, const MatchVerdict& verdict);
    bool waitUnlessStopped(std::int64_t seconds);
    void logEvent(MatchEventCode code, std::int64_t first = 0, std::int64_t second = 0, std::int64_t third = 0) noexcept;
    static MatchedPair makePair(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket, bool isInverted, double confidence);

    MatcherSettings settings;
    LanguageModelClient& model;
    MatchStore& store;
    LogChannel& channel;
    MatcherProgress progress;
    std::mutex stateMutex;
    std::atomic<bool> stopRequested{false};
};

}
