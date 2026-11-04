#include "app/Application.h"

#include "app/BackgroundMatcher.h"
#include "app/MatchedPairFile.h"
#include "arbitrage/ArbitrageEngine.h"
#include "common/Clock.h"
#include "common/EnvironmentFile.h"
#include "common/TimeParsing.h"
#include "ingestion/EntityDirectory.h"
#include "ingestion/HttpClient.h"
#include "ingestion/KalshiBookFeed.h"
#include "ingestion/KalshiMarketCatalog.h"
#include "ingestion/KalshiSigner.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/MarketFilter.h"
#include "ingestion/PolymarketBookFeed.h"
#include "ingestion/PolymarketMarketCatalog.h"
#include "logging/BinaryLogger.h"
#include "logging/RecordFormatting.h"
#include "logging/RecordTypes.h"
#include "matching/CandidateFinder.h"
#include "matching/MatchStore.h"

#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace kparb {

namespace {

std::string timestampForFileName()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

std::string readKalshiPrivateKey(const ApplicationConfig& config)
{
    if (auto inlineKey = readEnvironmentVariable(config.kalshiPrivateKeyVariable)) {
        return *inlineKey;
    }
    if (auto keyPath = readEnvironmentVariable(config.kalshiPrivateKeyPathVariable)) {
        std::ifstream file(*keyPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    return {};
}

void elevateCurrentThreadPriority()
{
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

void writeMatchEvent(LogChannel& channel, MatchEventCode code, std::int64_t first, std::int64_t second = 0)
{
    MatchEventRecord event{};
    event.eventCode = code;
    event.firstArgument = first;
    event.secondArgument = second;
    channel.write(event);
}

void writeWithRetry(LogChannel& channel, const PairNameRecord& record)
{
    while (!channel.write(record)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool waitUntil(const std::function<bool()>& condition, const std::atomic<bool>& shutdownRequested)
{
    while (!shutdownRequested.load(std::memory_order_acquire)) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

}

Application::Application(ApplicationConfig applicationConfig, RunOptions runOptions)
    : config(std::move(applicationConfig))
    , options(std::move(runOptions))
{
}

int Application::run(const std::atomic<bool>& shutdownRequested)
{
    loadEnvironmentFile(config.environmentFile);

    std::filesystem::create_directories(config.logDirectory);
    const auto logPath = config.logDirectory / ("kparb-" + timestampForFileName() + ".bin");
    BinaryLogger logger(logPath);
    LogChannels channels{
        logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::Application)),
        logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::Engine)),
        logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::KalshiFeed)),
        logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::PolymarketFeed)),
        logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::Matcher)),
    };

    auto pairNames = std::make_shared<PairNameDirectory>();
    const bool printFeedEvents = config.printFeedEvents;
    const bool printEngineStats = config.printEngineStats;
    logger.setRecordObserver([pairNames, printFeedEvents, printEngineStats](const LogRecord& record) {
        switch (record.header.type) {
        case RecordType::PairName:
            pairNames->absorb(record.loadPayload<PairNameRecord>());
            return;
        case RecordType::Opportunity:
        case RecordType::OpportunityClosed:
            std::cout << formatRecordText(record, *pairNames) << '\n';
            return;
        case RecordType::FeedEvent:
            if (printFeedEvents && record.loadPayload<FeedEventRecord>().eventCode != FeedEventCode::SnapshotReceived) {
                std::cout << formatRecordText(record, *pairNames) << '\n';
            }
            return;
        case RecordType::EngineStats:
            if (printEngineStats) {
                std::cout << formatRecordText(record, *pairNames) << '\n';
            }
            return;
        default:
            return;
        }
    });
    logger.start();
    std::cout << "binary log: " << logPath.string() << '\n';

    const auto overallDeadline = options.durationSeconds > 0 ? Clock::now() + std::chrono::seconds(options.durationSeconds)
                                                             : Clock::time_point::max();
    int exitCode = 0;
    try {
        switch (options.mode) {
        case RunMode::TradeSavedPairs:
            exitCode = runSavedPairs(channels, overallDeadline, shutdownRequested);
            break;
        case RunMode::CandidatesOnly:
            exitCode = runCandidatesOnly(channels);
            break;
        case RunMode::DiscoverMatchAndTrade:
        case RunMode::MatchOnly:
            exitCode = runDiscoveryCycles(channels, overallDeadline, shutdownRequested);
            break;
        }
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        exitCode = 1;
    }

    logger.stop();
    std::cout << "wrote " << logger.recordsWritten() << " log records, dropped " << logger.droppedRecordCount() << '\n';
    return exitCode;
}

