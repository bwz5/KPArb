#pragma once

#include <cstdint>

namespace kparb {

inline constexpr std::uint64_t logFileMagic = 0x474F4C4252415050ULL;
inline constexpr std::uint32_t logFileFormatVersion = 1;

struct LogFileHeader {
    std::uint64_t magic;
    std::uint32_t formatVersion;
    std::uint32_t recordSizeBytes;
    std::int64_t wallClockAnchorNanos;
    std::int64_t steadyClockAnchorNanos;
};

static_assert(sizeof(LogFileHeader) == 32);

}
