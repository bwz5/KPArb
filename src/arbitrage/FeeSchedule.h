#pragma once

#include "common/Price.h"

#include <array>

namespace kparb {

class FeeSchedule {
public:
    static FeeSchedule zero() noexcept;
    static FeeSchedule quadratic(double feeRate) noexcept;

    Ticks feeAt(Ticks price) const noexcept
    {
        return feesByPrice[price];
    }

private:
    std::array<Ticks, ticksPerDollar + 1> feesByPrice{};
};

}
