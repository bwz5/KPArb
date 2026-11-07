#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kparb {

class SequenceTracker {
public:
    static constexpr std::size_t trackedSubscriptionCount = 256;

    void reset() noexcept
    {
        lastSequenceBySubscription.fill(0);
    }

    bool acceptAndCheckContinuity(std::int64_t subscriptionIdentifier, std::int64_t sequenceNumber) noexcept
    {
        if (subscriptionIdentifier < 0 || sequenceNumber < 0
            || subscriptionIdentifier >= static_cast<std::int64_t>(trackedSubscriptionCount)) {
            return true;
        }
        std::int64_t& lastSequence = lastSequenceBySubscription[static_cast<std::size_t>(subscriptionIdentifier)];
        const bool isContinuous = lastSequence == 0 || sequenceNumber == lastSequence + 1;
        lastSequence = sequenceNumber;
        return isContinuous;
    }

private:
    std::array<std::int64_t, trackedSubscriptionCount> lastSequenceBySubscription{};
};

}
