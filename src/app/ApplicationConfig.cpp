#include "app/ApplicationConfig.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kparb {

namespace {

template <typename Value>
void readInto(const nlohmann::json& object, const char* key, Value& destination)
{
    const auto found = object.find(key);
    if (found != object.end() && !found->is_null()) {
        destination = found->get<Value>();
    }
}

void readPathInto(const nlohmann::json& object, const char* key, std::filesystem::path& destination)
{
    const auto found = object.find(key);
    if (found != object.end() && found->is_string()) {
        destination = found->get<std::string>();
    }
}

nlohmann::json sectionOf(const nlohmann::json& document, const char* key)
{
    const auto found = document.find(key);
    return found != document.end() && found->is_object() ? *found : nlohmann::json::object();
}

}

ApplicationConfig parseApplicationConfig(const std::string& json)
{
    ApplicationConfig config;
    if (json.find_first_not_of(" \t\r\n") == std::string::npos) {
        return config;
    }
    const auto document = nlohmann::json::parse(json);

    const auto environment = sectionOf(document, "environment");
    readPathInto(environment, "file", config.environmentFile);
    readInto(environment, "kalshiKeyIdentifierVariable", config.kalshiKeyIdentifierVariable);
    readInto(environment, "kalshiPrivateKeyVariable", config.kalshiPrivateKeyVariable);
    readInto(environment, "kalshiPrivateKeyPathVariable", config.kalshiPrivateKeyPathVariable);
    readInto(environment, "languageModelApiKeyVariable", config.languageModelApiKeyVariable);

    const auto kalshi = sectionOf(document, "kalshi");
    readInto(kalshi, "restBaseUrl", config.kalshiCatalog.restBaseUrl);
    readInto(kalshi, "pageLimit", config.kalshiCatalog.pageLimit);
    readInto(kalshi, "maximumPages", config.kalshiCatalog.maximumPages);
    readInto(kalshi, "excludeMultivariateMarkets", config.kalshiCatalog.excludeMultivariateMarkets);
    readInto(kalshi, "webSocketUrl", config.kalshiFeed.connection.url);
    readInto(kalshi, "tickersPerSubscription", config.kalshiFeed.tickersPerSubscription);
    readInto(kalshi, "takerFeeRate", config.kalshiTakerFeeRate);

    const auto polymarket = sectionOf(document, "polymarket");
    readInto(polymarket, "gammaBaseUrl", config.polymarketCatalog.gammaBaseUrl);
    readInto(polymarket, "pageLimit", config.polymarketCatalog.pageLimit);
    readInto(polymarket, "maximumPages", config.polymarketCatalog.maximumPages);
    readInto(polymarket, "webSocketUrl", config.polymarketFeed.connection.url);
    readInto(polymarket, "tokensPerSubscription", config.polymarketFeed.tokensPerSubscription);
    readInto(polymarket, "textPingIntervalSeconds", config.polymarketFeed.connection.textPingIntervalSeconds);

    const auto languageModel = sectionOf(document, "languageModel");
    readInto(languageModel, "baseUrl", config.languageModel.baseUrl);
    readInto(languageModel, "model", config.languageModel.model);
    readInto(languageModel, "fallbackModels", config.languageModel.fallbackModels);
    readInto(languageModel, "requestsPerMinute", config.languageModel.requestsPerMinute);
    readInto(languageModel, "maximumAttempts", config.languageModel.maximumAttempts);
    readInto(languageModel, "requestJsonObject", config.languageModel.requestJsonObject);
    readInto(languageModel, "maximumOutputTokens", config.languageModel.maximumOutputTokens);
    readInto(languageModel, "reasoningEffort", config.languageModel.reasoningEffort);
    readInto(languageModel, "maximumRetryAfterSeconds", config.languageModel.maximumRetryAfterSeconds);

    const auto matching = sectionOf(document, "matching");
    readInto(matching, "candidatesPerMarket", config.matcher.candidates.candidatesPerMarket);
    readInto(matching, "candidatesPerPolymarketMarket", config.matcher.candidates.candidatesPerPolymarketMarket);
    readInto(matching, "requireCompatibleMarketTypes", config.matcher.candidates.requireCompatibleMarketTypes);
    readInto(matching, "requireMatchingStrikes", config.matcher.candidates.requireMatchingStrikes);
    readInto(matching, "concurrentRequests", config.matcher.concurrentRequests);
    readInto(matching, "minimumSimilarity", config.matcher.candidates.minimumSimilarity);
    std::int64_t maximumCloseTimeDifferenceDays = config.matcher.candidates.maximumCloseTimeDifferenceSeconds / 86400;
    readInto(matching, "maximumCloseTimeDifferenceDays", maximumCloseTimeDifferenceDays);
    config.matcher.candidates.maximumCloseTimeDifferenceSeconds = maximumCloseTimeDifferenceDays * 86400;
    readInto(matching, "requireCompatibleNumbers", config.matcher.candidates.requireCompatibleNumbers);
    readInto(matching, "minimumConfidence", config.matcher.minimumConfidence);
    readInto(matching, "maximumLanguageModelRequests", config.matcher.maximumLanguageModelRequests);
    readInto(matching, "maximumRulesCharacters", config.matcher.maximumRulesCharacters);
    readInto(matching, "maximumConsecutiveFailures", config.matcher.maximumConsecutiveFailures);
    readInto(matching, "maximumQuotaWaitSeconds", config.matcher.maximumQuotaWaitSeconds);
    readPathInto(matching, "cachePath", config.matchCachePath);
    readPathInto(matching, "manualMatchesPath", config.manualMatchesPath);
    readPathInto(matching, "matchedPairsPath", config.matchedPairsPath);
    readPathInto(matching, "entitiesPath", config.entitiesPath);
    readInto(matching, "sharedTeamBonus", config.matcher.candidates.sharedTeamBonus);
    readInto(matching, "excludedKalshiTickerPrefixes", config.matcher.excludedKalshiTickerPrefixes);

    const auto arbitrage = sectionOf(document, "arbitrage");
    readInto(arbitrage, "minimumEdgeTicks", config.engine.minimumEdgeTicks);
    std::int64_t maximumQuoteAgeMilliseconds = config.engine.maximumQuoteAgeNanos / 1'000'000;
    readInto(arbitrage, "maximumQuoteAgeMilliseconds", maximumQuoteAgeMilliseconds);
    config.engine.maximumQuoteAgeNanos = maximumQuoteAgeMilliseconds * 1'000'000;
    readInto(arbitrage, "maximumDepthLevels", config.engine.maximumDepthLevels);
    std::int64_t statsIntervalSeconds = config.engine.statsIntervalNanos / 1'000'000'000;
    readInto(arbitrage, "statsIntervalSeconds", statsIntervalSeconds);
    config.engine.statsIntervalNanos = statsIntervalSeconds * 1'000'000'000;
    readInto(arbitrage, "maximumPairs", config.maximumPairs);

    const auto discovery = sectionOf(document, "discovery");
    readInto(discovery, "maximumDaysUntilResolution", config.maximumDaysUntilResolution);
    readInto(discovery, "kalshiCloseTimeSearchDays", config.kalshiCloseTimeSearchDays);
    readInto(discovery, "polymarketMoneylineSearchDays", config.polymarketMoneylineSearchDays);
    readInto(discovery, "rematchIntervalMinutes", config.rematchIntervalMinutes);
    readInto(discovery, "pairsBeforeStreaming", config.pairsBeforeStreaming);
    readInto(discovery, "maximumMinutesBeforeStreaming", config.maximumMinutesBeforeStreaming);
    readInto(discovery, "pairsBetweenStreamRestarts", config.pairsBetweenStreamRestarts);

    const auto logging = sectionOf(document, "logging");
    readPathInto(logging, "directory", config.logDirectory);
    readInto(logging, "printFeedEvents", config.printFeedEvents);
    readInto(logging, "printEngineStats", config.printEngineStats);
    return config;
}

ApplicationConfig loadApplicationConfig(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return ApplicationConfig{};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseApplicationConfig(buffer.str());
}

}
