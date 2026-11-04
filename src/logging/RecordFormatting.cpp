#include "logging/RecordFormatting.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace kparb {

std::string_view recordTypeName(RecordType type) noexcept
{
    switch (type) {
    case RecordType::Empty:
        return "empty";
    case RecordType::PairName:
        return "pair_name";
    case RecordType::Opportunity:
        return "opportunity";
    case RecordType::OpportunityClosed:
        return "opportunity_closed";
    case RecordType::FeedEvent:
        return "feed_event";
    case RecordType::EngineStats:
        return "engine_stats";
    case RecordType::MatchEvent:
        return "match_event";
    }
    return "unknown";
}

std::string_view feedEventName(FeedEventCode code) noexcept
{
    switch (code) {
    case FeedEventCode::Connected:
        return "connected";
    case FeedEventCode::Disconnected:
        return "disconnected";
    case FeedEventCode::ConnectionError:
        return "connection_error";
    case FeedEventCode::AuthenticationFailed:
        return "authentication_failed";
    case FeedEventCode::SubscriptionSent:
        return "subscription_sent";
    case FeedEventCode::SubscriptionConfirmed:
        return "subscription_confirmed";
    case FeedEventCode::SnapshotReceived:
        return "snapshot_received";
    case FeedEventCode::SequenceGap:
        return "sequence_gap";
    case FeedEventCode::QueueOverflow:
        return "queue_overflow";
    case FeedEventCode::ParseError:
        return "parse_error";
    case FeedEventCode::VenueReset:
        return "venue_reset";
    case FeedEventCode::ServerError:
        return "server_error";
    case FeedEventCode::Reconnecting:
        return "reconnecting";
    case FeedEventCode::ResyncRequested:
        return "resync_requested";
    }
    return "unknown";
}

std::string_view matchEventName(MatchEventCode code) noexcept
{
    switch (code) {
    case MatchEventCode::KalshiCatalogLoaded:
        return "kalshi_catalog_loaded";
    case MatchEventCode::PolymarketCatalogLoaded:
        return "polymarket_catalog_loaded";
    case MatchEventCode::CandidatesFound:
        return "candidates_found";
    case MatchEventCode::VerdictFromCache:
        return "verdict_from_cache";
    case MatchEventCode::VerdictFromLanguageModel:
        return "verdict_from_language_model";
    case MatchEventCode::LanguageModelFailure:
        return "language_model_failure";
    case MatchEventCode::PairAccepted:
        return "pair_accepted";
    case MatchEventCode::MatchingFinished:
        return "matching_finished";
    }
    return "unknown";
}

std::string_view tradeDirectionName(TradeDirection direction) noexcept
{
    switch (direction) {
    case TradeDirection::BuyKalshiYesBuyPolymarketNo:
        return "kalshi_yes+polymarket_no";
    case TradeDirection::BuyKalshiNoBuyPolymarketYes:
        return "kalshi_no+polymarket_yes";
    }
    return "unknown";
}

std::vector<PairNameRecord> makePairNameRecords(std::uint32_t pairIndex, PairNameField field, std::string_view text, bool isInverted)
{
    std::vector<PairNameRecord> records;
    const std::size_t chunkCount = std::max<std::size_t>(1, (text.size() + pairNameChunkCapacity - 1) / pairNameChunkCapacity);
    for (std::size_t chunkIndex = 0; chunkIndex < chunkCount && chunkIndex < 255; ++chunkIndex) {
        PairNameRecord record{};
        record.pairIndex = pairIndex;
        record.field = field;
        record.chunkIndex = static_cast<std::uint8_t>(chunkIndex);
        record.chunkCount = static_cast<std::uint8_t>(std::min<std::size_t>(chunkCount, 255));
        record.isInverted = isInverted ? 1 : 0;
        const std::size_t offset = chunkIndex * pairNameChunkCapacity;
        const std::size_t length = std::min(pairNameChunkCapacity, text.size() - std::min(offset, text.size()));
        std::memcpy(record.text, text.data() + offset, length);
        records.push_back(record);
    }
    return records;
}

