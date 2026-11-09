#include "execution/ExposureBook.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kparb {

ExposureBook::ExposureBook(RiskSettings riskSettings)
    : settings(std::move(riskSettings))
{
}

void ExposureBook::advanceTo(std::int64_t nowNanos)
{
    const auto periodNanos = static_cast<std::int64_t>(settings.rebalanceHours * 3600.0 * 1e9);
    if (periodEndsAtNanos == 0) {
        periodEndsAtNanos = nowNanos + periodNanos;
        return;
    }
    if (periodNanos > 0 && nowNanos >= periodEndsAtNanos) {
        while (periodEndsAtNanos <= nowNanos) {
            periodEndsAtNanos += periodNanos;
        }
        kalshiSpentDollars = 0.0;
        polymarketSpentDollars = 0.0;
        lossDollars = 0.0;
        spentByPair.clear();
    }
}

std::uint32_t ExposureBook::affordableContracts(std::uint32_t pairIndex, double kalshiDollarsPerContract, double polymarketDollarsPerContract) const
{
    const double perContract = kalshiDollarsPerContract + polymarketDollarsPerContract;
    if (perContract <= 0.0 || kalshiDollarsPerContract < 0.0 || polymarketDollarsPerContract < 0.0) {
        return 0;
    }
    const auto found = spentByPair.find(pairIndex);
    const double pairSpent = found == spentByPair.end() ? 0.0 : found->second;
    double contracts = std::min({
        static_cast<double>(settings.maximumContractsPerTrade),
        settings.maximumDollarsPerTrade / perContract,
        (settings.maximumDollarsPerPair - pairSpent) / perContract,
        kalshiDollarsPerContract > 0.0 ? (settings.kalshiBudgetDollars - kalshiSpentDollars) / kalshiDollarsPerContract
                                       : std::numeric_limits<double>::infinity(),
        polymarketDollarsPerContract > 0.0 ? (settings.polymarketBudgetDollars - polymarketSpentDollars) / polymarketDollarsPerContract
                                           : std::numeric_limits<double>::infinity(),
    });
    contracts = std::floor(contracts + 1e-9);
    return contracts <= 0.0 ? 0 : static_cast<std::uint32_t>(contracts);
}

void ExposureBook::recordSpend(std::uint32_t pairIndex, double kalshiDollars, double polymarketDollars)
{
    kalshiSpentDollars += kalshiDollars;
    polymarketSpentDollars += polymarketDollars;
    spentByPair[pairIndex] += kalshiDollars + polymarketDollars;
}

void ExposureBook::recordLoss(double dollars)
{
    lossDollars += dollars;
    if (lossDollars >= settings.maximumLossDollars) {
        halt("loss limit reached");
    }
}

void ExposureBook::halt(std::string reason)
{
    if (reasonForHalt.empty()) {
        reasonForHalt = std::move(reason);
    }
}

bool ExposureBook::isTradingAllowed() const
{
    std::error_code error;
    return reasonForHalt.empty() && !(!settings.killSwitchPath.empty() && std::filesystem::exists(settings.killSwitchPath, error));
}

const std::string& ExposureBook::haltReason() const noexcept
{
    return reasonForHalt;
}

double ExposureBook::kalshiSpent() const noexcept
{
    return kalshiSpentDollars;
}

double ExposureBook::polymarketSpent() const noexcept
{
    return polymarketSpentDollars;
}

double ExposureBook::lossThisPeriod() const noexcept
{
    return lossDollars;
}

const RiskSettings& ExposureBook::riskSettings() const noexcept
{
    return settings;
}

}
