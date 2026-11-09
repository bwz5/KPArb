#include "matching/EntityMatchRule.h"

#include "matching/CandidateFinder.h"

#include <array>
#include <cstdlib>
#include <unordered_map>

namespace kparb {

namespace {

constexpr std::int64_t maximumDayDifference = 1;
constexpr std::int64_t secondsPerDay = 86'400;
constexpr std::int64_t secondsPerHour = 3'600;
constexpr std::int64_t maximumStartDifferenceSeconds = 3 * secondsPerHour;
constexpr std::size_t winnerMarketsPerEvent = 2;

std::int64_t daysFromCivil(std::int64_t year, std::int64_t month, std::int64_t day) noexcept
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yearOfEra = year - era * 400;
    const std::int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

std::int64_t floorDivide(std::int64_t value, std::int64_t divisor) noexcept
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

std::int64_t yearOfDay(std::int64_t day) noexcept
{
    std::int64_t year = 1970 + floorDivide(day, 366);
    while (daysFromCivil(year + 1, 1, 1) <= day) {
        ++year;
    }
    return year;
}

std::int64_t nthSundayOf(std::int64_t year, std::int64_t month, std::int64_t occurrence) noexcept
{
    const std::int64_t firstDay = daysFromCivil(year, month, 1);
    const std::int64_t weekday = ((firstDay + 4) % 7 + 7) % 7;
    return firstDay + (7 - weekday) % 7 + 7 * (occurrence - 1);
}

bool isDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

std::optional<std::int64_t> readNumber(std::string_view text) noexcept
{
    std::int64_t value = 0;
    for (const char character : text) {
        if (!isDigit(character)) {
            return std::nullopt;
        }
        value = value * 10 + (character - '0');
    }
    return value;
}

std::optional<std::int64_t> validDay(std::int64_t year, std::int64_t month, std::int64_t day) noexcept
{
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }
    return daysFromCivil(year, month, day);
}

std::string_view seriesOf(std::string_view ticker) noexcept
{
    return ticker.substr(0, ticker.find('-'));
}

bool endsWith(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool isGameWinnerSeries(std::string_view series) noexcept
{
    return endsWith(series, "GAME") || endsWith(series, "MATCH");
}

}

std::optional<std::int64_t> kalshiTickerDay(std::string_view ticker) noexcept
{
    const std::size_t separator = ticker.find('-');
    if (separator == std::string_view::npos || ticker.size() < separator + 8) {
        return std::nullopt;
    }
    const std::string_view date = ticker.substr(separator + 1, 7);
    static constexpr std::array<std::string_view, 12> monthNames{"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    std::int64_t month = 0;
    for (std::size_t index = 0; index < monthNames.size(); ++index) {
        if (date.substr(2, 3) == monthNames[index]) {
            month = static_cast<std::int64_t>(index) + 1;
        }
    }
    const auto year = readNumber(date.substr(0, 2));
    const auto day = readNumber(date.substr(5, 2));
    if (month == 0 || !year || !day) {
        return std::nullopt;
    }
    return validDay(2000 + *year, month, *day);
}

std::int64_t easternUtcOffsetSeconds(std::int64_t utcSeconds) noexcept
{
    const std::int64_t year = yearOfDay(floorDivide(utcSeconds, secondsPerDay));
    const std::int64_t daylightStart = nthSundayOf(year, 3, 2) * secondsPerDay + 7 * secondsPerHour;
    const std::int64_t daylightEnd = nthSundayOf(year, 11, 1) * secondsPerDay + 6 * secondsPerHour;
    return utcSeconds >= daylightStart && utcSeconds < daylightEnd ? -4 * secondsPerHour : -5 * secondsPerHour;
}

std::int64_t easternDay(std::int64_t utcSeconds) noexcept
{
    return floorDivide(utcSeconds + easternUtcOffsetSeconds(utcSeconds), secondsPerDay);
}

std::optional<std::int64_t> kalshiTickerStartSeconds(std::string_view ticker) noexcept
{
    const auto day = kalshiTickerDay(ticker);
    const std::size_t separator = ticker.find('-');
    if (!day || ticker.size() < separator + 12) {
        return std::nullopt;
    }
    const auto hour = readNumber(ticker.substr(separator + 8, 2));
    const auto minute = readNumber(ticker.substr(separator + 10, 2));
    if (!hour || !minute || *hour > 23 || *minute > 59) {
        return std::nullopt;
    }
    const std::int64_t localSeconds = *day * secondsPerDay + *hour * secondsPerHour + *minute * 60;
    return localSeconds - easternUtcOffsetSeconds(localSeconds + 5 * secondsPerHour);
}

std::optional<std::int64_t> polymarketSlugDay(std::string_view slug) noexcept
{
    if (slug.size() < 11 || slug[slug.size() - 11] != '-') {
        return std::nullopt;
    }
    const std::string_view date = slug.substr(slug.size() - 10);
    if (date[4] != '-' || date[7] != '-') {
        return std::nullopt;
    }
    const auto year = readNumber(date.substr(0, 4));
    const auto month = readNumber(date.substr(5, 2));
    const auto day = readNumber(date.substr(8, 2));
    if (!year || !month || !day) {
        return std::nullopt;
    }
    return validDay(*year, *month, *day);
}

std::unordered_set<std::string> kalshiEventsWithTieMarket(const std::vector<MarketInfo>& kalshiMarkets)
{
    std::unordered_set<std::string> events;
    for (const MarketInfo& market : kalshiMarkets) {
        if (endsWith(market.identifier, "-TIE")) {
            events.insert(market.eventIdentifier);
        }
    }
    return events;
}

bool isEntityResolvedMoneyline(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket, bool kalshiEventHasTie) noexcept
{
    if (kalshiEventHasTie || !isGameWinnerSeries(seriesOf(kalshiMarket.identifier))) {
        return false;
    }
    if (kalshiMarket.teamKeys.size() != 2 || kalshiMarket.teamKeys != polymarketMarket.teamKeys) {
        return false;
    }
    const std::string& yesEntity = polymarketMarket.yesOutcomeEntityKey;
    const std::string& noEntity = polymarketMarket.noOutcomeEntityKey;
    if (yesEntity.empty() || noEntity.empty() || yesEntity == noEntity) {
        return false;
    }
    const EntityPolarity polarity = entityPolarity(kalshiMarket, polymarketMarket);
    if (polarity != EntityPolarity::Aligned && polarity != EntityPolarity::Inverted) {
        return false;
    }
    const auto kalshiDay = kalshiTickerDay(kalshiMarket.identifier);
    const auto polymarketDay = polymarketSlugDay(polymarketMarket.eventIdentifier);
    if (!kalshiDay || !polymarketDay) {
        return false;
    }
    const std::int64_t polymarketStart = polymarketMarket.scheduledStartSeconds;
    if (polymarketStart == 0) {
        return std::llabs(*kalshiDay - *polymarketDay) <= maximumDayDifference;
    }
    if (const auto kalshiStart = kalshiTickerStartSeconds(kalshiMarket.identifier)) {
        return std::llabs(*kalshiStart - polymarketStart) <= maximumStartDifferenceSeconds;
    }
    return easternDay(polymarketStart) == *kalshiDay;
}

std::vector<bool> findEntityRuleMatches(const std::vector<MarketInfo>& kalshiMarkets, const std::vector<MarketInfo>& polymarketMarkets,
    const std::vector<CandidatePair>& candidates)
{
    const std::unordered_set<std::string> eventsWithTie = kalshiEventsWithTieMarket(kalshiMarkets);
    std::unordered_map<std::string, std::size_t> marketsPerEvent;
    for (const MarketInfo& market : kalshiMarkets) {
        ++marketsPerEvent[market.eventIdentifier];
    }
    std::vector<bool> matches(candidates.size(), false);
    std::unordered_map<std::size_t, std::size_t> matchesPerKalshiMarket;
    std::unordered_map<std::string, std::size_t> matchesPerPolymarketSubject;
    const auto subjectKeyOf = [&](std::size_t index) {
        return std::to_string(candidates[index].polymarketIndex) + "|" + kalshiMarkets[candidates[index].kalshiIndex].subjectTeamKey;
    };
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const MarketInfo& kalshiMarket = kalshiMarkets[candidates[index].kalshiIndex];
        const bool hasTie = eventsWithTie.contains(kalshiMarket.eventIdentifier);
        const bool isTwoMarketEvent = marketsPerEvent[kalshiMarket.eventIdentifier] == winnerMarketsPerEvent;
        if (isTwoMarketEvent && isEntityResolvedMoneyline(kalshiMarket, polymarketMarkets[candidates[index].polymarketIndex], hasTie)) {
            matches[index] = true;
            ++matchesPerKalshiMarket[candidates[index].kalshiIndex];
            ++matchesPerPolymarketSubject[subjectKeyOf(index)];
        }
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (matches[index]
            && (matchesPerKalshiMarket[candidates[index].kalshiIndex] != 1 || matchesPerPolymarketSubject[subjectKeyOf(index)] != 1)) {
            matches[index] = false;
        }
    }
    return matches;
}

bool haveConflictingDates(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket) noexcept
{
    const auto kalshiDay = kalshiTickerDay(kalshiMarket.identifier);
    const auto polymarketDay = polymarketSlugDay(polymarketMarket.eventIdentifier);
    return kalshiDay && polymarketDay && std::llabs(*kalshiDay - *polymarketDay) > maximumDayDifference;
}

MatchVerdict entityRuleVerdict(const MarketInfo& kalshiMarket, const MarketInfo& polymarketMarket)
{
    MatchVerdict verdict;
    verdict.isSameEvent = true;
    verdict.isInverted = entityPolarity(kalshiMarket, polymarketMarket) == EntityPolarity::Inverted;
    verdict.confidence = 1.0;
    verdict.reason = "Both venues name the same two teams on the same date in a two-way winner market";
    verdict.model = "entity-rule";
    return verdict;
}

}
