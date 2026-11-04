#pragma once

#include "ingestion/JsonDocumentBuffer.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/LadderUpdateSink.h"

#include <cstdint>
#include <string_view>

namespace kparb {

struct PolymarketParseResult {
    bool isMalformed = false;
    std::uint32_t snapshotCount = 0;
    std::uint32_t priceChangeCount = 0;
    std::uint32_t updatesProduced = 0;
};

class PolymarketMessageParser {
public:
    explicit PolymarketMessageParser(const LadderDirectory& ladderDirectory, std::size_t initialCapacity = 1 << 22);

    PolymarketParseResult parse(std::string_view message, std::int64_t receivedAtNanos, LadderUpdateSink sink);

private:
    void parseEvent(simdjson::dom::object event, std::int64_t receivedAtNanos, LadderUpdateSink sink, PolymarketParseResult& result);
    void parseBook(simdjson::dom::object event, std::int64_t receivedAtNanos, LadderUpdateSink sink, PolymarketParseResult& result);
    void parseChange(std::string_view tokenIdentifier, simdjson::dom::object change, std::int64_t receivedAtNanos,
        LadderUpdateSink sink, PolymarketParseResult& result);

    const LadderDirectory& directory;
    JsonDocumentBuffer document;
};

}
