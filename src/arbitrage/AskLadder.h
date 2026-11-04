#pragma once

#include "common/CacheLine.h"
#include "common/Price.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace kparb {

class alignas(cacheLineSize) AskLadder {
public:
    static constexpr std::size_t bitsPerWord = 64;
    static constexpr std::size_t occupancyWordCount = priceLevelCount / bitsPerWord;

    void clear() noexcept
    {
        occupiedWords.fill(0);
        bestAskPrice = noPrice;
        bestAskQuantity = 0;
    }

    void setLevel(Ticks price, Quantity quantity) noexcept
    {
        const std::size_t wordIndex = price / bitsPerWord;
        const std::uint64_t bit = std::uint64_t{1} << (price % bitsPerWord);
        if (quantity == 0) {
            if ((occupiedWords[wordIndex] & bit) == 0) {
                return;
            }
            occupiedWords[wordIndex] &= ~bit;
            if (price == bestAskPrice) {
                bestAskPrice = firstOccupiedPriceFrom(static_cast<std::size_t>(price) + 1);
                bestAskQuantity = bestAskPrice == noPrice ? 0 : quantityAtPrice[bestAskPrice];
            }
            return;
        }
        quantityAtPrice[price] = quantity;
        occupiedWords[wordIndex] |= bit;
        if (price <= bestAskPrice) {
            bestAskPrice = price;
            bestAskQuantity = quantity;
        }
    }

    void addToLevel(Ticks price, std::int32_t quantityChange) noexcept
    {
        const std::int64_t updated = static_cast<std::int64_t>(quantityAt(price)) + quantityChange;
        if (updated <= 0) {
            setLevel(price, 0);
        } else if (updated >= std::numeric_limits<Quantity>::max()) {
            setLevel(price, std::numeric_limits<Quantity>::max());
        } else {
            setLevel(price, static_cast<Quantity>(updated));
        }
    }

    Ticks bestPrice() const noexcept
    {
        return bestAskPrice;
    }

    Quantity bestQuantity() const noexcept
    {
        return bestAskQuantity;
    }

    bool isEmpty() const noexcept
    {
        return bestAskPrice == noPrice;
    }

    bool isOccupied(Ticks price) const noexcept
    {
        return (occupiedWords[price / bitsPerWord] >> (price % bitsPerWord)) & 1U;
    }

    Quantity quantityAt(Ticks price) const noexcept
    {
        return isOccupied(price) ? quantityAtPrice[price] : 0;
    }

    Ticks nextOccupiedPriceAfter(Ticks price) const noexcept
    {
        return firstOccupiedPriceFrom(static_cast<std::size_t>(price) + 1);
    }

    std::size_t occupiedLevelCount() const noexcept
    {
        std::size_t count = 0;
        for (const std::uint64_t word : occupiedWords) {
            count += static_cast<std::size_t>(std::popcount(word));
        }
        return count;
    }

    const void* addressOfLevel(Ticks price) const noexcept
    {
        return &quantityAtPrice[price];
    }

    const void* addressOfOccupancyWord(Ticks price) const noexcept
    {
        return &occupiedWords[price / bitsPerWord];
    }

    bool isSynchronized = false;

private:
    Ticks firstOccupiedPriceFrom(std::size_t startPrice) const noexcept
    {
        if (startPrice >= priceLevelCount) {
            return noPrice;
        }
        std::size_t wordIndex = startPrice / bitsPerWord;
        std::uint64_t word = occupiedWords[wordIndex] & (~std::uint64_t{0} << (startPrice % bitsPerWord));
        while (true) {
            if (word != 0) {
                return static_cast<Ticks>(wordIndex * bitsPerWord + static_cast<std::size_t>(std::countr_zero(word)));
            }
            if (++wordIndex == occupancyWordCount) {
                return noPrice;
            }
            word = occupiedWords[wordIndex];
        }
    }

    Ticks bestAskPrice = noPrice;
    Quantity bestAskQuantity = 0;
    std::array<std::uint64_t, occupancyWordCount> occupiedWords{};
    std::array<Quantity, priceLevelCount> quantityAtPrice{};
};

}
