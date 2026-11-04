#include "arbitrage/FeeSchedule.h"

#include <cmath>

namespace kparb {

FeeSchedule FeeSchedule::zero() noexcept
{
    return FeeSchedule{};
}

FeeSchedule FeeSchedule::quadratic(double feeRate) noexcept
{
    FeeSchedule schedule;
    for (std::size_t price = 0; price <= ticksPerDollar; ++price) {
        const double priceInDollars = static_cast<double>(price) / ticksPerDollar;
        const double feeInTicks = feeRate * priceInDollars * (1.0 - priceInDollars) * ticksPerDollar;
        schedule.feesByPrice[price] = static_cast<Ticks>(std::ceil(feeInTicks - 1e-9));
    }
    return schedule;
}

}
