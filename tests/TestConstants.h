#pragma once

#include "common/Price.h"

#include <cstddef>
#include <cstdint>

namespace kparb::test_constants {

namespace time_units {
inline constexpr std::int64_t secondsPerMinute = 60;
inline constexpr std::int64_t secondsPerHour = 3'600;
inline constexpr std::int64_t secondsPerDay = 86'400;
inline constexpr std::int64_t daysPerWeek = 7;
inline constexpr std::int64_t nanosecondsPerSecond = 1'000'000'000;
}

namespace fees {
inline constexpr double kalshiTakerFeeRate = 0.07;
inline constexpr double politicsTakerFeeRate = 0.04;
inline constexpr double cryptoTakerFeeRate = 0.07;
inline constexpr double noFeeRate = 0.0;
inline constexpr double linearFeeExponent = 1.0;
inline constexpr double quadraticFeeExponent = 2.0;
}

namespace prices {
inline constexpr Ticks freePrice = 0;
inline constexpr Ticks tenCents = 100;
inline constexpr Ticks thirtyCents = 300;
inline constexpr Ticks fortyCents = 400;
inline constexpr Ticks fiftyCents = 500;
inline constexpr Ticks ninetyCents = 900;
inline constexpr Ticks oneDollar = ticksPerDollar;
}

namespace engine_test {
inline constexpr std::uint32_t twoPairs = 2;
inline constexpr std::uint32_t threePairs = 3;
inline constexpr std::uint32_t firstPair = 0;
inline constexpr std::uint32_t secondPair = 1;
inline constexpr std::uint32_t thirdPair = 2;
inline constexpr std::uint32_t pairOutsideEngine = 5;
inline constexpr std::uint32_t kalshiLaddersPerPair = 2;
inline constexpr std::int64_t defaultUpdateNanos = 1;
inline constexpr std::int64_t venueResetNanos = 5;

inline constexpr Ticks discountedKalshiAsk = 450;
inline constexpr Ticks underpricedPolymarketYesAsk = 300;
inline constexpr Ticks overpricedKalshiNoAsk = 650;
inline constexpr Ticks feeTestKalshiAsk = 490;
inline constexpr Ticks feeTestPolymarketAsk = 470;
inline constexpr Ticks slightlyCheapKalshiAsk = 495;
inline constexpr Ticks unsynchronizedAsk = 100;
inline constexpr Ticks expensiveAsk = 900;
inline constexpr Ticks queuedAsk = 300;

inline constexpr std::int32_t standardQuantity = 100;
inline constexpr std::int32_t largeQuantity = 1000;
inline constexpr std::int32_t partialQuantity = 300;
inline constexpr std::int32_t smallQuantity = 5;
inline constexpr std::int32_t otherSmallQuantity = 7;
inline constexpr std::int32_t refreshedQuantity = 50;
inline constexpr std::int32_t removedQuantity = 0;

inline constexpr std::int64_t maximumQuoteAgeNanos = 1000;
inline constexpr std::int64_t earlyQuoteNanos = 10;
inline constexpr std::int64_t lateQuoteNanos = 5000;
inline constexpr std::int64_t refreshedQuoteNanos = 5100;

inline constexpr std::int32_t depthMinimumEdgeTicks = 10;
inline constexpr Ticks depthKalshiFirstAsk = 400;
inline constexpr Ticks depthKalshiSecondAsk = 420;
inline constexpr Ticks depthKalshiThirdAsk = 480;
inline constexpr Ticks depthPolymarketFirstAsk = 500;
inline constexpr Ticks depthPolymarketSecondAsk = 530;
inline constexpr std::int32_t depthKalshiFirstQuantity = 100;
inline constexpr std::int32_t depthKalshiSecondQuantity = 200;
inline constexpr std::int32_t depthKalshiThirdQuantity = 500;
inline constexpr std::int32_t depthPolymarketFirstQuantity = 150;
inline constexpr std::int32_t depthPolymarketSecondQuantity = 1000;
inline constexpr std::int32_t depthPolymarketFirstRefreshedQuantity = 160;
inline constexpr std::int32_t depthKalshiSecondRefreshedQuantity = 250;
inline constexpr std::uint32_t depthLevelsConsumed = 3;

inline constexpr std::int64_t kalshiQuoteNanos = 10;
inline constexpr std::int64_t openedNanos = 20;
inline constexpr std::int64_t closedNanos = 70;

inline constexpr std::int64_t firstQueuedNanos = 1;
inline constexpr std::int64_t secondQueuedNanos = 2;
inline constexpr std::int64_t thirdQueuedNanos = 3;
inline constexpr std::size_t queuedLevelUpdates = 2;
}

namespace ladder_test {
inline constexpr Ticks middleAsk = 600;
inline constexpr Ticks lowAsk = 450;
inline constexpr Ticks highAsk = 700;
inline constexpr Quantity middleQuantity = 10;
inline constexpr Quantity lowQuantity = 20;
inline constexpr Quantity highQuantity = 30;
inline constexpr Ticks firstWordAsk = 5;
inline constexpr Ticks lastWordAsk = 900;
inline constexpr Quantity firstWordQuantity = 1;
inline constexpr Quantity lastWordQuantity = 2;
inline constexpr Ticks updatedAsk = 300;
inline constexpr Quantity initialQuantity = 10;
inline constexpr Quantity replacedQuantity = 25;
inline constexpr Ticks accumulatedAsk = 400;
inline constexpr std::int32_t firstAddition = 50;
inline constexpr std::int32_t secondAddition = 25;
inline constexpr std::int32_t oversizedRemoval = -100;
inline constexpr Ticks bestAsk = 100;
inline constexpr Ticks worseAsk = 200;
inline constexpr Quantity bestQuantity = 1;
inline constexpr Quantity worseQuantity = 2;
inline constexpr Ticks clearedAsk = 250;
inline constexpr Quantity staleQuantity = 40;
inline constexpr Quantity freshQuantity = 5;
inline constexpr Quantity anyQuantity = 1;
inline constexpr Quantity removedQuantity = 0;
}

namespace fee_test {
inline constexpr Ticks noFee = 0;
inline constexpr Ticks kalshiFeeAtFiftyCents = 18;
inline constexpr Ticks kalshiFeeAtTenOrNinetyCents = 7;
inline constexpr Ticks politicsFeeAtFiftyCents = 10;
inline constexpr Ticks cryptoFeeAtThirtyCents = 15;
}

namespace hot_path_test {
inline constexpr std::uint32_t pairCount = 64;
inline constexpr std::uint32_t randomSeed = 7;
inline constexpr int lowestRandomAsk = 300;
inline constexpr int highestRandomAsk = 700;
inline constexpr std::int32_t largestRandomQuantity = 1000;
inline constexpr int randomUpdateCount = 20'000;
inline constexpr int logWriteCount = 1000;
}

namespace queue_test {
inline constexpr std::size_t smallCapacity = 8;
inline constexpr std::size_t tinyCapacity = 4;
inline constexpr std::size_t batchCapacity = 16;
inline constexpr std::size_t stressCapacity = 1024;
inline constexpr int orderedElementCount = 5;
inline constexpr int rejectedValue = 99;
inline constexpr int emptySentinel = -1;
inline constexpr std::uint64_t wraparoundIterations = 1000;
inline constexpr int batchFillCount = 10;
inline constexpr std::size_t firstBatchSize = 4;
inline constexpr std::uint64_t stressItemCount = 10'000'000;
inline constexpr std::size_t stressBatchSize = 64;
}

namespace decimal_test {
inline constexpr std::int64_t fiftyTwoPointThreeCentsTicks = 523;
inline constexpr std::int64_t fiftyTwoPointFourCentsTicks = 524;
inline constexpr std::int64_t fiftyCentsTicks = 500;
inline constexpr std::int64_t oneDollarTicks = 1000;
inline constexpr std::int64_t sevenCentsTicks = 70;
inline constexpr std::int64_t twelveHundredThirtyFourPointFiveSixUnits = 123'456;
inline constexpr std::int64_t negativeTwelveUnits = -1'200;
}

namespace calendar {
inline constexpr std::int64_t unixEpochSeconds = 0;
inline constexpr std::int64_t novemberFifth2024NoonUtcSeconds = 1'730'808'000;
inline constexpr std::int64_t novemberFifth2024MidnightUtcSeconds = 1'730'764'800;
inline constexpr std::int64_t leapDay2000MidnightUtcSeconds = 951'782'400;
inline constexpr std::int64_t december2026EveningUtcSeconds = 1'796'842'800;
inline constexpr std::int64_t lastSecondOf2099UtcSeconds = 4'102'444'799;
inline constexpr std::int64_t leapDay2024MidnightUtcSeconds = 1'709'164'800;
inline constexpr std::int64_t referenceNowSeconds = 1'800'000'000;
}

namespace market_filter_test {
inline constexpr double resolutionWindowDays = 2.0;
inline constexpr std::int64_t resolutionWindowSeconds = 2 * time_units::secondsPerDay;
inline constexpr std::int64_t unknownTimeSeconds = 0;
inline constexpr std::int64_t alreadyOpenSeconds = 1;
inline constexpr std::size_t keptMarketCount = 2;
}

namespace catalog_test {
inline constexpr std::int64_t kalshiFixtureCloseSeconds = 1'796'842'800;
inline constexpr std::int64_t kalshiFixtureOpenSeconds = 1'793'541'600;
inline constexpr std::int64_t polymarketFixtureCloseSeconds = 1'796'774'400;
inline constexpr std::int64_t polymarketFixtureOpenSeconds = 1'790'856'000;
inline constexpr double polymarketFixtureFeeRate = 0.04;
inline constexpr std::size_t polymarketFixtureEntryCount = 3;
inline constexpr std::size_t kalshiFixtureMarketCount = 2;
inline constexpr std::int64_t bosnaGameStartSeconds = 1'793'817'000;
inline constexpr std::int64_t bosnaResolutionDays = 7;
inline constexpr double bosnaSpreadLine = 7.5;
inline constexpr double atlantaWashingtonTotal = 225.5;
inline constexpr double newYorkHighCap = 50.0;
inline constexpr std::size_t sportsMarketCount = 2;
inline constexpr std::size_t strikeMarketCount = 3;
inline constexpr int httpOk = 200;
inline constexpr std::size_t requestsAfterOneTruncatedPage = 2;
}

namespace config_test {
inline constexpr std::int64_t exampleCloseTimeDifferenceDays = 2;
inline constexpr double exampleMaximumDaysUntilResolution = 2.0;
inline constexpr std::int64_t exampleRematchIntervalMinutes = 60;
inline constexpr std::size_t examplePairsBeforeStreaming = 500;
inline constexpr std::int64_t exampleMaximumMinutesBeforeStreaming = 5;
inline constexpr std::size_t examplePairsBetweenStreamRestarts = 25;
inline constexpr std::int32_t defaultMinimumEdgeTicks = 5;
inline constexpr std::int64_t exampleStatsIntervalSeconds = 10;
inline constexpr std::int64_t examplePolymarketMoneylineSearchDays = 10;
inline constexpr std::size_t exampleConcurrentRequests = 8;
inline constexpr std::size_t exampleCandidatesPerPolymarketMarket = 3;
inline constexpr double exampleMinimumConfidence = 0.85;
inline constexpr std::int32_t overriddenMinimumEdgeTicks = 12;
inline constexpr std::int64_t defaultTextPingIntervalSeconds = 10;
inline constexpr double savedPairConfidence = 0.9;
inline constexpr double savedPairFeeRate = 0.04;
}

namespace log_record_test {
inline constexpr std::size_t recordBytes = 64;
inline constexpr std::uint32_t pairIndex = 7;
inline constexpr Ticks kalshiBestPrice = 420;
inline constexpr Ticks polymarketBestPrice = 550;
inline constexpr std::uint32_t executableQuantity = 1200;
inline constexpr std::size_t longIdentifierLength = 95;
inline constexpr std::uint32_t namedPairIndex = 3;
inline constexpr std::uint32_t unnamedPairIndex = 9;
inline constexpr std::size_t chunksForLongIdentifier = 3;
}

namespace binary_logger_test {
inline constexpr std::uint32_t singleProducerRecordCount = 1000;
inline constexpr std::uint32_t recordsPerProducer = 100'000;
inline constexpr std::uint16_t firstSource = 1;
inline constexpr std::uint16_t secondSource = 2;
inline constexpr std::uint64_t producerCount = 2;
inline constexpr std::size_t sequenceSlots = 3;
inline constexpr std::int64_t noSequenceYet = -1;
}

namespace parser_test {
inline constexpr std::int64_t kalshiReceivedAtNanos = 77;
inline constexpr std::int64_t polymarketReceivedAtNanos = 9;
inline constexpr std::int64_t warmupReceivedAtNanos = 1;
inline constexpr int allocationCheckIterations = 1000;
inline constexpr std::size_t clearUpdatesPerKalshiSnapshot = 2;
inline constexpr std::size_t clearUpdatesPerPolymarketBook = 2;
inline constexpr std::size_t kalshiSnapshotYesBidLevels = 2;
inline constexpr std::size_t kalshiFixedPointLevels = 2;

inline constexpr std::int64_t kalshiSnapshotSubscription = 2;
inline constexpr std::int64_t kalshiSnapshotSequence = 1;
inline constexpr std::int64_t kalshiDeltaSequence = 3;
inline constexpr std::size_t kalshiSnapshotUpdateCount = 6;
inline constexpr Ticks kalshiFirstYesBidCents = 8;
inline constexpr Ticks kalshiSecondYesBidCents = 22;
inline constexpr Ticks kalshiFirstNoBidCents = 54;
inline constexpr Ticks kalshiSecondNoBidCents = 56;
inline constexpr std::int32_t kalshiFirstYesBidContracts = 300;
inline constexpr std::int32_t kalshiFirstNoBidContracts = 20;
inline constexpr Ticks ticksPerCent = 10;
inline constexpr Ticks kalshiDollarYesBidTicks = 85;
inline constexpr Ticks kalshiFixedPointNoBidTicks = 545;
inline constexpr std::int32_t kalshiFixedPointNoBidQuantity = 1250;
inline constexpr Ticks kalshiDeltaYesBidCents = 96;
inline constexpr std::int32_t kalshiDeltaContracts = -54;
inline constexpr Ticks kalshiNoSideDeltaBidTicks = 300;
inline constexpr std::int32_t kalshiNoSideDeltaQuantity = 1250;
inline constexpr std::int64_t kalshiSubscribedIdentifier = 5;
inline constexpr std::int64_t kalshiAlreadySubscribedCode = 6;
inline constexpr std::size_t kalshiUpdatesPerSnapshotAndDelta = 7;

inline constexpr Ticks polymarketFirstAsk = 520;
inline constexpr Ticks polymarketSecondAsk = 530;
inline constexpr std::int32_t polymarketFirstAskQuantity = 2500;
inline constexpr std::int32_t polymarketSecondAskQuantity = 6050;
inline constexpr std::size_t polymarketBookUpdateCount = 6;
inline constexpr std::size_t polymarketSecondLevelUpdateIndex = 4;
inline constexpr std::size_t polymarketLaddersPerToken = 2;
inline constexpr std::size_t polymarketBookCount = 2;
inline constexpr Ticks polymarketRemovedAsk = 610;
inline constexpr Ticks polymarketLegacyAsk = 400;
inline constexpr std::int32_t polymarketLegacyQuantity = 330'000;
inline constexpr std::size_t polymarketUpdatesPerBookAndChange = 8;
}

namespace gzip_test {
inline constexpr int repeatedTickerCount = 20'000;
inline constexpr std::size_t outputSlackBytes = 32;
inline constexpr int gzipHeaderWindowBitsOffset = 16;
inline constexpr int deflateMemoryLevel = 8;
inline constexpr std::size_t repeatedCharacterCount = 100'000;
inline constexpr std::size_t truncationDivisor = 2;
}

namespace publisher_test {
inline constexpr std::uint16_t channelSource = 2;
inline constexpr std::int64_t updateNanos = 1;
inline constexpr std::uint32_t ladderIndex = 4;
inline constexpr std::int32_t quantity = 10;
inline constexpr Ticks price = 500;
}

namespace signer_test {
inline constexpr std::size_t rsaKeyBits = 2048;
inline constexpr std::int64_t requestTimestampMilliseconds = 1'703'123'456'789;
inline constexpr std::size_t ed25519SignatureBytes = 64;
inline constexpr std::size_t authenticationHeaderCount = 3;
inline constexpr std::size_t keyHeaderIndex = 0;
inline constexpr std::size_t signatureHeaderIndex = 1;
inline constexpr std::size_t timestampHeaderIndex = 2;
inline constexpr std::size_t base64PaddingCharacters = 2;
}

namespace environment_test {
inline constexpr std::size_t parsedEntryCount = 5;
inline constexpr std::size_t plainEntry = 0;
inline constexpr std::size_t exportedEntry = 1;
inline constexpr std::size_t quotedEntry = 2;
inline constexpr std::size_t singleQuotedEntry = 3;
inline constexpr std::size_t emptyEntry = 4;
}

namespace matching_test {
inline constexpr std::int64_t standardCloseSeconds = 1'000'000;
inline constexpr std::int64_t nearbyCloseSeconds = 1'100'000;
inline constexpr std::int64_t distantCloseSeconds = 9'000'000;
inline constexpr double looseSimilarity = 0.1;
inline constexpr double moderateSimilarity = 0.2;
inline constexpr double strictSimilarity = 0.3;
inline constexpr double noSimilarityRequired = 0.0;
inline constexpr double perfectScore = 1.0;
inline constexpr double scoreTolerance = 1e-9;
inline constexpr std::size_t singleCandidatePerMarket = 1;
inline constexpr std::size_t twoCandidatesPerPolymarketMarket = 2;
inline constexpr std::size_t twoCandidates = 2;
inline constexpr double telekomSpreadLine = 7.5;
inline constexpr double lowerTotalLine = 220.5;
inline constexpr double higherTotalLine = 225.5;
inline constexpr int hourlyBitcoinMarketCount = 6;
inline constexpr int closestBitcoinMarketHour = 2;
inline constexpr int sharedTeamsPerGame = 2;
inline constexpr std::size_t noSharedEntities = 0;
inline constexpr std::size_t bothEntitiesShared = 2;
inline constexpr double pointsPropLine = 9.5;
inline constexpr std::size_t testMarketCount = 3;
inline constexpr std::size_t confirmedPairCount = 2;
inline constexpr int requestsAfterManualExclusion = 2;
inline constexpr int requestsWithOneRetry = 4;
inline constexpr std::size_t verdictsAfterOneQuotaFailure = 2;
inline constexpr std::size_t failuresBeforeDisabling = 2;
inline constexpr std::size_t parallelRequests = 3;
inline constexpr int minimumObservedParallelism = 2;
inline constexpr int concurrentRequestMilliseconds = 50;
inline constexpr double cachedConfidence = 0.99;
inline constexpr double mirroredConfidence = 0.95;
inline constexpr double parsedConfidence = 0.8;
inline constexpr double storedConfidence = 0.93;
inline constexpr std::int64_t shortQuotaWaitSeconds = 1;
inline constexpr std::int64_t noQuotaWaitSeconds = 0;
inline constexpr std::size_t promptMessageCount = 2;
inline constexpr std::size_t userMessageIndex = 1;
inline constexpr std::size_t requestMessageCount = 2;
}

namespace entity_rule_test {
inline constexpr std::int64_t oneDay = 1;
inline constexpr std::int64_t kalshiFifteenHundredUtcHour = 15;
inline constexpr std::int64_t halfHourSeconds = 1'800;
inline constexpr std::int64_t oneAmUtcHour = 1;
inline constexpr std::int64_t noonUtcHour = 12;
inline constexpr std::int64_t sixPmUtcHour = 18;
inline constexpr double anyCandidateScore = 1.0;
inline constexpr std::int64_t sameCloseTime = 0;
inline constexpr std::size_t firstKalshiMarket = 0;
inline constexpr std::size_t nextDayKalshiMarket = 2;
inline constexpr std::size_t sameDayPolymarketMarket = 0;
inline constexpr std::size_t nextDayPolymarketMarket = 1;
}

namespace entity_directory_test {
inline constexpr std::size_t fixtureEntityCount = 16;
inline constexpr std::size_t minimumConfiguredLeagueCount = 4;
inline constexpr std::size_t minimumConfiguredEntityCount = 126;
}

namespace ladder_directory_test {
inline constexpr std::uint32_t subscribedPairCount = 3;
inline constexpr std::size_t pairsSharingKalshiMarket = 2;
inline constexpr std::size_t pairsSharingPolymarketToken = 2;
inline constexpr std::size_t distinctKalshiTickers = 2;
inline constexpr std::size_t distinctPolymarketTokens = 4;
inline constexpr std::size_t subscriptionChunkSize = 2;
inline constexpr std::size_t subscriptionMessageCount = 2;
inline constexpr std::uint32_t firstPair = 0;
inline constexpr std::uint32_t secondPair = 1;
inline constexpr std::uint32_t thirdPair = 2;
}

namespace log_channel_test {
inline constexpr std::size_t overflowWriteCount = 2;
}

namespace sequence_test {
inline constexpr std::int64_t mergedSubscription = 1;
inline constexpr std::int64_t otherSubscription = 2;
inline constexpr std::int64_t firstSequence = 1;
inline constexpr std::int64_t subscriptionUpdateSequence = 2;
inline constexpr std::int64_t followingSequence = 3;
inline constexpr std::int64_t skippedToSequence = 5;
inline constexpr std::int64_t unsequenced = -1;
inline constexpr std::int64_t untrackedSubscription = 4096;
}

}