int Application::runCandidatesOnly(LogChannels& channels)
{
    const DiscoveredMarkets markets = discoverMarkets(channels.application);
    MatchStore store(config.matchCachePath, config.manualMatchesPath);
    store.load();
    const CandidateFinder finder(markets.polymarket, config.matcher.candidates);
    const std::vector<CandidatePair> candidates = finder.findCandidates(markets.kalshi);

    std::filesystem::create_directories(options.candidatesPath.parent_path());
    std::ofstream file(options.candidatesPath);
    file << "score\tcached_verdict\tkalshi_ticker\tkalshi_text\tpolymarket_slug\tpolymarket_text\n";
    std::size_t cachedMatches = 0;
    std::size_t cachedRejections = 0;
    for (const CandidatePair& candidate : candidates) {
        const MarketInfo& kalshiMarket = markets.kalshi[candidate.kalshiIndex];
        const MarketInfo& polymarketMarket = markets.polymarket[candidate.polymarketIndex];
        const auto verdict = store.findVerdict(kalshiMarket.identifier, polymarketMarket.identifier);
        const char* verdictText = !verdict ? "unknown" : verdict->isSameEvent ? "match" : "different";
        cachedMatches += verdict && verdict->isSameEvent ? 1 : 0;
        cachedRejections += verdict && !verdict->isSameEvent ? 1 : 0;
        file << candidate.score << '\t' << verdictText << '\t' << kalshiMarket.identifier << '\t' << matchingTextFor(kalshiMarket) << '\t'
             << polymarketMarket.eventIdentifier << '\t' << matchingTextFor(polymarketMarket) << '\n';
    }
    std::cout << "candidate pairs: " << candidates.size() << " (cached matches " << cachedMatches << ", cached rejections "
              << cachedRejections << ", unknown " << candidates.size() - cachedMatches - cachedRejections << ")\n"
              << "wrote " << options.candidatesPath.string() << '\n';
    return 0;
}

int Application::runSavedPairs(LogChannels& channels, Clock::time_point overallDeadline, const std::atomic<bool>& shutdownRequested)
{
    auto pairs = loadMatchedPairs(config.matchedPairsPath);
    std::cout << "loaded " << pairs.size() << " saved pairs from " << config.matchedPairsPath.string() << '\n';
    if (pairs.size() > config.maximumPairs) {
        pairs.resize(config.maximumPairs);
    }
    if (pairs.empty()) {
        std::cout << "no saved pairs, nothing to stream\n";
        return 2;
    }
    const StreamOutcome outcome = streamAndDetect(
        pairs, channels, [overallDeadline] { return Clock::now() >= overallDeadline; }, shutdownRequested);
    if (outcome == StreamOutcome::AuthenticationFailed) {
        return 3;
    }
    return outcome == StreamOutcome::Failed ? 1 : 0;
}

