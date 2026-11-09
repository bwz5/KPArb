#pragma once

#include "arbitrage/ExecutionSignal.h"
#include "execution/OrderTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace kparb {

enum class FirstLegPolicy : std::uint8_t {
    ThinnerBookFirst,
    KalshiFirst,
    PolymarketFirst,
};

struct PlannerSettings {
    FirstLegPolicy firstLeg = FirstLegPolicy::ThinnerBookFirst;
    std::int32_t chaseTicks = 5;
    std::int32_t unwindSlippageTicks = 20;
};

struct PairRoute {
    std::string kalshiTicker;
    std::string polymarketAlignedToken;
    std::string polymarketOppositeToken;
    std::string polymarketSlug;
    std::string polymarketYesToken;
    double polymarketTakerFeeRate = 0.0;
    double polymarketTakerFeeExponent = 1.0;
};

struct LegPlan {
    Venue venue = Venue::Kalshi;
    std::string marketIdentifier;
    ContractSide side = ContractSide::Yes;
    Ticks limitPrice = noPrice;
    Ticks chaseLimitPrice = noPrice;
    std::string unwindMarketIdentifier;
    ContractSide unwindSide = ContractSide::No;
    Ticks unwindLimitPrice = noPrice;
};

struct TradePlan {
    std::uint32_t contracts = 0;
    LegPlan first;
    LegPlan second;
    double kalshiDollarsPerContract = 0.0;
    double polymarketDollarsPerContract = 0.0;
};

std::optional<TradePlan> planTrade(const ExecutionSignal& signal, const PairRoute& route, const PlannerSettings& settings);

}
