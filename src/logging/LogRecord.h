#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace kparb {

enum class RecordType : std::uint16_t {
    Empty = 0,
    PairName = 1,
    Opportunity = 2,
    OpportunityClosed = 3,
    FeedEvent = 4,
    EngineStats = 5,
    MatchEvent = 6,
};

struct RecordHeader {
    std::int64_t timestampNanos;
    RecordType type;
    std::uint16_t sourceIdentifier;
    std::uint32_t sequenceNumber;
};

static_assert(sizeof(RecordHeader) == 16);

inline constexpr std::size_t recordSize = 64;
inline constexpr std::size_t payloadCapacity = recordSize - sizeof(RecordHeader);

struct alignas(recordSize) LogRecord {
    RecordHeader header;
    std::array<std::byte, payloadCapacity> payload;

    template <typename Payload>
    void storePayload(const Payload& value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Payload>);
        static_assert(sizeof(Payload) <= payloadCapacity);
        std::memcpy(payload.data(), &value, sizeof(Payload));
    }

    template <typename Payload>
    Payload loadPayload() const noexcept
    {
        static_assert(std::is_trivially_copyable_v<Payload>);
        static_assert(sizeof(Payload) <= payloadCapacity);
        Payload value;
        std::memcpy(&value, payload.data(), sizeof(Payload));
        return value;
    }
};

static_assert(sizeof(LogRecord) == recordSize);
static_assert(std::is_trivially_copyable_v<LogRecord>);

}
