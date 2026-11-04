#include "ingestion/EntityDirectory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace kparb {

namespace {

struct TextWord {
    std::size_t begin;
    std::size_t end;
    std::string lowercase;
};

constexpr std::size_t maximumSeparatorLength = 3;
constexpr std::size_t ambiguousEntity = static_cast<std::size_t>(-1);

bool isAsciiDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

bool isAsciiLetter(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool isWordCharacter(char character) noexcept
{
    return isAsciiLetter(character) || isAsciiDigit(character) || static_cast<unsigned char>(character) >= 0x80;
}

char toLowercase(char character) noexcept
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

std::vector<TextWord> splitWords(std::string_view text)
{
    std::vector<TextWord> words;
    std::size_t position = 0;
    while (position < text.size()) {
        if (!isWordCharacter(text[position])) {
            ++position;
            continue;
        }
        TextWord word{position, position, {}};
        while (word.end < text.size() && isWordCharacter(text[word.end])) {
            word.lowercase += toLowercase(text[word.end]);
            ++word.end;
        }
        position = word.end;
        words.push_back(std::move(word));
    }
    return words;
}

std::vector<std::string> lowercaseWords(std::string_view text)
{
    std::vector<std::string> words;
    for (TextWord& word : splitWords(text)) {
        words.push_back(std::move(word.lowercase));
    }
    return words;
}

std::string joinWords(const std::vector<std::string>& words)
{
    std::string joined;
    for (const std::string& word : words) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += word;
    }
    return joined;
}

bool startsWith(std::string_view text, std::string_view prefix) noexcept
{
    return !prefix.empty() && text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

struct AliasTarget {
    std::size_t entityIndex;
    char trailingCharacter;
};

void buildAliasIndex(EntityLeague& league, const std::map<std::string, AliasTarget>& entityOfAlias)
{
    for (const auto& [joined, target] : entityOfAlias) {
        if (target.entityIndex == ambiguousEntity) {
            continue;
        }
        EntityAlias alias{lowercaseWords(joined), target.entityIndex, target.trailingCharacter};
        league.aliasesByFirstWord[alias.words.front()].push_back(std::move(alias));
    }
    for (auto& [firstWord, aliases] : league.aliasesByFirstWord) {
        std::sort(aliases.begin(), aliases.end(),
            [](const EntityAlias& left, const EntityAlias& right) { return left.words.size() > right.words.size(); });
    }
}

}

EntityDirectory EntityDirectory::parse(const std::string& json)
{
    EntityDirectory directory;
    const auto document = nlohmann::json::parse(json);
    const nlohmann::json leagueList = document.value("leagues", nlohmann::json::array());
    for (const auto& leagueObject : leagueList) {
        EntityLeague league;
        league.name = leagueObject.value("name", "");
        league.kalshiSeriesPrefix = leagueObject.value("kalshiSeriesPrefix", "");
        league.polymarketSlugPrefix = leagueObject.value("polymarketSlugPrefix", "");
        std::map<std::string, AliasTarget> entityOfAlias;
        const nlohmann::json entityList = leagueObject.value("entities", nlohmann::json::array());
        for (const auto& entityObject : entityList) {
            Entity entity{entityObject.value("code", ""), entityObject.value("name", "")};
            if (entity.code.empty() || entity.name.empty() || league.entityIndexByCode.count(entity.code) > 0) {
                continue;
            }
            const std::size_t entityIndex = league.entities.size();
            league.entityIndexByCode.emplace(entity.code, entityIndex);
            std::vector<std::string> names{entity.name};
            const nlohmann::json aliases = entityObject.value("aliases", nlohmann::json::array());
            for (const auto& alias : aliases) {
                names.push_back(alias.get<std::string>());
            }
            for (const std::string& name : names) {
                const std::string joined = joinWords(lowercaseWords(name));
                if (joined.empty()) {
                    continue;
                }
                const char trailingCharacter = isWordCharacter(name.back()) ? char{0} : name.back();
                const auto [entry, inserted] = entityOfAlias.emplace(joined, AliasTarget{entityIndex, trailingCharacter});
                if (!inserted && entry->second.entityIndex != entityIndex) {
                    entry->second.entityIndex = ambiguousEntity;
                } else if (!inserted && entry->second.trailingCharacter == 0) {
                    entry->second.trailingCharacter = trailingCharacter;
                }
            }
            league.entities.push_back(std::move(entity));
        }
        if (league.name.empty()) {
            continue;
        }
        buildAliasIndex(league, entityOfAlias);
        directory.leagues.push_back(std::move(league));
    }
    return directory;
}

EntityDirectory EntityDirectory::load(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return EntityDirectory{};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

std::size_t EntityDirectory::leagueCount() const noexcept
{
    return leagues.size();
}

std::size_t EntityDirectory::entityCount() const noexcept
{
    std::size_t count = 0;
    for (const EntityLeague& league : leagues) {
        count += league.entities.size();
    }
    return count;
}

const EntityLeague* EntityDirectory::leagueOf(const MarketInfo& market) const noexcept
{
    const EntityLeague* best = nullptr;
    std::size_t bestLength = 0;
    for (const EntityLeague& league : leagues) {
        const std::string& prefix = market.venue == Venue::Kalshi ? league.kalshiSeriesPrefix : league.polymarketSlugPrefix;
        if (prefix.size() > bestLength && startsWith(market.eventIdentifier, prefix)) {
            best = &league;
            bestLength = prefix.size();
        }
    }
    return best;
}

CanonicalText EntityDirectory::canonicalize(const EntityLeague& league, std::string_view text) const
{
    CanonicalText result;
    if (league.aliasesByFirstWord.empty()) {
        result.text = std::string(text);
        return result;
    }
    const std::vector<TextWord> words = splitWords(text);
    std::size_t copiedUntil = 0;
    std::size_t wordIndex = 0;
    while (wordIndex < words.size()) {
        const auto found = league.aliasesByFirstWord.find(words[wordIndex].lowercase);
        const EntityAlias* matched = nullptr;
        if (found != league.aliasesByFirstWord.end()) {
            for (const EntityAlias& alias : found->second) {
                const std::size_t lastIndex = wordIndex + alias.words.size() - 1;
                if (lastIndex >= words.size()) {
                    continue;
                }
                bool isMatch = true;
                for (std::size_t offset = 1; offset < alias.words.size() && isMatch; ++offset) {
                    const TextWord& previous = words[wordIndex + offset - 1];
                    const TextWord& current = words[wordIndex + offset];
                    isMatch = current.lowercase == alias.words[offset] && current.begin - previous.end <= maximumSeparatorLength;
                }
                if (isMatch) {
                    matched = &alias;
                    break;
                }
            }
        }
        if (matched == nullptr) {
            ++wordIndex;
            continue;
        }
        const std::size_t lastIndex = wordIndex + matched->words.size() - 1;
        result.text.append(text.substr(copiedUntil, words[wordIndex].begin - copiedUntil));
        result.text += league.entities[matched->entityIndex].name;
        copiedUntil = words[lastIndex].end;
        if (matched->trailingCharacter != 0 && copiedUntil < text.size() && text[copiedUntil] == matched->trailingCharacter) {
            ++copiedUntil;
        }
        if (std::find(result.mentionedEntities.begin(), result.mentionedEntities.end(), matched->entityIndex) == result.mentionedEntities.end()) {
            result.mentionedEntities.push_back(matched->entityIndex);
        }
        wordIndex = lastIndex + 1;
    }
    result.text.append(text.substr(copiedUntil));
    return result;
}

std::string EntityDirectory::keyFor(const EntityLeague& league, std::size_t entityIndex)
{
    return league.name + ":" + league.entities[entityIndex].code;
}

const std::size_t* EntityDirectory::findCode(const EntityLeague& league, std::string_view code)
{
    const auto found = league.entityIndexByCode.find(std::string(code));
    return found == league.entityIndexByCode.end() ? nullptr : &found->second;
}

std::vector<std::size_t> EntityDirectory::kalshiEventEntities(const EntityLeague& league, std::string_view eventTicker)
{
    const std::size_t dash = eventTicker.find('-');
    if (dash == std::string_view::npos) {
        return {};
    }
    std::string_view codes = eventTicker.substr(dash + 1);
    codes = codes.substr(0, codes.find('-'));
    constexpr std::size_t dateLength = 7;
    if (codes.size() <= dateLength || !isAsciiDigit(codes[0]) || !isAsciiDigit(codes[1]) || !isAsciiLetter(codes[2])) {
        return {};
    }
    codes.remove_prefix(dateLength);
    while (!codes.empty() && isAsciiDigit(codes.front())) {
        codes.remove_prefix(1);
    }
    for (std::size_t split = 1; split < codes.size(); ++split) {
        const std::size_t* first = findCode(league, codes.substr(0, split));
        const std::size_t* second = findCode(league, codes.substr(split));
        if (first != nullptr && second != nullptr) {
            return {*first, *second};
        }
    }
    return {};
}

std::string EntityDirectory::kalshiSubjectKey(const EntityLeague& league, std::string_view marketTicker)
{
    const std::string_view suffix = marketTicker.substr(marketTicker.rfind('-') + 1);
    for (std::size_t length = suffix.size(); length > 0; --length) {
        const std::string_view rest = suffix.substr(length);
        if (!std::all_of(rest.begin(), rest.end(), isAsciiDigit)) {
            continue;
        }
        if (const std::size_t* entityIndex = findCode(league, suffix.substr(0, length))) {
            return keyFor(league, *entityIndex);
        }
    }
    return {};
}

void EntityDirectory::annotate(MarketInfo& market) const
{
    const EntityLeague* league = leagueOf(market);
    if (league == nullptr) {
        return;
    }
    market.entityLeague = league->name;
    std::vector<std::size_t> mentioned;
    std::vector<std::size_t> subjectMentions;
    for (std::string* field : {&market.subtitle, &market.title, &market.eventTitle, &market.rulesSummary, &market.yesOutcomeName,
             &market.noOutcomeName}) {
        CanonicalText canonical = canonicalize(*league, *field);
        if (subjectMentions.empty() && (field == &market.subtitle || field == &market.title)) {
            subjectMentions = canonical.mentionedEntities;
        }
        mentioned.insert(mentioned.end(), canonical.mentionedEntities.begin(), canonical.mentionedEntities.end());
        *field = std::move(canonical.text);
    }
    market.rules = canonicalize(*league, market.rules).text;
    if (market.venue == Venue::Kalshi) {
        const std::vector<std::size_t> eventEntities = kalshiEventEntities(*league, market.eventIdentifier);
        mentioned.insert(mentioned.end(), eventEntities.begin(), eventEntities.end());
        market.subjectTeamKey = kalshiSubjectKey(*league, market.identifier);
    } else {
        market.subjectTeamKey = subjectMentions.size() == 1 ? keyFor(*league, subjectMentions.front()) : std::string{};
    }
    market.teamKeys.clear();
    for (const std::size_t entityIndex : mentioned) {
        market.teamKeys.push_back(keyFor(*league, entityIndex));
    }
    std::sort(market.teamKeys.begin(), market.teamKeys.end());
    market.teamKeys.erase(std::unique(market.teamKeys.begin(), market.teamKeys.end()), market.teamKeys.end());
}

void EntityDirectory::annotate(std::vector<MarketInfo>& markets) const
{
    if (leagues.empty()) {
        return;
    }
    for (MarketInfo& market : markets) {
        annotate(market);
    }
}

}
