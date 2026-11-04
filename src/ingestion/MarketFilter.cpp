#include "ingestion/MarketFilter.h"

#include <algorithm>

namespace kparb {

ResolutionWindow resolutionWindowFrom(std::int64_t nowSeconds, double maximumDaysUntilResolution)
{
    const auto windowSeconds = static_cast<std::int64_t>(maximumDaysUntilResolution * 86400.0);
    return ResolutionWindow{nowSeconds, nowSeconds + windowSeconds};
}

std::vector<MarketInfo> keepMarketsResolvingWithin(std::vector<MarketInfo> markets, const ResolutionWindow& window)
{
    markets.erase(std::remove_if(markets.begin(), markets.end(),
                      [&window](const MarketInfo& market) {
                          return market.closeTimeSeconds <= 0 || market.closeTimeSeconds < window.earliestSeconds
                              || market.closeTimeSeconds > window.latestSeconds;
                      }),
        markets.end());
    return markets;
}

}
