#include "matching/MarketMatcher.h"

#include "common/Clock.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <ctime>
#include <set>
#include <unordered_map>

namespace kparb {

namespace {

std::string formatDate(std::int64_t epochSeconds)
{
    if (epochSeconds <= 0) {
        return "unknown";
    }
    const auto time = static_cast<std::time_t>(epochSeconds);
    std::tm utc{};
    gmtime_r(&time, &utc);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M UTC", &utc);
    return buffer;
}

std::string truncate(const std::string& text, std::size_t limit)
{
    return text.size() <= limit ? text : text.substr(0, limit) + "...";
}

}

MarketMatcher::MarketMatcher(MatcherSettings matcherSettings, LanguageModelClient& languageModel, MatchStore& matchStore,
    LogChannel& eventChannel)
    : settings(std::move(matcherSettings))
    , model(languageModel)
    , store(matchStore)
    , channel(eventChannel)
{
}

std::vector<ChatMessage> MarketMatcher::buildMessages(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket) const
{
    const std::string system =
        "You compare binary prediction markets listed on two different exchanges and decide whether they settle on exactly "
        "the same real-world outcome. You are strict: an arbitrage trade will be placed on your answer, so markets that "
        "could resolve differently must be rejected. Respond only with a JSON object.";

    std::string user;
    user += "Market A (Kalshi)\n";
    user += "Title: " + kalshiMarket.title + "\n";
    if (!kalshiMarket.subtitle.empty()) {
        user += "YES outcome: " + kalshiMarket.subtitle + "\n";
    }
    user += "Expected settlement: " + formatDate(kalshiMarket.closeTimeSeconds) + "\n";
    user += "Rules: " + truncate(kalshiMarket.rules, settings.maximumRulesCharacters) + "\n\n";

    user += "Market B (Polymarket)\n";
    user += "Question: " + polymarketMarket.title + "\n";
    if (!polymarketMarket.subtitle.empty()) {
        user += "Option: " + polymarketMarket.subtitle + "\n";
    }
    user += "First outcome: \"" + polymarketMarket.yesOutcomeName + "\"; second outcome: \"" + polymarketMarket.noOutcomeName + "\"\n";
    user += "End date: " + formatDate(polymarketMarket.closeTimeSeconds) + "\n";
    user += "Rules: " + truncate(polymarketMarket.rules, settings.maximumRulesCharacters) + "\n\n";

    user +=
        "Decide whether buying Kalshi YES together with one Polymarket outcome would be a perfect hedge.\n"
        "same_event is true only if both markets depend on the same underlying event, the same threshold or condition, "
        "and the same deadline or time window, so that Kalshi YES happens exactly when one specific Polymarket outcome "
        "happens. Different wording is fine. Different thresholds, dates, deadlines, candidates, teams, or resolution "
        "sources that could make the markets disagree are not.\n"
        "is_inverted is true if Kalshi YES corresponds to the Polymarket second outcome (\"" + polymarketMarket.noOutcomeName
        + "\") rather than the first outcome (\"" + polymarketMarket.yesOutcomeName + "\").\n"
        "confidence is your probability from 0 to 1 that same_event is correct.\n"
        "Return JSON exactly in this shape: {\"same_event\": true or false, \"is_inverted\": true or false, "
        "\"confidence\": number, \"reason\": \"one short sentence\"}";

    return {{"system", system}, {"user", user}};
}

std::optional<MatchVerdict> MarketMatcher::parseVerdict(std::string_view content)
{
    const std::size_t objectStart = content.find('{');
    const std::size_t objectEnd = content.rfind('}');
    if (objectStart == std::string_view::npos || objectEnd == std::string_view::npos || objectEnd < objectStart) {
        return std::nullopt;
    }
    const auto document = nlohmann::json::parse(content.substr(objectStart, objectEnd - objectStart + 1), nullptr, false);
    if (!document.is_object() || !document.contains("same_event")) {
        return std::nullopt;
    }
    const auto readBoolean = [&document](const char* key) {
        const auto found = document.find(key);
        if (found == document.end()) {
            return false;
        }
        if (found->is_boolean()) {
            return found->get<bool>();
        }
        if (found->is_string()) {
            return found->get<std::string>() == "true";
        }
        return false;
    };
    MatchVerdict verdict;
    verdict.isSameEvent = readBoolean("same_event");
    verdict.isInverted = readBoolean("is_inverted");
    const auto confidence = document.find("confidence");
    if (confidence != document.end() && confidence->is_number()) {
        verdict.confidence = confidence->get<double>();
    } else if (confidence != document.end() && confidence->is_string()) {
        verdict.confidence = std::atof(confidence->get<std::string>().c_str());
    }
    if (verdict.confidence > 1.0 && verdict.confidence <= 100.0) {
        verdict.confidence /= 100.0;
    }
    verdict.reason = document.value("reason", "");
    return verdict;
}

const MatcherProgress& MarketMatcher::lastProgress() const noexcept
{
    return progress;
}

void MarketMatcher::logEvent(MatchEventCode code, std::int64_t first, std::int64_t second, std::int64_t third) noexcept
{
    MatchEventRecord event{};
    event.firstArgument = first;
    event.secondArgument = second;
    event.thirdArgument = third;
    event.eventCode = code;
    channel.write(event);
}

MatchedPair MarketMatcher::makePair(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket, bool isInverted, double confidence)
{
    MatchedPair pair;
    pair.kalshiTicker = kalshiMarket.identifier;
    pair.kalshiTitle = matchingTextFor(kalshiMarket);
    pair.polymarketIdentifier = polymarketMarket.identifier;
    pair.polymarketSlug = polymarketMarket.eventIdentifier;
    pair.polymarketTitle = matchingTextFor(polymarketMarket);
    pair.polymarketYesToken = polymarketMarket.yesTokenIdentifier;
    pair.polymarketNoToken = polymarketMarket.noTokenIdentifier;
    pair.isInverted = isInverted;
    pair.confidence = confidence;
    pair.polymarketTakerFeeRate = polymarketMarket.takerFeeRate;
    pair.polymarketTakerFeeExponent = polymarketMarket.takerFeeExponent;
    return pair;
}

void MarketMatcher::requestStop() noexcept
{
    stopRequested.store(true, std::memory_order_release);
}

std::vector<MatchedPair> MarketMatcher::match(const std::vector<MarketInfo>& kalshiMarkets, const std::vector<MarketInfo>& polymarketMarkets,
    const ProgressCallback& onProgress, const PairCallback& onPairAccepted)
{
    const CandidateFinder finder(polymarketMarkets, settings.candidates);
    const std::vector<CandidatePair> candidates = finder.findCandidates(kalshiMarkets);
    logEvent(MatchEventCode::CandidatesFound, static_cast<std::int64_t>(candidates.size()));

    progress = MatcherProgress{};
    progress.candidatesTotal = candidates.size();
    std::vector<MatchedPair> pairs;
    std::set<std::pair<std::string, std::string>> accepted;
    std::size_t unsavedVerdicts = 0;
    std::size_t consecutiveFailures = 0;

    for (const CandidatePair& candidate : candidates) {
        if (stopRequested.load(std::memory_order_acquire)) {
            break;
        }
        const MarketInfo& kalshiMarket = kalshiMarkets[candidate.kalshiIndex];
        const MarketInfo& polymarketMarket = polymarketMarkets[candidate.polymarketIndex];
        ++progress.candidatesProcessed;
        if (store.isManuallyExcluded(kalshiMarket.identifier, polymarketMarket.identifier)) {
            continue;
        }

        std::optional<MatchVerdict> verdict = store.findVerdict(kalshiMarket.identifier, polymarketMarket.identifier);
        const auto similarityPermille = static_cast<std::int64_t>(candidate.similarity * 1000.0);
        if (verdict) {
            logEvent(MatchEventCode::VerdictFromCache, static_cast<std::int64_t>(candidate.kalshiIndex),
                static_cast<std::int64_t>(candidate.polymarketIndex), verdict->isSameEvent ? 1 : 0);
        } else {
            if (progress.isLanguageModelDisabled || progress.languageModelRequests >= settings.maximumLanguageModelRequests) {
                continue;
            }
            ++progress.languageModelRequests;
            const LanguageModelResponse response = model.complete(buildMessages(kalshiMarket, polymarketMarket));
            if (!response.succeeded) {
                logEvent(MatchEventCode::LanguageModelFailure, static_cast<std::int64_t>(candidate.kalshiIndex),
                    static_cast<std::int64_t>(candidate.polymarketIndex), similarityPermille);
                if (++consecutiveFailures >= settings.maximumConsecutiveFailures) {
                    progress.isLanguageModelDisabled = true;
                }
                MatchVerdict failed;
                failed.reason = "error: " + response.errorMessage;
                if (onProgress) {
                    onProgress(progress, kalshiMarket, polymarketMarket, failed);
                }
                continue;
            }
            consecutiveFailures = 0;
            verdict = parseVerdict(response.content);
            if (!verdict) {
                logEvent(MatchEventCode::LanguageModelFailure, static_cast<std::int64_t>(candidate.kalshiIndex),
                    static_cast<std::int64_t>(candidate.polymarketIndex), -1);
                continue;
            }
            verdict->model = model.modelName();
            verdict->decidedAtSeconds = wallClockSeconds();
            store.recordVerdict(kalshiMarket.identifier, polymarketMarket.identifier, *verdict);
            logEvent(MatchEventCode::VerdictFromLanguageModel, static_cast<std::int64_t>(candidate.kalshiIndex),
                static_cast<std::int64_t>(candidate.polymarketIndex), verdict->isSameEvent ? 1 : 0);
            if (++unsavedVerdicts >= settings.saveInterval) {
                store.save();
                unsavedVerdicts = 0;
            }
        }

        if (verdict->isSameEvent && verdict->confidence >= settings.minimumConfidence
            && accepted.emplace(kalshiMarket.identifier, polymarketMarket.identifier).second) {
            pairs.push_back(makePair(kalshiMarket, polymarketMarket, verdict->isInverted, verdict->confidence));
            ++progress.pairsAccepted;
            if (onPairAccepted) {
                onPairAccepted(pairs.back());
            }
            logEvent(MatchEventCode::PairAccepted, static_cast<std::int64_t>(pairs.size() - 1), similarityPermille,
                static_cast<std::int64_t>(verdict->confidence * 1000.0));
        }
        if (onProgress) {
            onProgress(progress, kalshiMarket, polymarketMarket, *verdict);
        }
    }
    if (unsavedVerdicts > 0) {
        store.save();
    }

    std::unordered_map<std::string, const MarketInfo*> kalshiByTicker;
    for (const MarketInfo& market : kalshiMarkets) {
        kalshiByTicker.emplace(market.identifier, &market);
    }
    std::unordered_map<std::string, const MarketInfo*> polymarketByIdentifier;
    for (const MarketInfo& market : polymarketMarkets) {
        polymarketByIdentifier.emplace(market.identifier, &market);
    }
    for (const ManualMatch& manual : store.manualInclusions()) {
        if (stopRequested.load(std::memory_order_acquire)) {
            break;
        }
        const auto kalshiMarket = kalshiByTicker.find(manual.kalshiTicker);
        const auto polymarketMarket = polymarketByIdentifier.find(manual.polymarketIdentifier);
        if (kalshiMarket == kalshiByTicker.end() || polymarketMarket == polymarketByIdentifier.end()) {
            continue;
        }
        if (accepted.emplace(manual.kalshiTicker, manual.polymarketIdentifier).second) {
            MatchedPair pair = makePair(*kalshiMarket->second, *polymarketMarket->second, manual.isInverted, 1.0);
            pair.isManual = true;
            pairs.push_back(std::move(pair));
            if (onPairAccepted) {
                onPairAccepted(pairs.back());
            }
            logEvent(MatchEventCode::PairAccepted, static_cast<std::int64_t>(pairs.size() - 1), -1, 1000);
        }
    }
    logEvent(MatchEventCode::MatchingFinished, static_cast<std::int64_t>(pairs.size()),
        static_cast<std::int64_t>(progress.languageModelRequests), static_cast<std::int64_t>(candidates.size()));
    return pairs;
}

}
