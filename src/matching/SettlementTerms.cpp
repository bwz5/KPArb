#include "matching/SettlementTerms.h"

#include <array>
#include <initializer_list>
#include <regex>
#include <string_view>
#include <utility>
#include <vector>

namespace kparb {

namespace {

std::string lowercase(std::string_view text)
{
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

bool containsAny(std::string_view text, std::initializer_list<std::string_view> phrases)
{
    for (const std::string_view phrase : phrases) {
        if (text.find(phrase) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

struct ScopePattern {
    std::regex pattern;
    std::string scope;
};

const std::vector<ScopePattern>& scopePatterns()
{
    static const std::vector<ScopePattern> patterns = [] {
        const std::array<std::pair<const char*, const char*>, 18> definitions{{
            {R"(\bmap\s*1\b)", "map 1"},
            {R"(\bmap\s*2\b)", "map 2"},
            {R"(\bmap\s*3\b)", "map 3"},
            {R"(\bgame\s*1\s+winner\b)", "game 1"},
            {R"(\bgame\s*2\s+winner\b)", "game 2"},
            {R"(\b(first|1st)\s+half\b|\b1h\b)", "first half"},
            {R"(\b(second|2nd)\s+half\b|\b2h\b)", "second half"},
            {R"(\b(first|1st)\s+quarter\b|\b1q\b)", "first quarter"},
            {R"(\b(second|2nd)\s+quarter\b|\b2q\b)", "second quarter"},
            {R"(\b(third|3rd)\s+quarter\b|\b3q\b)", "third quarter"},
            {R"(\b(fourth|4th)\s+quarter\b|\b4q\b)", "fourth quarter"},
            {R"(\b(first|1st)\s+period\b)", "first period"},
            {R"(\b(second|2nd)\s+period\b)", "second period"},
            {R"(\b(third|3rd)\s+period\b)", "third period"},
            {R"(\b(first|1st)\s+set\b|\bset\s*1\b)", "first set"},
            {R"(\b(second|2nd)\s+set\b|\bset\s*2\b)", "second set"},
            {R"(\b(first|1st)\s+(five|5)\s+innings\b|\bf5\b)", "first five innings"},
            {R"(\b(first|1st)\s+inning\b)", "first inning"},
        }};
        std::vector<ScopePattern> compiled;
        for (const auto& [pattern, scope] : definitions) {
            compiled.push_back({std::regex(pattern, std::regex::ECMAScript | std::regex::optimize), scope});
        }
        return compiled;
    }();
    return patterns;
}

TimeScope timeScopeOf(std::string_view rules)
{
    if (containsAny(rules, {"90 minutes", "regulation time", "regular time", "reg time", "end of regulation", "does not include overtime",
            "not including overtime", "excluding overtime", "excludes overtime", "does not include extra time"})) {
        return TimeScope::RegulationOnly;
    }
    if (containsAny(rules, {"including any overtime", "including overtime", "includes overtime", "overtime is included",
            "overtime periods", "including extra time", "includes extra time", "after extra time", "shootout"})) {
        return TimeScope::IncludesOvertime;
    }
    return TimeScope::Unknown;
}

NoResultSettlement settlementOfSentence(std::string_view sentence)
{
    if (containsAny(sentence, {"fair price", "fair market price", "fair market value"})) {
        return NoResultSettlement::FairPrice;
    }
    if (containsAny(sentence, {"50-50", "50/50", "fifty-fifty"})) {
        return NoResultSettlement::FiftyFifty;
    }
    if (containsAny(sentence, {"resolve to \"no\"", "resolve to no", "resolves to \"no\"", "resolve \"no\""})) {
        return NoResultSettlement::ResolvesNo;
    }
    if (containsAny(sentence, {"resolve to \"yes\"", "resolve to yes", "resolves to \"yes\"", "resolve \"yes\""})) {
        return NoResultSettlement::ResolvesYes;
    }
    return NoResultSettlement::Unknown;
}

NoResultSettlement noResultOf(std::string_view rules)
{
    std::size_t start = 0;
    while (start < rules.size()) {
        std::size_t end = rules.find_first_of(".\n", start);
        if (end == std::string_view::npos) {
            end = rules.size();
        }
        const std::string_view sentence = rules.substr(start, end - start);
        if (sentence.find("cancel") != std::string_view::npos) {
            const NoResultSettlement settlement = settlementOfSentence(sentence);
            if (settlement != NoResultSettlement::Unknown) {
                return settlement;
            }
        }
        start = end + 1;
    }
    return NoResultSettlement::Unknown;
}

const char* timeScopeName(TimeScope scope) noexcept
{
    switch (scope) {
    case TimeScope::RegulationOnly:
        return "regulation time only";
    case TimeScope::IncludesOvertime:
        return "including overtime";
    case TimeScope::Unknown:
        break;
    }
    return "unstated";
}

}

std::string partialScopeOf(const std::string& text)
{
    const std::string lowered = lowercase(text);
    for (const ScopePattern& pattern : scopePatterns()) {
        if (std::regex_search(lowered, pattern.pattern)) {
            return pattern.scope;
        }
    }
    return {};
}

SettlementTerms readSettlementTerms(const MarketInfo& market)
{
    SettlementTerms terms;
    terms.partialScope = partialScopeOf(market.title + " \n " + market.subtitle);
    const std::string rules = lowercase(market.rules);
    terms.timeScope = timeScopeOf(rules);
    terms.noResult = noResultOf(rules);
    return terms;
}

SettlementComparison compareSettlementTerms(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket)
{
    const SettlementTerms kalshi = readSettlementTerms(kalshiMarket);
    const SettlementTerms polymarket = readSettlementTerms(polymarketMarket);
    SettlementComparison comparison;
    if (kalshi.partialScope != polymarket.partialScope) {
        comparison.isCompatible = false;
        comparison.conflict = "Kalshi settles on " + (kalshi.partialScope.empty() ? std::string("the full event") : kalshi.partialScope)
            + " but Polymarket settles on " + (polymarket.partialScope.empty() ? std::string("the full event") : polymarket.partialScope);
        return comparison;
    }
    const bool scopesDisagree = kalshi.timeScope != TimeScope::Unknown && polymarket.timeScope != TimeScope::Unknown
        && kalshi.timeScope != polymarket.timeScope;
    if (scopesDisagree) {
        comparison.isCompatible = false;
        comparison.conflict = std::string("Kalshi counts ") + timeScopeName(kalshi.timeScope) + " but Polymarket counts "
            + timeScopeName(polymarket.timeScope);
        return comparison;
    }
    comparison.carriesNoResultRisk = kalshi.noResult == NoResultSettlement::FairPrice;
    switch (polymarket.noResult) {
    case NoResultSettlement::FiftyFifty:
        comparison.polymarketYesNoResultValue = 0.5;
        comparison.polymarketNoNoResultValue = 0.5;
        comparison.polymarketPaysEvenOnNoResult = true;
        break;
    case NoResultSettlement::ResolvesNo:
        comparison.polymarketNoNoResultValue = 1.0;
        break;
    case NoResultSettlement::ResolvesYes:
        comparison.polymarketYesNoResultValue = 1.0;
        break;
    case NoResultSettlement::FairPrice:
    case NoResultSettlement::Unknown:
        break;
    }
    return comparison;
}

}
