#pragma once

#include "ingestion/MarketInfo.h"

#include <cstdint>
#include <vector>

namespace kparb {

struct ResolutionWindow {
    std::int64_t earliestSeconds;
    std::int64_t latestSeconds;
};

ResolutionWindow resolutionWindowFrom(std::int64_t nowSeconds, double maximumDaysUntilResolution);

std::vector<MarketInfo> keepMarketsResolvingWithin(std::vector<MarketInfo> markets, const ResolutionWindow& window);

}
