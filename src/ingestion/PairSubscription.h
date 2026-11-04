#pragma once

#include <string>

namespace kparb {

struct PairSubscription {
    std::string kalshiTicker;
    std::string polymarketYesEquivalentToken;
    std::string polymarketNoEquivalentToken;
};

}
