#pragma once

#include "common/Price.h"
#include "common/SpscQueue.h"
#include "common/Venue.h"

#include <cstdint>
#include <type_traits>

namespace kparb {

enum class UpdateKind : std::uint8_t {
    SetLevel = 0,
    AddToLevel = 1,
    ClearLadder = 2,
    ResetVenue = 3,
};

struct LadderUpdate {
    std::int64_t receivedAtNanos;
    std::uint32_t ladderIndex;
    std::int32_t quantity;
    Ticks price;
    UpdateKind kind;
    Venue venue;
};

static_assert(std::is_trivially_copyable_v<LadderUpdate>);
static_assert(sizeof(LadderUpdate) == 24);

enum class LadderSlot : std::uint32_t {
    KalshiBuyYes = 0,
    KalshiBuyNo = 1,
    PolymarketBuyYes = 2,
    PolymarketBuyNo = 3,
};

inline constexpr std::uint32_t laddersPerPair = 4;

constexpr std::uint32_t ladderIndexFor(std::uint32_t pairIndex, LadderSlot slot) noexcept
{
    return pairIndex * laddersPerPair + static_cast<std::uint32_t>(slot);
}

constexpr std::uint32_t pairIndexOfLadder(std::uint32_t ladderIndex) noexcept
{
    return ladderIndex / laddersPerPair;
}

constexpr std::uint32_t slotOfLadder(std::uint32_t ladderIndex) noexcept
{
    return ladderIndex % laddersPerPair;
}

inline constexpr std::size_t ladderUpdateQueueCapacity = 1 << 16;

using LadderUpdateQueue = SpscQueue<LadderUpdate, ladderUpdateQueueCapacity>;

}
