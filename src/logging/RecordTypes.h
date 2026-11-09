#pragma once

#include "common/Price.h"
#include "common/Venue.h"
#include "logging/LogRecord.h"

#include <cstdint>

namespace kparb {

enum class SourceIdentifier : std::uint16_t {
    Application = 0,
    Engine = 1,
    KalshiFeed = 2,
    PolymarketFeed = 3,
    Matcher = 4,
    Discovery = 5,
};

enum class PairNameField : std::uint8_t {
    KalshiTicker = 0,
    PolymarketIdentifier = 1,
};

inline constexpr std::size_t pairNameChunkCapacity = 40;

struct PairNameRecord {
    static constexpr RecordType recordType = RecordType::PairName;
    std::uint32_t pairIndex;
    PairNameField field;
    std::uint8_t chunkIndex;
    std::uint8_t chunkCount;
    std::uint8_t isInverted;
    char text[pairNameChunkCapacity];
};

enum class TradeDirection : std::uint8_t {
    BuyKalshiYesBuyPolymarketNo = 0,
    BuyKalshiNoBuyPolymarketYes = 1,
};

struct OpportunityRecord {
    static constexpr RecordType recordType = RecordType::Opportunity;
    std::int64_t triggerReceivedAtNanos;
    std::int64_t expectedProfitTickQuantity;
    std::uint32_t pairIndex;
    std::uint32_t executableQuantity;
    Ticks kalshiBestPrice;
    Ticks polymarketBestPrice;
    std::int16_t topEdgeTicks;
    Ticks kalshiWorstPrice;
    Ticks polymarketWorstPrice;
    TradeDirection direction;
    std::uint8_t levelsConsumed;
};

struct OpportunityClosedRecord {
    static constexpr RecordType recordType = RecordType::OpportunityClosed;
    std::int64_t triggerReceivedAtNanos;
    std::int64_t openDurationNanos;
    std::uint32_t pairIndex;
    TradeDirection direction;
};

enum class FeedEventCode : std::uint16_t {
    Connected = 0,
    Disconnected = 1,
    ConnectionError = 2,
    AuthenticationFailed = 3,
    SubscriptionSent = 4,
    SubscriptionConfirmed = 5,
    SnapshotReceived = 6,
    SequenceGap = 7,
    QueueOverflow = 8,
    ParseError = 9,
    VenueReset = 10,
    ServerError = 11,
    Reconnecting = 12,
    ResyncRequested = 13,
};

struct FeedEventRecord {
    static constexpr RecordType recordType = RecordType::FeedEvent;
    std::int64_t firstArgument;
    std::int64_t secondArgument;
    Venue venue;
    FeedEventCode eventCode;
};

struct EngineStatsRecord {
    static constexpr RecordType recordType = RecordType::EngineStats;
    std::uint64_t updatesApplied;
    std::uint64_t pairEvaluations;
    std::uint64_t opportunitiesOpened;
    std::uint64_t droppedLogRecords;
    std::uint32_t kalshiQueueDepth;
    std::uint32_t polymarketQueueDepth;
    std::uint32_t openOpportunityCount;
    std::uint32_t synchronizedLadderCount;
};

enum class MatchEventCode : std::uint16_t {
    KalshiCatalogLoaded = 0,
    PolymarketCatalogLoaded = 1,
    CandidatesFound = 2,
    VerdictFromCache = 3,
    VerdictFromLanguageModel = 4,
    LanguageModelFailure = 5,
    PairAccepted = 6,
    MatchingFinished = 7,
};

struct MatchEventRecord {
    static constexpr RecordType recordType = RecordType::MatchEvent;
    std::int64_t firstArgument;
    std::int64_t secondArgument;
    std::int64_t thirdArgument;
    MatchEventCode eventCode;
};

static_assert(sizeof(PairNameRecord) <= payloadCapacity);
static_assert(sizeof(OpportunityRecord) <= payloadCapacity);
static_assert(sizeof(OpportunityClosedRecord) <= payloadCapacity);
static_assert(sizeof(FeedEventRecord) <= payloadCapacity);
static_assert(sizeof(EngineStatsRecord) <= payloadCapacity);
static_assert(sizeof(MatchEventRecord) <= payloadCapacity);

}
