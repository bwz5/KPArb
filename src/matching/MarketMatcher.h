#pragma once

#include "ingestion/MarketInfo.h"
#include "logging/LogChannel.h"
#include "logging/RecordTypes.h"
#include "matching/CandidateFinder.h"
#include "matching/LanguageModelClient.h"
#include "matching/MatchStore.h"
#include "matching/MatchedPair.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

struct MatcherSettings {
    CandidateSettings candidates;
    double minimumConfidence = 0.85;
    std::size_t maximumLanguageModelRequests = 1500;
    std::size_t saveInterval = 20;
    std::size_t maximumRulesCharacters = 1500;
};

struct MatcherProgress {
    std::size_t candidatesTotal = 0;
    std::size_t candidatesProcessed = 0;
    std::size_t languageModelRequests = 0;
    std::size_t pairsAccepted = 0;
};

class MarketMatcher {
public:
    using ProgressCallback = std::function<void(const MatcherProgress&, const MarketInfo& kalshiMarket,
        const MarketInfo& polymarketMarket, const MatchVerdict& verdict)>;

    MarketMatcher(MatcherSettings matcherSettings, LanguageModelClient& languageModel, MatchStore& matchStore, LogChannel& eventChannel);

    std::vector<MatchedPair> match(const std::vector<MarketInfo>& kalshiMarkets, const std::vector<MarketInfo>& polymarketMarkets,
        const ProgressCallback& onProgress = {});

    std::vector<ChatMessage> buildMessages(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket) const;
    static std::optional<MatchVerdict> parseVerdict(std::string_view content);

private:
    void logEvent(MatchEventCode code, std::int64_t first = 0, std::int64_t second = 0, std::int64_t third = 0) noexcept;
    static MatchedPair makePair(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket, bool isInverted, double confidence);

    MatcherSettings settings;
    LanguageModelClient& model;
    MatchStore& store;
    LogChannel& channel;
};

}