void PairNameDirectory::absorb(const PairNameRecord& record)
{
    auto& parts = chunks[{record.pairIndex, record.field}];
    if (record.chunkIndex == 0 || parts.size() != record.chunkCount) {
        parts.assign(record.chunkCount, std::string());
    }
    if (record.chunkIndex < parts.size()) {
        const std::size_t length = strnlen(record.text, pairNameChunkCapacity);
        parts[record.chunkIndex] = std::string(record.text, length);
    }
}

std::string PairNameDirectory::assemble(std::uint32_t pairIndex, PairNameField field) const
{
    const auto found = chunks.find({pairIndex, field});
    if (found == chunks.end()) {
        return "pair#" + std::to_string(pairIndex);
    }
    std::string result;
    for (const auto& part : found->second) {
        result += part;
    }
    return result;
}

std::string PairNameDirectory::kalshiTicker(std::uint32_t pairIndex) const
{
    return assemble(pairIndex, PairNameField::KalshiTicker);
}

std::string PairNameDirectory::polymarketIdentifier(std::uint32_t pairIndex) const
{
    return assemble(pairIndex, PairNameField::PolymarketIdentifier);
}

std::string formatRecordText(const LogRecord& record, const PairNameDirectory& names)
{
    std::ostringstream stream;
    stream << recordTypeName(record.header.type) << ' ';
    switch (record.header.type) {
    case RecordType::PairName: {
        const auto payload = record.loadPayload<PairNameRecord>();
        stream << "pair=" << payload.pairIndex << " field=" << static_cast<int>(payload.field)
               << " chunk=" << static_cast<int>(payload.chunkIndex) << '/' << static_cast<int>(payload.chunkCount);
        break;
    }
    case RecordType::Opportunity: {
        const auto payload = record.loadPayload<OpportunityRecord>();
        stream << std::fixed << std::setprecision(3)
               << names.kalshiTicker(payload.pairIndex) << " <-> " << names.polymarketIdentifier(payload.pairIndex)
               << ' ' << tradeDirectionName(payload.direction)
               << " kalshi=" << ticksToDollars(payload.kalshiBestPrice)
               << " polymarket=" << ticksToDollars(payload.polymarketBestPrice)
               << " edge=" << ticksToDollars(payload.topEdgeTicks)
               << std::setprecision(2)
               << " contracts=" << quantityToContracts(payload.executableQuantity)
               << " profit=$" << ticksToDollars(payload.expectedProfitTickQuantity) / quantityUnitsPerContract
               << " levels=" << static_cast<int>(payload.levelsConsumed)
               << " latency_ns=" << (record.header.timestampNanos - payload.triggerReceivedAtNanos);
        break;
    }
    case RecordType::OpportunityClosed: {
        const auto payload = record.loadPayload<OpportunityClosedRecord>();
        stream << names.kalshiTicker(payload.pairIndex) << " <-> " << names.polymarketIdentifier(payload.pairIndex)
               << ' ' << tradeDirectionName(payload.direction)
               << " open_ms=" << static_cast<double>(payload.openDurationNanos) / 1e6;
        break;
    }
    case RecordType::FeedEvent: {
        const auto payload = record.loadPayload<FeedEventRecord>();
        stream << venueName(payload.venue) << ' ' << feedEventName(payload.eventCode)
               << " first=" << payload.firstArgument << " second=" << payload.secondArgument;
        break;
    }
    case RecordType::EngineStats: {
        const auto payload = record.loadPayload<EngineStatsRecord>();
        stream << "updates=" << payload.updatesApplied << " evaluations=" << payload.pairEvaluations
               << " opened=" << payload.opportunitiesOpened << " open=" << payload.openOpportunityCount
               << " synchronized_ladders=" << payload.synchronizedLadderCount
               << " kalshi_queue=" << payload.kalshiQueueDepth << " polymarket_queue=" << payload.polymarketQueueDepth
               << " dropped_log_records=" << payload.droppedLogRecords;
        break;
    }
    case RecordType::MatchEvent: {
        const auto payload = record.loadPayload<MatchEventRecord>();
        stream << matchEventName(payload.eventCode) << " first=" << payload.firstArgument
               << " second=" << payload.secondArgument << " third=" << payload.thirdArgument;
        break;
    }
    case RecordType::Empty:
        break;
    }
    return stream.str();
}

