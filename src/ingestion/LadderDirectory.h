#pragma once

#include "ingestion/PairSubscription.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kparb {

struct KalshiLadderTargets {
    std::uint32_t buyYesLadder;
    std::uint32_t buyNoLadder;
};

class LadderDirectory {
public:
    explicit LadderDirectory(const std::vector<PairSubscription>& subscriptions);

    const std::vector<KalshiLadderTargets>* findKalshiMarket(std::string_view ticker) const noexcept;
    const std::vector<std::uint32_t>* findPolymarketToken(std::string_view tokenIdentifier) const noexcept;

    std::vector<std::string> kalshiTickers() const;
    std::vector<std::string> polymarketTokens() const;
    std::uint32_t pairCount() const noexcept;

private:
    struct TransparentHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    template <typename Value>
    using LookupTable = std::unordered_map<std::string, Value, TransparentHash, std::equal_to<>>;

    LookupTable<std::vector<KalshiLadderTargets>> kalshiMarkets;
    LookupTable<std::vector<std::uint32_t>> polymarketTokenLadders;
    std::uint32_t totalPairs;
};

}
