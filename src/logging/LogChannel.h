#pragma once

#include "common/CacheLine.h"
#include "common/Clock.h"
#include "common/SpscQueue.h"
#include "logging/LogRecord.h"

#include <atomic>
#include <cstdint>

namespace kparb {

inline constexpr std::size_t logChannelCapacity = 1 << 14;

using LogRecordQueue = SpscQueue<LogRecord, logChannelCapacity>;

class LogChannel {
public:
    explicit LogChannel(std::uint16_t sourceIdentifier) noexcept
        : source(sourceIdentifier)
    {
    }

    LogChannel(const LogChannel&) = delete;
    LogChannel& operator=(const LogChannel&) = delete;

    template <typename Payload>
    bool write(const Payload& payload) noexcept
    {
        return writeAt(payload, steadyNanoseconds());
    }

    template <typename Payload>
    bool writeAt(const Payload& payload, std::int64_t timestampNanos) noexcept
    {
        LogRecord record;
        record.header.timestampNanos = timestampNanos;
        record.header.type = Payload::recordType;
        record.header.sourceIdentifier = source;
        record.header.sequenceNumber = nextSequenceNumber++;
        record.payload = {};
        record.storePayload(payload);
        if (queue.tryPush(record)) {
            return true;
        }
        droppedRecords.value.store(droppedRecords.value.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return false;
    }

    std::uint64_t droppedRecordCount() const noexcept
    {
        return droppedRecords.value.load(std::memory_order_relaxed);
    }

    std::uint16_t sourceIdentifier() const noexcept
    {
        return source;
    }

    LogRecordQueue& records() noexcept
    {
        return queue;
    }

private:
    std::uint16_t source;
    std::uint32_t nextSequenceNumber = 0;
    CacheLinePadded<std::atomic<std::uint64_t>> droppedRecords;
    LogRecordQueue queue;
};

}
