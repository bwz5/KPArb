#pragma once

#include <cstdint>
#include <string>

namespace kparb {

struct MatchVerdict {
    bool isSameEvent = false;
    bool isInverted = false;
    double confidence = 0.0;
    std::string reason;
    std::string model;
    std::int64_t decidedAtSeconds = 0;
};

struct MatchedPair {
    std::string kalshiTicker;
    std::string kalshiTitle;
    std::string polymarketIdentifier;
    std::string polymarketSlug;
    std::string polymarketTitle;
    std::string polymarketYesToken;
    std::string polymarketNoToken;
    bool isInverted = false;
    double confidence = 0.0;
    double polymarketTakerFeeRate = 0.0;
    double polymarketTakerFeeExponent = 1.0;
    bool isManual = false;
    double noResultProbability = 0.0;
    double polymarketYesNoResultValue = 0.0;
    double polymarketNoNoResultValue = 0.0;
};

}
