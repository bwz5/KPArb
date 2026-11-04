#pragma once

#include <cstdint>
#include <string_view>

namespace kparb {

enum class Venue : std::uint8_t {
    Kalshi = 0,
    Polymarket = 1,
};

inline constexpr std::uint32_t venueCount = 2;

constexpr std::string_view venueName(Venue venue) noexcept
{
    switch (venue) {
    case Venue::Kalshi:
        return "Kalshi";
    case Venue::Polymarket:
        return "Polymarket";
    }
    return "Unknown";
}

}
