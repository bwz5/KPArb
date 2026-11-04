#pragma once

#include "ingestion/JsonDocumentBuffer.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/LadderUpdateSink.h"

#include <cstdint>
#include <string_view>

namespace kparb {

enum class KalshiMessageKind : std::uint8_t {
    Snapshot,
    Delta,
    Subscribed,
    Error,
    Ignored,
    UnknownMarket,
    Malformed,
};

struct KalshiParseResult {
    KalshiMessageKind kind = KalshiMessageKind::Ignored;
    std::int64_t subscriptionIdentifier = -1;
    std::int64_t sequenceNumber = -1;
    std::int64_t errorCode = 0;
    std::uint32_t updatesProduced = 0;
};

class KalshiMessageParser {
public:
    explicit KalshiMessageParser(const LadderDirectory& ladderDirectory, std::size_t initialCapacity = 1 << 20);

    KalshiParseResult parse(std::string_view message, std::int64_t receivedAtNanos, LadderUpdateSink sink);

private:
    const LadderDirectory& directory;
    JsonDocumentBuffer document;
};

}
