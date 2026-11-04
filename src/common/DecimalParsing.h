#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kparb {

enum class Rounding : std::uint8_t {
    Down,
    Up,
};

constexpr std::optional<std::int64_t> parseScaledDecimal(std::string_view text, int decimalDigits, Rounding rounding) noexcept
{
    if (text.empty()) {
        return std::nullopt;
    }
    bool isNegative = false;
    std::size_t position = 0;
    if (text[0] == '-' || text[0] == '+') {
        isNegative = text[0] == '-';
        position = 1;
    }
    std::int64_t integerPart = 0;
    bool sawDigit = false;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
        integerPart = integerPart * 10 + (text[position] - '0');
        if (integerPart > 1'000'000'000'000LL) {
            return std::nullopt;
        }
        sawDigit = true;
        ++position;
    }
    std::int64_t fractionalPart = 0;
    int fractionalDigitsRead = 0;
    bool hasDiscardedNonZero = false;
    if (position < text.size() && text[position] == '.') {
        ++position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            if (fractionalDigitsRead < decimalDigits) {
                fractionalPart = fractionalPart * 10 + (text[position] - '0');
                ++fractionalDigitsRead;
            } else if (text[position] != '0') {
                hasDiscardedNonZero = true;
            }
            sawDigit = true;
            ++position;
        }
    }
    if (!sawDigit || position != text.size()) {
        return std::nullopt;
    }
    for (int digit = fractionalDigitsRead; digit < decimalDigits; ++digit) {
        fractionalPart *= 10;
    }
    std::int64_t scale = 1;
    for (int digit = 0; digit < decimalDigits; ++digit) {
        scale *= 10;
    }
    std::int64_t magnitude = integerPart * scale + fractionalPart;
    if (hasDiscardedNonZero && rounding == Rounding::Up && !isNegative) {
        ++magnitude;
    }
    return isNegative ? -magnitude : magnitude;
}

}
