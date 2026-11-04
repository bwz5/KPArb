#include "ingestion/TeamDirectory.h"

#include "ingestion/MarketClassification.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace kparb {

namespace {

bool isAsciiLetter(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool isAsciiDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

bool isWordCharacter(char character) noexcept
{
    return isAsciiLetter(character) || isAsciiDigit(character);
}

std::string toLowercase(std::string_view text)
{
    std::string lowercase(text);
    for (char& character : lowercase) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return lowercase;
}

bool containsWord(std::string_view text, std::string_view word) noexcept
{
    for (std::size_t position = text.find(word); position != std::string_view::npos; position = text.find(word, position + 1)) {
        const bool startsWord = position == 0 || !isWordCharacter(text[position - 1]);
        const std::size_t end = position + word.size();
        const bool endsWord = end == text.size() || !isWordCharacter(text[end]);
        if (startsWord && endsWord) {
            return true;
        }
    }
    return false;
}

std::string_view teamCodesOfKalshiEvent(std::string_view eventTicker) noexcept
{
    const std::size_t dash = eventTicker.find('-');
    if (dash == std::string_view::npos) {
        return {};
    }
    std::string_view rest = eventTicker.substr(dash + 1);
    rest = rest.substr(0, rest.find('-'));
    constexpr std::size_t dateLength = 7;
    if (rest.size() <= dateLength || !isAsciiDigit(rest[0]) || !isAsciiDigit(rest[1]) || !isAsciiLetter(rest[2])) {
        return {};
    }
    rest.remove_prefix(dateLength);
    while (!rest.empty() && isAsciiDigit(rest.front())) {
        rest.remove_prefix(1);
    }
    return rest;
}

}

TeamDirectory TeamDirectory::parse(const std::string& json)
{
    TeamDirectory directory;
    const auto document = nlohmann::json::parse(json);
    const nlohmann::json leagueList = document.value("leagues", nlohmann::json::array());
    for (const auto& leagueObject : leagueList) {
        LeagueTeams league;
        league.name = leagueObject.value("name", "");
        league.kalshiSeriesPrefix = leagueObject.value("kalshiSeriesPrefix", "");
        league.polymarketSlugPrefix = leagueObject.value("polymarketSlugPrefix", "");
        const nlohmann::json teams = leagueObject.value("teams", nlohmann::json::object());
        for (const auto& [code, names] : teams.items()) {
            TeamNames team{code, {}};
            for (const auto& name : names) {
                team.lowercaseNames.push_back(toLowercase(name.get<std::string>()));
            }
            league.teams.push_back(std::move(team));
        }
        if (!league.name.empty()) {
            directory.leagues.push_back(std::move(league));
        }
    }
    return directory;
}

TeamDirectory TeamDirectory::load(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return TeamDirectory{};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

std::size_t TeamDirectory::leagueCount() const noexcept
{
    return leagues.size();
}

std::string TeamDirectory::keyFor(const LeagueTeams& league, std::string_view code)
{
    return league.name + ":" + std::string(code);
}

bool TeamDirectory::isTeamCode(const LeagueTeams& league, std::string_view code) noexcept
{
    return std::any_of(league.teams.begin(), league.teams.end(), [code](const TeamNames& team) { return team.code == code; });
}

const LeagueTeams* TeamDirectory::leagueOf(const MarketInfo& market) const noexcept
{
    for (const LeagueTeams& league : leagues) {
        const std::string& prefix = market.venue == Venue::Kalshi ? league.kalshiSeriesPrefix : league.polymarketSlugPrefix;
        if (!prefix.empty() && market.eventIdentifier.rfind(prefix, 0) == 0) {
            return &league;
        }
    }
    return nullptr;
}

std::vector<std::string> TeamDirectory::mentionedTeamKeys(const LeagueTeams& league, const std::string& text)
{
    const std::string lowercase = toLowercase(text);
    std::vector<std::string> keys;
    for (const TeamNames& team : league.teams) {
        const bool isMentioned = std::any_of(team.lowercaseNames.begin(), team.lowercaseNames.end(),
            [&lowercase](const std::string& name) { return containsWord(lowercase, name); });
        if (isMentioned) {
            keys.push_back(keyFor(league, team.code));
        }
    }
    return keys;
}

std::vector<std::string> TeamDirectory::kalshiTeamKeys(const MarketInfo& market) const
{
    const LeagueTeams* league = leagueOf(market);
    if (league == nullptr) {
        return {};
    }
    const std::string_view codes = teamCodesOfKalshiEvent(market.eventIdentifier);
    for (std::size_t split = 2; split + 2 <= codes.size(); ++split) {
        const std::string_view first = codes.substr(0, split);
        const std::string_view second = codes.substr(split);
        if (isTeamCode(*league, first) && isTeamCode(*league, second)) {
            return {keyFor(*league, first), keyFor(*league, second)};
        }
    }
    return {};
}

std::vector<std::string> TeamDirectory::polymarketTeamKeys(const MarketInfo& market) const
{
    const LeagueTeams* league = leagueOf(market);
    if (league == nullptr) {
        return {};
    }
    return mentionedTeamKeys(*league, market.title + " | " + market.subtitle + " | " + market.eventTitle + " | "
            + market.yesOutcomeName + " | " + market.noOutcomeName);
}

std::vector<std::string> TeamDirectory::teamKeysFor(const MarketInfo& market) const
{
    std::vector<std::string> keys = market.venue == Venue::Kalshi ? kalshiTeamKeys(market) : polymarketTeamKeys(market);
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::string TeamDirectory::subjectTeamKeyFor(const MarketInfo& market) const
{
    const LeagueTeams* league = leagueOf(market);
    if (league == nullptr) {
        return {};
    }
    if (market.venue == Venue::Kalshi) {
        std::string_view suffix = market.identifier;
        suffix = suffix.substr(suffix.rfind('-') + 1);
        std::size_t letters = 0;
        while (letters < suffix.size() && isAsciiLetter(suffix[letters])) {
            ++letters;
        }
        const std::string_view code = suffix.substr(0, letters);
        return isTeamCode(*league, code) ? keyFor(*league, code) : std::string{};
    }
    for (const std::string* text : {&market.subtitle, &market.title}) {
        const std::vector<std::string> mentioned = mentionedTeamKeys(*league, *text);
        if (!mentioned.empty()) {
            return mentioned.size() == 1 ? mentioned.front() : std::string{};
        }
    }
    return {};
}

void TeamDirectory::annotate(std::vector<MarketInfo>& markets) const
{
    if (leagues.empty()) {
        return;
    }
    for (MarketInfo& market : markets) {
        market.teamKeys = teamKeysFor(market);
        market.subjectTeamKey = subjectTeamKeyFor(market);
    }
}

}
