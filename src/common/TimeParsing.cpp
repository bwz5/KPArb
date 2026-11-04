#include "common/TimeParsing.h"

namespace kparb {

namespace {

bool readDigits(std::string_view text, std::size_t& position, std::size_t count, std::int64_t& value)
{
    if (position + count > text.size()) {
        return false;
    }
    value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const char character = text[position + index];
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (character - '0');
    }
    position += count;
    return true;
}

bool expectCharacter(std::string_view text, std::size_t& position, char expected)
{
    if (position < text.size() && text[position] == expected) {
        ++position;
        return true;
    }
    return false;
}

}

std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

std::optional<std::int64_t> parseIsoTimestampSeconds(std::string_view text)
{
    std::size_t position = 0;
    std::int64_t year = 0;
    std::int64_t month = 0;
    std::int64_t day = 0;
    if (!readDigits(text, position, 4, year) || !expectCharacter(text, position, '-')
        || !readDigits(text, position, 2, month) || !expectCharacter(text, position, '-')
        || !readDigits(text, position, 2, day)) {
        return std::nullopt;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }
    std::int64_t hour = 0;
    std::int64_t minute = 0;
    std::int64_t second = 0;
    std::int64_t offsetSeconds = 0;
    if (position < text.size() && (text[position] == 'T' || text[position] == ' ')) {
        ++position;
        if (!readDigits(text, position, 2, hour) || !expectCharacter(text, position, ':')
            || !readDigits(text, position, 2, minute)) {
            return std::nullopt;
        }
        if (expectCharacter(text, position, ':') && !readDigits(text, position, 2, second)) {
            return std::nullopt;
        }
        if (expectCharacter(text, position, '.')) {
            while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
                ++position;
            }
        }
        if (position < text.size()) {
            const char zone = text[position];
            if (zone == 'Z' || zone == 'z') {
                ++position;
            } else if (zone == '+' || zone == '-') {
                ++position;
                std::int64_t offsetHours = 0;
                std::int64_t offsetMinutes = 0;
                if (!readDigits(text, position, 2, offsetHours)) {
                    return std::nullopt;
                }
                expectCharacter(text, position, ':');
                if (position < text.size() && !readDigits(text, position, 2, offsetMinutes)) {
                    return std::nullopt;
                }
                offsetSeconds = (offsetHours * 3600 + offsetMinutes * 60) * (zone == '+' ? 1 : -1);
            }
        }
    }
    if (position != text.size()) {
        return std::nullopt;
    }
    const std::int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return days * 86400 + hour * 3600 + minute * 60 + second - offsetSeconds;
}

}
