#include "ingestion/LadderDirectory.h"

#include "arbitrage/LadderUpdate.h"

namespace kparb {

LadderDirectory::LadderDirectory(const std::vector<PairSubscription>& subscriptions)
    : totalPairs(static_cast<std::uint32_t>(subscriptions.size()))
{
    for (std::uint32_t pairIndex = 0; pairIndex < totalPairs; ++pairIndex) {
        const PairSubscription& subscription = subscriptions[pairIndex];
        kalshiMarkets[subscription.kalshiTicker].push_back(KalshiLadderTargets{
            ladderIndexFor(pairIndex, LadderSlot::KalshiBuyYes),
            ladderIndexFor(pairIndex, LadderSlot::KalshiBuyNo),
        });
        polymarketTokenLadders[subscription.polymarketYesEquivalentToken].push_back(
            ladderIndexFor(pairIndex, LadderSlot::PolymarketBuyYes));
        polymarketTokenLadders[subscription.polymarketNoEquivalentToken].push_back(
            ladderIndexFor(pairIndex, LadderSlot::PolymarketBuyNo));
    }
}

const std::vector<KalshiLadderTargets>* LadderDirectory::findKalshiMarket(std::string_view ticker) const noexcept
{
    const auto found = kalshiMarkets.find(ticker);
    return found == kalshiMarkets.end() ? nullptr : &found->second;
}

const std::vector<std::uint32_t>* LadderDirectory::findPolymarketToken(std::string_view tokenIdentifier) const noexcept
{
    const auto found = polymarketTokenLadders.find(tokenIdentifier);
    return found == polymarketTokenLadders.end() ? nullptr : &found->second;
}

std::vector<std::string> LadderDirectory::kalshiTickers() const
{
    std::vector<std::string> tickers;
    tickers.reserve(kalshiMarkets.size());
    for (const auto& entry : kalshiMarkets) {
        tickers.push_back(entry.first);
    }
    return tickers;
}

std::vector<std::string> LadderDirectory::polymarketTokens() const
{
    std::vector<std::string> tokens;
    tokens.reserve(polymarketTokenLadders.size());
    for (const auto& entry : polymarketTokenLadders) {
        tokens.push_back(entry.first);
    }
    return tokens;
}

std::uint32_t LadderDirectory::pairCount() const noexcept
{
    return totalPairs;
}

}
