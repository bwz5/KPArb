#pragma once

#include <cstddef>
#include <cstdint>

namespace kparb {

using Ticks = std::uint16_t;
using Quantity = std::uint32_t;

inline constexpr Ticks ticksPerDollar = 1000;
inline constexpr Ticks noPrice = 0xFFFF;
inline constexpr std::size_t priceLevelCount = 1024;
inline constexpr std::uint32_t quantityUnitsPerContract = 100;
inline constexpr int priceDecimalDigits = 3;
inline constexpr int quantityDecimalDigits = 2;

constexpr Ticks complementPrice(Ticks price) noexcept
{
    return static_cast<Ticks>(ticksPerDollar - price);
}

constexpr bool isValidPrice(std::int64_t price) noexcept
{
    return price >= 0 && price <= ticksPerDollar;
}

constexpr double ticksToDollars(std::int64_t ticks) noexcept
{
    return static_cast<double>(ticks) / static_cast<double>(ticksPerDollar);
}

constexpr Ticks dollarsToTicks(double dollars) noexcept
{
    const double clamped = dollars < 0.0 ? 0.0 : (dollars > 1.0 ? 1.0 : dollars);
    return static_cast<Ticks>(clamped * static_cast<double>(ticksPerDollar) + 0.5);
}

constexpr double quantityToContracts(std::int64_t quantity) noexcept
{
    return static_cast<double>(quantity) / static_cast<double>(quantityUnitsPerContract);
}

}
