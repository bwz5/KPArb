#include "logging/BinaryLogReader.h"
#include "logging/RecordFormatting.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

using namespace kparb;

std::optional<RecordType> parseRecordType(const std::string& name)
{
    for (const RecordType type : {RecordType::PairName, RecordType::Opportunity, RecordType::OpportunityClosed,
             RecordType::FeedEvent, RecordType::EngineStats, RecordType::MatchEvent}) {
        if (recordTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

}

int main(int argumentCount, char** arguments)
{
    if (argumentCount < 2) {
        std::cerr << "usage: kparb_logdecode <file.bin> [--type opportunity|opportunity_closed|feed_event|engine_stats|match_event|pair_name] [--csv]\n";
        return 64;
    }
    const std::string path = arguments[1];
    std::optional<RecordType> filter;
    bool isCsv = false;
    for (int index = 2; index < argumentCount; ++index) {
        const std::string argument = arguments[index];
        if (argument == "--type" && index + 1 < argumentCount) {
            filter = parseRecordType(arguments[++index]);
            if (!filter) {
                std::cerr << "unknown record type\n";
                return 64;
            }
        } else if (argument == "--csv") {
            isCsv = true;
        } else {
            std::cerr << "unknown argument: " << argument << '\n';
            return 64;
        }
    }
    if (isCsv && !filter) {
        std::cerr << "--csv requires --type\n";
        return 64;
    }

    try {
        BinaryLogReader reader(path);
        PairNameDirectory names;
        if (isCsv) {
            std::cout << csvHeaderFor(*filter) << '\n';
        }
        LogRecord record{};
        while (reader.readNext(record)) {
            if (record.header.type == RecordType::PairName) {
                names.absorb(record.loadPayload<PairNameRecord>());
            }
            if (filter && record.header.type != *filter) {
                continue;
            }
            if (isCsv) {
                std::cout << formatRecordCsv(record, names) << '\n';
            } else {
                const double secondsSinceStart = static_cast<double>(record.header.timestampNanos - reader.fileHeader().steadyClockAnchorNanos) / 1e9;
                std::cout << '[' << std::fixed << secondsSinceStart << "s src=" << record.header.sourceIdentifier
                          << " seq=" << record.header.sequenceNumber << "] " << formatRecordText(record, names) << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
