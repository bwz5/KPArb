#pragma once

#include "arbitrage/ArbitrageEngine.h"
#include "execution/TradeExecutor.h"
#include "ingestion/KalshiBookFeed.h"
#include "ingestion/KalshiMarketCatalog.h"
#include "ingestion/PolymarketBookFeed.h"
#include "ingestion/PolymarketMarketCatalog.h"
#include "matching/ChatCompletionsClient.h"
#include "matching/MarketMatcher.h"

#include <filesystem>
#include <string>

namespace kparb {

struct ApplicationConfig {
    std::filesystem::path environmentFile = ".env";
    std::string kalshiKeyIdentifierVariable = "KALSHI_API_KEY_ID";
    std::string kalshiPrivateKeyVariable = "KALSHI_PRIVATE_KEY";
    std::string kalshiPrivateKeyPathVariable = "KALSHI_PRIVATE_KEY_PATH";
    std::string languageModelApiKeyVariable = "KPARB_LLM_API_KEY";

    KalshiCatalogSettings kalshiCatalog;
    KalshiFeedSettings kalshiFeed;
    PolymarketCatalogSettings polymarketCatalog;
    PolymarketFeedSettings polymarketFeed;
    ChatCompletionsSettings languageModel;
    MatcherSettings matcher;
    std::filesystem::path matchCachePath = "data/match_cache.json";
    std::filesystem::path manualMatchesPath = "data/manual_matches.json";
    std::filesystem::path matchedPairsPath = "data/matched_pairs.json";
    std::filesystem::path entitiesPath = "config/entities.json";

    double maximumDaysUntilResolution = 2.0;
    std::int64_t kalshiCloseTimeSearchDays = 21;
    std::int64_t polymarketMoneylineSearchDays = 10;
    std::int64_t rematchIntervalMinutes = 60;
    std::size_t pairsBeforeStreaming = 500;
    std::int64_t maximumMinutesBeforeStreaming = 5;
    std::size_t pairsBetweenStreamRestarts = 25;
    std::int64_t minimumMinutesBetweenStreamRestarts = 10;

    EngineSettings engine;
    double kalshiTakerFeeRate = 0.07;
    std::size_t maximumPairs = 5000;

    ExecutionSettings execution;

    std::filesystem::path logDirectory = "data/logs";
    bool printFeedEvents = true;
    bool printEngineStats = true;
};

ApplicationConfig loadApplicationConfig(const std::filesystem::path& path);

ApplicationConfig parseApplicationConfig(const std::string& json);

}
