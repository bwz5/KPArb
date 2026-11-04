#pragma once

#include "logging/LogRecord.h"
#include "logging/RecordTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kparb {

std::string_view recordTypeName(RecordType type) noexcept;
std::string_view feedEventName(FeedEventCode code) noexcept;
std::string_view matchEventName(MatchEventCode code) noexcept;
std::string_view tradeDirectionName(TradeDirection direction) noexcept;

std::vector<PairNameRecord> makePairNameRecords(std::uint32_t pairIndex, PairNameField field, std::string_view text, bool isInverted);

class PairNameDirectory {
public:
    void absorb(const PairNameRecord& record);
    std::string kalshiTicker(std::uint32_t pairIndex) const;
    std::string polymarketIdentifier(std::uint32_t pairIndex) const;

private:
    std::string assemble(std::uint32_t pairIndex, PairNameField field) const;

    std::map<std::pair<std::uint32_t, PairNameField>, std::vector<std::string>> chunks;
};

std::string formatRecordText(const LogRecord& record, const PairNameDirectory& names);
std::string csvHeaderFor(RecordType type);
std::string formatRecordCsv(const LogRecord& record, const PairNameDirectory& names);

}