int Application::runDiscoveryCycles(LogChannels& channels, Clock::time_point overallDeadline, const std::atomic<bool>& shutdownRequested)
{
    const bool isMatchOnly = options.mode == RunMode::MatchOnly;
    for (std::size_t cycle = 1; !shutdownRequested.load(std::memory_order_acquire) && Clock::now() < overallDeadline; ++cycle) {
        std::cout << "=== discovery cycle " << cycle << " ===\n";
        const auto cycleDeadline = config.rematchIntervalMinutes > 0
            ? std::min(overallDeadline, Clock::now() + std::chrono::minutes(config.rematchIntervalMinutes))
            : overallDeadline;

        std::optional<DiscoveredMarkets> markets = discoverMarketsUntilAvailable(channels.application, overallDeadline, shutdownRequested);
        if (!markets) {
            break;
        }
        BackgroundMatcher matcher(config, channels.matcher);
        matcher.start(std::move(markets->kalshi), std::move(markets->polymarket));

        if (isMatchOnly) {
            matcher.waitUntilFinished();
            saveMatchedPairs(config.matchedPairsPath, matcher.acceptedPairs());
            std::cout << "saved " << matcher.acceptedPairCount() << " matched pairs to " << config.matchedPairsPath.string() << '\n';
            return 0;
        }

        const std::size_t streamingThreshold = config.pairsBeforeStreaming;
        const auto streamingStartDeadline = std::min(cycleDeadline, Clock::now() + std::chrono::minutes(config.maximumMinutesBeforeStreaming));
        waitUntil(
            [&] {
                return matcher.acceptedPairCount() >= streamingThreshold || matcher.isFinished()
                    || Clock::now() >= streamingStartDeadline;
            },
            shutdownRequested);

        int exitCode = -1;
        while (exitCode < 0 && !shutdownRequested.load(std::memory_order_acquire) && Clock::now() < cycleDeadline) {
            auto pairs = matcher.acceptedPairs();
            if (pairs.size() > config.maximumPairs) {
                pairs.resize(config.maximumPairs);
            }
            saveMatchedPairs(config.matchedPairsPath, pairs);
            if (pairs.empty()) {
                if (matcher.isFinished()) {
                    std::cout << "no matched pairs this cycle, waiting for the next discovery cycle\n";
                    waitUntil([&] { return Clock::now() >= cycleDeadline; }, shutdownRequested);
                } else {
                    waitUntil([&] { return matcher.acceptedPairCount() > 0 || matcher.isFinished() || Clock::now() >= cycleDeadline; },
                        shutdownRequested);
                }
                continue;
            }

            const std::size_t streamedPairCount = pairs.size();
            const bool canGrow = streamedPairCount < config.maximumPairs;
            const auto shouldEndSession = [&] {
                if (Clock::now() >= cycleDeadline) {
                    return true;
                }
                if (!canGrow) {
                    return false;
                }
                const std::size_t accepted = matcher.acceptedPairCount();
                const bool finishedWithNewPairs = matcher.isFinished() && accepted > streamedPairCount;
                return finishedWithNewPairs || accepted >= streamedPairCount + config.pairsBetweenStreamRestarts;
            };
            std::cout << (matcher.isFinished() ? "streaming all " : "streaming while matching continues: ") << streamedPairCount
                      << " pairs\n";
            switch (streamAndDetect(pairs, channels, shouldEndSession, shutdownRequested)) {
            case StreamOutcome::AuthenticationFailed:
                exitCode = 3;
                break;
            case StreamOutcome::Failed:
                exitCode = 1;
                break;
            case StreamOutcome::ShutdownRequested:
                break;
            case StreamOutcome::SessionEnded:
                if (Clock::now() < cycleDeadline) {
                    std::cout << "restarting stream with " << matcher.acceptedPairCount() << " pairs\n";
                }
                break;
            }
        }
        matcher.stop();
        saveMatchedPairs(config.matchedPairsPath, matcher.acceptedPairs());
        if (exitCode >= 0) {
            return exitCode;
        }
        if (config.rematchIntervalMinutes <= 0) {
            break;
        }
    }
    return 0;
}

