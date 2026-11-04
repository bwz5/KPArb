#include "arbitrage/FeeSchedule.h"

#include <cmath>

namespace kparb {

FeeSchedule FeeSchedule::zero() noexcept
{
    return FeeSchedule{};
}

FeeSchedule FeeSchedule::quadratic(double feeRate) noexcept
{
    return curve(feeRate, 1.0);
}

FeeSchedule FeeSchedule::curve(double feeRate, double exponent) noexcept
{
    FeeSchedule schedule;
    if (feeRate <= 0.0) {
        return schedule;
    }
    for (std::size_t price = 0; price <= ticksPerDollar; ++price) {
        const double priceInDollars = static_cast<double>(price) / ticksPerDollar;
        const double priceComponent = std::pow(priceInDollars * (1.0 - priceInDollars), exponent);
        const double feeInTicks = feeRate * priceComponent * ticksPerDollar;
        schedule.feesByPrice[price] = static_cast<Ticks>(std::ceil(feeInTicks - 1e-9));
    }
    return schedule;
}

}
