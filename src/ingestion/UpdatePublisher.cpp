#include "ingestion/UpdatePublisher.h"

namespace kparb {

UpdatePublisher::UpdatePublisher(Venue feedVenue, LadderUpdateQueue& updateQueue, LogChannel& eventChannel) noexcept
    : venue(feedVenue)
    , queue(updateQueue)
    , channel(eventChannel)
{
}

void UpdatePublisher::publish(const LadderUpdate& update) noexcept
{
    if (isResetPending && !pushResetMarker(update.receivedAtNanos)) [[unlikely]] {
        return;
    }
    if (queue.tryPush(update)) [[likely]] {
        ++published;
        return;
    }
    isResetPending = true;
    overflowSignalled.store(true, std::memory_order_release);
    logEvent(FeedEventCode::QueueOverflow, static_cast<std::int64_t>(published));
}

void UpdatePublisher::invalidateBooks(std::int64_t nowNanos) noexcept
{
    isResetPending = true;
    pushResetMarker(nowNanos);
}

bool UpdatePublisher::pushResetMarker(std::int64_t nowNanos) noexcept
{
    if (!queue.tryPush(LadderUpdate{nowNanos, 0, 0, 0, UpdateKind::ResetVenue, venue})) {
        return false;
    }
    isResetPending = false;
    logEvent(FeedEventCode::VenueReset);
    return true;
}

void UpdatePublisher::logEvent(FeedEventCode code, std::int64_t firstArgument, std::int64_t secondArgument) noexcept
{
    FeedEventRecord event{};
    event.firstArgument = firstArgument;
    event.secondArgument = secondArgument;
    event.venue = venue;
    event.eventCode = code;
    channel.write(event);
}

bool UpdatePublisher::takeOverflowSignal() noexcept
{
    return overflowSignalled.exchange(false, std::memory_order_acq_rel);
}

std::uint64_t UpdatePublisher::publishedCount() const noexcept
{
    return published;
}

}
