#pragma once

#include "logging/LogFileFormat.h"
#include "logging/LogRecord.h"

#include <cstdio>
#include <filesystem>

namespace kparb {

class BinaryLogReader {
public:
    explicit BinaryLogReader(const std::filesystem::path& path);
    ~BinaryLogReader();

    BinaryLogReader(const BinaryLogReader&) = delete;
    BinaryLogReader& operator=(const BinaryLogReader&) = delete;

    bool readNext(LogRecord& record);
    const LogFileHeader& fileHeader() const noexcept;
    std::int64_t toWallClockNanos(std::int64_t steadyNanos) const noexcept;

private:
    std::FILE* file = nullptr;
    LogFileHeader header{};
};

}
