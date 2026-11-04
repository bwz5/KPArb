#pragma once

#include "common/Venue.h"

#include <cstdint>
#include <string>

namespace kparb {

struct MarketInfo {
    Venue venue = Venue::Kalshi;
    std::string identifier;
    std::string eventIdentifier;
    std::string title;
    std::string subtitle;
    std::string rules;
    std::string yesOutcomeName;
    std::string noOutcomeName;
    std::string yesTokenIdentifier;
    std::string noTokenIdentifier;
    std::int64_t closeTimeSeconds = 0;
    std::int64_t openTimeSeconds = 0;
    bool isAcceptingOrders = false;
    double takerFeeRate = 0.0;
    double takerFeeExponent = 1.0;
};

}
