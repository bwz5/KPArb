#pragma once

#include "arbitrage/LadderUpdate.h"
#include "logging/LogChannel.h"
#include "logging/RecordTypes.h"

#include <atomic>
#include <cstdint>

namespace kparb {

class UpdatePublisher {
public:
    UpdatePublisher(Venue feedVenue, LadderUpdateQueue& updateQueue, LogChannel& eventChannel) noexcept;

    void publish(const LadderUpdate& update) noexcept;
    void invalidateBooks(std::int64_t nowNanos) noexcept;
    void logEvent(FeedEventCode code, std::int64_t firstArgument = 0, std::int64_t secondArgument = 0) noexcept;

    bool takeOverflowSignal() noexcept;
    std::uint64_t publishedCount() const noexcept;

private:
    bool pushResetMarker(std::int64_t nowNanos) noexcept;

    Venue venue;
    LadderUpdateQueue& queue;
    LogChannel& channel;
    bool isResetPending = false;
    std::uint64_t published = 0;
    std::atomic<bool> overflowSignalled{false};
};

}
