#pragma once

#include "ingestion/MarketInfo.h"
#include "matching/CandidateFinder.h"
#include "matching/MatchedPair.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace kparb {

std::optional<std::int64_t> kalshiTickerDay(std::string_view ticker) noexcept;
std::optional<std::int64_t> polymarketSlugDay(std::string_view slug) noexcept;
std::optional<std::int64_t> kalshiTickerStartSeconds(std::string_view ticker) noexcept;
std::int64_t easternUtcOffsetSeconds(std::int64_t utcSeconds) noexcept;
std::int64_t easternDay(std::int64_t utcSeconds) noexcept;

std::unordered_set<std::string> kalshiEventsWithTieMarket(const std::vector<MarketInfo>& kalshiMarkets);

bool isEntityResolvedMoneyline(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket, bool kalshiEventHasTie) noexcept;

std::vector<bool> findEntityRuleMatches(const std::vector<MarketInfo>& kalshiMarkets, const std::vector<MarketInfo>& polymarketMarkets,
    const std::vector<CandidatePair>& candidates);

bool haveConflictingDates(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket) noexcept;

MatchVerdict entityRuleVerdict(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket);

}
