#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kparb {

std::vector<std::string> tokenizeForMatching(std::string_view text);

bool isNumericToken(std::string_view token) noexcept;

bool isYearToken(std::string_view token) noexcept;

}