std::optional<DiscoveredMarkets> Application::discoverMarketsUntilAvailable(LogChannel& applicationChannel,
    Clock::time_point overallDeadline, const std::atomic<bool>& shutdownRequested)
{
    constexpr int discoveryRetrySeconds = 60;
    while (!shutdownRequested.load(std::memory_order_acquire) && Clock::now() < overallDeadline) {
        try {
            return discoverMarkets(applicationChannel);
        } catch (const std::exception& error) {
            std::cout << "discovery failed: " << error.what() << "\nretrying discovery in " << discoveryRetrySeconds << "s\n";
        }
        for (int second = 0; second < discoveryRetrySeconds && !shutdownRequested.load(std::memory_order_acquire); ++second) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return std::nullopt;
}

DiscoveredMarkets Application::discoverMarkets(LogChannel& applicationChannel)
{
    HttpClient httpClient;
    const std::int64_t nowSeconds = wallClockSeconds();
    const ResolutionWindow window = resolutionWindowFrom(nowSeconds, config.maximumDaysUntilResolution);
    std::cout << "resolution window: " << formatIsoTimestampSeconds(window.earliestSeconds) << " to "
              << formatIsoTimestampSeconds(window.latestSeconds) << '\n';

    KalshiCatalogSettings kalshiSettings = config.kalshiCatalog;
    kalshiSettings.minimumCloseTimestamp = window.earliestSeconds;
    kalshiSettings.maximumCloseTimestamp = std::max(window.latestSeconds, nowSeconds + config.kalshiCloseTimeSearchDays * 86400);
    std::cout << "fetching Kalshi markets...\n";
    KalshiMarketCatalog kalshiCatalog(kalshiSettings, httpClient);
    auto kalshiFetched = kalshiCatalog.fetchOpenMarkets([](std::size_t pages, std::size_t markets) {
        if (pages % 25 == 0) {
            std::cout << "  kalshi pages=" << pages << " markets=" << markets << '\n';
        }
    });
    const std::size_t kalshiFetchedCount = kalshiFetched.size();
    DiscoveredMarkets markets;
    markets.kalshi = keepOpenMarketsResolvingWithin(std::move(kalshiFetched), window);
    writeMatchEvent(applicationChannel, MatchEventCode::KalshiCatalogLoaded, static_cast<std::int64_t>(markets.kalshi.size()),
        static_cast<std::int64_t>(kalshiFetchedCount));
    std::cout << "kalshi open binary markets resolving in window: " << markets.kalshi.size() << " of " << kalshiFetchedCount << " fetched\n";

    PolymarketCatalogSettings polymarketSettings = config.polymarketCatalog;
    polymarketSettings.minimumEndTimestamp = window.earliestSeconds;
    polymarketSettings.maximumEndTimestamp = window.latestSeconds;
    const auto printPolymarketProgress = [](std::size_t pages, std::size_t markets) {
        if (pages % 25 == 0) {
            std::cout << "  polymarket pages=" << pages << " markets=" << markets << '\n';
        }
    };
    std::cout << "fetching Polymarket markets...\n";
    auto polymarketFetched = PolymarketMarketCatalog(polymarketSettings, httpClient).fetchOpenMarkets(printPolymarketProgress);

    PolymarketCatalogSettings moneylineSettings = polymarketSettings;
    moneylineSettings.maximumEndTimestamp = std::max(window.latestSeconds, nowSeconds + config.polymarketMoneylineSearchDays * 86400);
    moneylineSettings.sportsMarketType = "moneyline";
    std::cout << "fetching Polymarket moneylines that settle after the game...\n";
    auto moneylines = PolymarketMarketCatalog(moneylineSettings, httpClient).fetchOpenMarkets(printPolymarketProgress);
    std::unordered_set<std::string> knownIdentifiers;
    for (const MarketInfo& market : polymarketFetched) {
        knownIdentifiers.insert(market.identifier);
    }
    for (MarketInfo& market : moneylines) {
        if (knownIdentifiers.insert(market.identifier).second) {
            polymarketFetched.push_back(std::move(market));
        }
    }
    const EntityDirectory entities = EntityDirectory::load(config.entitiesPath);
    entities.annotate(markets.kalshi);
    entities.annotate(polymarketFetched);
    std::cout << "entities: " << entities.entityCount() << " across " << entities.leagueCount() << " leagues\n";
    const std::size_t polymarketFetchedCount = polymarketFetched.size();
    markets.polymarket = keepOpenMarketsResolvingWithin(std::move(polymarketFetched), window);
    writeMatchEvent(applicationChannel, MatchEventCode::PolymarketCatalogLoaded, static_cast<std::int64_t>(markets.polymarket.size()),
        static_cast<std::int64_t>(polymarketFetchedCount));
    std::cout << "polymarket open binary markets resolving in window: " << markets.polymarket.size() << " of " << polymarketFetchedCount
              << " fetched\n";
    return markets;
}

StreamOutcome Application::streamAndDetect(const std::vector<MatchedPair>& pairs, LogChannels& channels,
    const std::function<bool()>& shouldEndSession, const std::atomic<bool>& shutdownRequested)
{
    const auto keyIdentifier = readEnvironmentVariable(config.kalshiKeyIdentifierVariable);
    const std::string privateKeyPem = readKalshiPrivateKey(config);
    if (!keyIdentifier || privateKeyPem.empty()) {
        std::cerr << "Kalshi credentials missing: set " << config.kalshiKeyIdentifierVariable << " and "
                  << config.kalshiPrivateKeyVariable << " (or " << config.kalshiPrivateKeyPathVariable << ")\n";
        return StreamOutcome::Failed;
    }
    const KalshiSigner signer = KalshiSigner::fromPem(privateKeyPem);

    std::vector<PairSubscription> subscriptions;
    subscriptions.reserve(pairs.size());
    for (std::uint32_t pairIndex = 0; pairIndex < pairs.size(); ++pairIndex) {
        const MatchedPair& pair = pairs[pairIndex];
        subscriptions.push_back(PairSubscription{
            pair.kalshiTicker,
            pair.isInverted ? pair.polymarketNoToken : pair.polymarketYesToken,
            pair.isInverted ? pair.polymarketYesToken : pair.polymarketNoToken,
        });
        const std::string polymarketName = pair.polymarketSlug.empty() ? pair.polymarketIdentifier : pair.polymarketSlug;
        for (const auto& record : makePairNameRecords(pairIndex, PairNameField::KalshiTicker, pair.kalshiTicker, pair.isInverted)) {
            writeWithRetry(channels.application, record);
        }
        for (const auto& record : makePairNameRecords(pairIndex, PairNameField::PolymarketIdentifier, polymarketName, pair.isInverted)) {
            writeWithRetry(channels.application, record);
        }
    }
    const LadderDirectory directory(subscriptions);

    const auto pairCount = static_cast<std::uint32_t>(pairs.size());
    auto engine = std::make_unique<ArbitrageEngine>(pairCount, FeeSchedule::quadratic(config.kalshiTakerFeeRate), FeeSchedule::zero(),
        config.engine, channels.engine);
    std::size_t pairsWithPolymarketFees = 0;
    for (std::uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
        const MatchedPair& pair = pairs[pairIndex];
        if (pair.polymarketTakerFeeRate > 0.0) {
            if (!engine->assignPolymarketFeeSchedule(pairIndex, FeeSchedule::curve(pair.polymarketTakerFeeRate, pair.polymarketTakerFeeExponent))) {
                std::cerr << "too many distinct Polymarket fee schedules\n";
                return StreamOutcome::Failed;
            }
            ++pairsWithPolymarketFees;
        }
    }

    auto kalshiQueue = std::make_unique<LadderUpdateQueue>();
    auto polymarketQueue = std::make_unique<LadderUpdateQueue>();

    KalshiFeedSettings kalshiSettings = config.kalshiFeed;
    kalshiSettings.apiKeyIdentifier = *keyIdentifier;
    KalshiBookFeed kalshiFeed(kalshiSettings, signer, directory, *kalshiQueue, channels.kalshiFeed);
    PolymarketBookFeed polymarketFeed(config.polymarketFeed, directory, *polymarketQueue, channels.polymarketFeed);

    std::cout << "streaming " << pairCount << " pairs (" << directory.kalshiTickers().size() << " kalshi markets, "
              << directory.polymarketTokens().size() << " polymarket tokens, " << pairsWithPolymarketFees
              << " with polymarket fees), kalshi key " << (signer.scheme() == SignatureScheme::Ed25519 ? "ed25519" : "rsa-pss")
              << ", minimum edge " << config.engine.minimumEdgeTicks << " ticks\n";

    std::atomic<bool> engineStopRequested{false};
    std::thread engineThread([&] {
        elevateCurrentThreadPriority();
        engine->run(*kalshiQueue, *polymarketQueue, engineStopRequested);
    });
    kalshiFeed.start();
    polymarketFeed.start();

    StreamOutcome outcome = StreamOutcome::ShutdownRequested;
    while (!shutdownRequested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (kalshiFeed.hasAuthenticationFailed()) {
            std::cerr << "Kalshi rejected the API credentials. Check " << config.kalshiKeyIdentifierVariable
                      << " and make sure the private key matches the key ID (Kalshi keys are normally RSA)\n";
            outcome = StreamOutcome::AuthenticationFailed;
            break;
        }
        if (shouldEndSession()) {
            outcome = StreamOutcome::SessionEnded;
            break;
        }
    }

    kalshiFeed.stop();
    polymarketFeed.stop();
    engineStopRequested.store(true, std::memory_order_release);
    engineThread.join();

    const EngineCounters& counters = engine->counters();
    std::cout << "session summary: updates applied=" << counters.updatesApplied << " evaluations=" << counters.pairEvaluations
              << " opportunities opened=" << counters.opportunitiesOpened << " peak synchronized ladders="
              << counters.peakSynchronizedLadderCount << '/' << pairCount * laddersPerPair << '\n';
    return outcome;
}

}
