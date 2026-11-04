#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kparb {

std::optional<std::int64_t> parseIsoTimestampSeconds(std::string_view text);

std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day);

}
