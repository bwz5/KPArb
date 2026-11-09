#pragma once

#include "ingestion/MarketInfo.h"

#include <cstdint>
#include <string>

namespace kparb {

enum class TimeScope : std::uint8_t { Unknown, RegulationOnly, IncludesOvertime };

enum class NoResultSettlement : std::uint8_t { Unknown, FairPrice, FiftyFifty, ResolvesNo, ResolvesYes };

struct SettlementTerms {
    std::string partialScope;
    TimeScope timeScope = TimeScope::Unknown;
    NoResultSettlement noResult = NoResultSettlement::Unknown;
};

struct SettlementComparison {
    bool isCompatible = true;
    std::string conflict;
    bool carriesNoResultRisk = false;
    bool polymarketPaysEvenOnNoResult = false;
    double polymarketYesNoResultValue = 0.0;
    double polymarketNoNoResultValue = 0.0;
};

SettlementTerms readSettlementTerms(const MarketInfo& market);

SettlementComparison compareSettlementTerms(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket);

std::string partialScopeOf(const std::string& text);

}
