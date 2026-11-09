#pragma once

#include "common/Price.h"
#include "common/SpscQueue.h"
#include "logging/RecordTypes.h"

#include <cstddef>
#include <cstdint>

namespace kparb {

enum class SignalKind : std::uint8_t {
    Opened,
    Changed,
    Closed,
};

struct ExecutionSignal {
    std::int64_t detectedAtNanos = 0;
    std::uint32_t pairIndex = 0;
    std::uint32_t executableQuantity = 0;
    std::uint32_t kalshiTopQuantity = 0;
    std::uint32_t polymarketTopQuantity = 0;
    Ticks kalshiPrice = noPrice;
    Ticks polymarketPrice = noPrice;
    Ticks kalshiWorstPrice = noPrice;
    Ticks polymarketWorstPrice = noPrice;
    Ticks kalshiOppositeAsk = noPrice;
    Ticks polymarketOppositeAsk = noPrice;
    std::int16_t edgeTicks = 0;
    TradeDirection direction = TradeDirection::BuyKalshiYesBuyPolymarketNo;
    SignalKind kind = SignalKind::Opened;
};

inline constexpr std::size_t executionSignalQueueCapacity = 4096;
using ExecutionSignalQueue = SpscQueue<ExecutionSignal, executionSignalQueueCapacity>;

}
