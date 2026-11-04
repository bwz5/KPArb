#include "ingestion/MarketClassification.h"

#include <array>
#include <utility>

namespace kparb {

namespace {

bool endsWith(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool startsWith(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string_view kalshiPeriodPrefix(std::string_view series) noexcept
{
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> periods{{
        {"1H", "first_half_"},
        {"2H", "second_half_"},
        {"1Q", "q1_"},
        {"2Q", "q2_"},
        {"3Q", "q3_"},
        {"4Q", "q4_"},
    }};
    for (const auto& [marker, prefix] : periods) {
        if (series.find(marker) != std::string_view::npos) {
            return prefix;
        }
    }
    return {};
}

}

std::string seriesOfKalshiEvent(std::string_view eventTicker)
{
    return std::string(eventTicker.substr(0, eventTicker.find('-')));
}

std::string kalshiMarketType(std::string_view eventTicker)
{
    const std::string series = seriesOfKalshiEvent(eventTicker);
    std::string_view base;
    if (endsWith(series, "TEAMTOTAL")) {
        base = "team_total";
    } else if (endsWith(series, "SPREAD")) {
        base = "spread";
    } else if (endsWith(series, "TOTAL")) {
        base = "total";
    } else if (endsWith(series, "GAME") || endsWith(series, "MATCH")) {
        base = "winner";
    } else if (endsWith(series, "SCORE")) {
        base = "exact_score";
    } else if (endsWith(series, "BTTS")) {
        base = "both_teams_to_score";
    } else {
        return {};
    }
    return std::string(kalshiPeriodPrefix(series)) + std::string(base);
}

std::string polymarketMarketType(std::string_view sportsMarketType)
{
    std::string_view type = sportsMarketType;
    if (startsWith(type, "soccer_")) {
        type.remove_prefix(7);
    }
    if (type == "moneyline") {
        return "winner";
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 3> plurals{{
        {"team_totals", "team_total"},
        {"spreads", "spread"},
        {"totals", "total"},
    }};
    for (const auto& [plural, singular] : plurals) {
        if (endsWith(type, plural)) {
            return std::string(type.substr(0, type.size() - plural.size())) + std::string(singular);
        }
    }
    return std::string(type);
}

bool areMarketTypesCompatible(std::string_view kalshiType, std::string_view polymarketType) noexcept
{
    return kalshiType.empty() || kalshiType == polymarketType;
}

}
