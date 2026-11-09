#include "execution/TradePlanner.h"

namespace kparb {

namespace {

Ticks cappedPrice(std::int64_t price) noexcept
{
    constexpr std::int64_t highestBuyablePrice = ticksPerDollar - 1;
    return static_cast<Ticks>(price < 0 ? 0 : (price > highestBuyablePrice ? highestBuyablePrice : price));
}

}

std::optional<TradePlan> planTrade(const ExecutionSignal& signal, const PairRoute& route, const PlannerSettings& settings)
{
    const std::uint32_t contracts = signal.executableQuantity / quantityUnitsPerContract;
    if (signal.kind == SignalKind::Closed || contracts == 0 || signal.kalshiWorstPrice == noPrice || signal.polymarketWorstPrice == noPrice) {
        return std::nullopt;
    }
    const bool buysKalshiYes = signal.direction == TradeDirection::BuyKalshiYesBuyPolymarketNo;

    LegPlan kalshiLeg;
    kalshiLeg.venue = Venue::Kalshi;
    kalshiLeg.marketIdentifier = route.kalshiTicker;
    kalshiLeg.side = buysKalshiYes ? ContractSide::Yes : ContractSide::No;
    kalshiLeg.limitPrice = signal.kalshiWorstPrice;
    kalshiLeg.chaseLimitPrice = cappedPrice(static_cast<std::int64_t>(signal.kalshiWorstPrice) + settings.chaseTicks);
    kalshiLeg.unwindMarketIdentifier = route.kalshiTicker;
    kalshiLeg.unwindSide = buysKalshiYes ? ContractSide::No : ContractSide::Yes;
    kalshiLeg.unwindLimitPrice = signal.kalshiOppositeAsk == noPrice
        ? noPrice
        : cappedPrice(static_cast<std::int64_t>(signal.kalshiOppositeAsk) + settings.unwindSlippageTicks);

    LegPlan polymarketLeg;
    polymarketLeg.venue = Venue::Polymarket;
    polymarketLeg.marketIdentifier = buysKalshiYes ? route.polymarketOppositeToken : route.polymarketAlignedToken;
    polymarketLeg.side = ContractSide::Yes;
    polymarketLeg.limitPrice = signal.polymarketWorstPrice;
    polymarketLeg.chaseLimitPrice = cappedPrice(static_cast<std::int64_t>(signal.polymarketWorstPrice) + settings.chaseTicks);
    polymarketLeg.unwindMarketIdentifier = buysKalshiYes ? route.polymarketAlignedToken : route.polymarketOppositeToken;
    polymarketLeg.unwindSide = ContractSide::Yes;
    polymarketLeg.unwindLimitPrice = signal.polymarketOppositeAsk == noPrice
        ? noPrice
        : cappedPrice(static_cast<std::int64_t>(signal.polymarketOppositeAsk) + settings.unwindSlippageTicks);

    bool kalshiGoesFirst = true;
    switch (settings.firstLeg) {
    case FirstLegPolicy::ThinnerBookFirst:
        kalshiGoesFirst = signal.kalshiTopQuantity <= signal.polymarketTopQuantity;
        break;
    case FirstLegPolicy::KalshiFirst:
        kalshiGoesFirst = true;
        break;
    case FirstLegPolicy::PolymarketFirst:
        kalshiGoesFirst = false;
        break;
    }

    TradePlan plan;
    plan.contracts = contracts;
    plan.first = kalshiGoesFirst ? kalshiLeg : polymarketLeg;
    plan.second = kalshiGoesFirst ? polymarketLeg : kalshiLeg;
    plan.kalshiDollarsPerContract = ticksToDollars(signal.kalshiWorstPrice);
    plan.polymarketDollarsPerContract = ticksToDollars(signal.polymarketWorstPrice);
    return plan;
}

}
