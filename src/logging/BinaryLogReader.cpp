#include "logging/BinaryLogReader.h"

#include <stdexcept>

namespace kparb {

BinaryLogReader::BinaryLogReader(const std::filesystem::path& path)
{
    file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("Unable to open log file " + path.string());
    }
    if (std::fread(&header, sizeof(header), 1, file) != 1) {
        throw std::runtime_error("Log file is missing its header");
    }
    if (header.magic != logFileMagic) {
        throw std::runtime_error("Log file has an unexpected magic number");
    }
    if (header.formatVersion != logFileFormatVersion || header.recordSizeBytes != sizeof(LogRecord)) {
        throw std::runtime_error("Log file format is not supported");
    }
}

BinaryLogReader::~BinaryLogReader()
{
    if (file != nullptr) {
        std::fclose(file);
    }
}

bool BinaryLogReader::readNext(LogRecord& record)
{
    return std::fread(&record, sizeof(LogRecord), 1, file) == 1;
}

const LogFileHeader& BinaryLogReader::fileHeader() const noexcept
{
    return header;
}

std::int64_t BinaryLogReader::toWallClockNanos(std::int64_t steadyNanos) const noexcept
{
    return header.wallClockAnchorNanos + (steadyNanos - header.steadyClockAnchorNanos);
}

}
