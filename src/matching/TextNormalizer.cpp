#include "matching/TextNormalizer.h"

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace kparb {

namespace {

bool isDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

bool isLetter(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

char toLower(char character) noexcept
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

const std::unordered_set<std::string_view>& stopWords()
{
    static const std::unordered_set<std::string_view> words{
        "a", "an", "the", "will", "be", "is", "are", "was", "of", "in", "on", "at", "to", "for", "by", "and", "or",
        "with", "as", "this", "that", "it", "its", "if", "do", "does", "any", "market", "question", "resolve",
        "resolves", "yes", "no", "who", "what", "which", "how", "s", "vs", "v", "from", "into", "than", "then", "there",
        "their", "his", "her", "he", "she", "they", "we", "you", "i",
    };
    return words;
}

const std::unordered_map<std::string_view, std::string_view>& canonicalWords()
{
    static const std::unordered_map<std::string_view, std::string_view> words{
        {"jan", "january"}, {"feb", "february"}, {"mar", "march"}, {"apr", "april"}, {"jun", "june"},
        {"jul", "july"}, {"aug", "august"}, {"sep", "september"}, {"sept", "september"}, {"oct", "october"},
        {"nov", "november"}, {"dec", "december"}, {"btc", "bitcoin"}, {"eth", "ethereum"}, {"fed", "federal"},
        {"us", "united"}, {"usa", "united"}, {"gop", "republican"}, {"dem", "democratic"}, {"democrat", "democratic"},
        {"democrats", "democratic"}, {"republicans", "republican"}, {"pres", "president"}, {"presidential", "president"},
        {"wins", "win"}, {"winner", "win"}, {"won", "win"}, {"elected", "election"}, {"rates", "rate"},
    };
    return words;
}

std::string collapseNumberFormatting(std::string_view text)
{
    std::string collapsed;
    collapsed.reserve(text.size());
    for (std::size_t position = 0; position < text.size(); ++position) {
        const char character = text[position];
        const bool isBetweenDigits = position > 0 && position + 1 < text.size() && isDigit(text[position - 1]) && isDigit(text[position + 1]);
        if (character == ',' && isBetweenDigits) {
            continue;
        }
        if ((character == 'k' || character == 'K') && position > 0 && isDigit(text[position - 1])
            && (position + 1 == text.size() || !isLetter(text[position + 1]))) {
            collapsed += "000";
            continue;
        }
        collapsed.push_back(toLower(character));
    }
    return collapsed;
}

}

bool isNumericToken(std::string_view token) noexcept
{
    if (token.empty()) {
        return false;
    }
    bool sawDigit = false;
    for (const char character : token) {
        if (isDigit(character)) {
            sawDigit = true;
        } else if (character != '.') {
            return false;
        }
    }
    return sawDigit;
}

bool isYearToken(std::string_view token) noexcept
{
    if (token.size() != 4 || !isNumericToken(token)) {
        return false;
    }
    return token >= "2000" && token <= "2100";
}

std::vector<std::string> tokenizeForMatching(std::string_view text)
{
    const std::string collapsed = collapseNumberFormatting(text);
    std::vector<std::string> tokens;
    std::string current;
    const auto finishToken = [&tokens, &current] {
        while (!current.empty() && current.back() == '.') {
            current.pop_back();
        }
        if (current.empty()) {
            return;
        }
        if (isNumericToken(current) && current.find('.') != std::string::npos) {
            while (current.back() == '0') {
                current.pop_back();
            }
            if (current.back() == '.') {
                current.pop_back();
            }
        }
        const auto canonical = canonicalWords().find(current);
        std::string token = canonical == canonicalWords().end() ? current : std::string(canonical->second);
        if (stopWords().count(token) == 0) {
            tokens.push_back(std::move(token));
        }
        current.clear();
    };
    for (std::size_t position = 0; position < collapsed.size(); ++position) {
        const char character = collapsed[position];
        const bool isDecimalPoint = character == '.' && !current.empty() && isDigit(current.back())
            && position + 1 < collapsed.size() && isDigit(collapsed[position + 1]);
        if (isLetter(character) || isDigit(character) || isDecimalPoint) {
            current.push_back(character);
        } else {
            finishToken();
        }
    }
    finishToken();
    return tokens;
}

}