std::string csvHeaderFor(RecordType type)
{
    switch (type) {
    case RecordType::Opportunity:
        return "timestamp_ns,kalshi_ticker,polymarket_identifier,direction,kalshi_price,polymarket_price,edge,contracts,expected_profit,kalshi_worst_price,polymarket_worst_price,levels,latency_ns";
    case RecordType::OpportunityClosed:
        return "timestamp_ns,kalshi_ticker,polymarket_identifier,direction,open_duration_ns";
    case RecordType::FeedEvent:
        return "timestamp_ns,venue,event,first_argument,second_argument";
    case RecordType::EngineStats:
        return "timestamp_ns,updates,evaluations,opened,open,synchronized_ladders,kalshi_queue,polymarket_queue,dropped_log_records";
    case RecordType::MatchEvent:
        return "timestamp_ns,event,first_argument,second_argument,third_argument";
    default:
        return "timestamp_ns,type";
    }
}

std::string formatRecordCsv(const LogRecord& record, const PairNameDirectory& names)
{
    std::ostringstream stream;
    stream << record.header.timestampNanos << ',';
    switch (record.header.type) {
    case RecordType::Opportunity: {
        const auto payload = record.loadPayload<OpportunityRecord>();
        stream << std::fixed << std::setprecision(3)
               << names.kalshiTicker(payload.pairIndex) << ',' << names.polymarketIdentifier(payload.pairIndex) << ','
               << tradeDirectionName(payload.direction) << ','
               << ticksToDollars(payload.kalshiBestPrice) << ',' << ticksToDollars(payload.polymarketBestPrice) << ','
               << ticksToDollars(payload.topEdgeTicks) << ','
               << std::setprecision(2) << quantityToContracts(payload.executableQuantity) << ','
               << ticksToDollars(payload.expectedProfitTickQuantity) / quantityUnitsPerContract << ','
               << std::setprecision(3) << ticksToDollars(payload.kalshiWorstPrice) << ','
               << ticksToDollars(payload.polymarketWorstPrice) << ','
               << static_cast<int>(payload.levelsConsumed) << ','
               << (record.header.timestampNanos - payload.triggerReceivedAtNanos);
        break;
    }
    case RecordType::OpportunityClosed: {
        const auto payload = record.loadPayload<OpportunityClosedRecord>();
        stream << names.kalshiTicker(payload.pairIndex) << ',' << names.polymarketIdentifier(payload.pairIndex) << ','
               << tradeDirectionName(payload.direction) << ',' << payload.openDurationNanos;
        break;
    }
    case RecordType::FeedEvent: {
        const auto payload = record.loadPayload<FeedEventRecord>();
        stream << venueName(payload.venue) << ',' << feedEventName(payload.eventCode) << ','
               << payload.firstArgument << ',' << payload.secondArgument;
        break;
    }
    case RecordType::EngineStats: {
        const auto payload = record.loadPayload<EngineStatsRecord>();
        stream << payload.updatesApplied << ',' << payload.pairEvaluations << ',' << payload.opportunitiesOpened << ','
               << payload.openOpportunityCount << ',' << payload.synchronizedLadderCount << ','
               << payload.kalshiQueueDepth << ',' << payload.polymarketQueueDepth << ',' << payload.droppedLogRecords;
        break;
    }
    case RecordType::MatchEvent: {
        const auto payload = record.loadPayload<MatchEventRecord>();
        stream << matchEventName(payload.eventCode) << ',' << payload.firstArgument << ','
               << payload.secondArgument << ',' << payload.thirdArgument;
        break;
    }
    default:
        stream << recordTypeName(record.header.type);
        break;
    }
    return stream.str();
}

}
