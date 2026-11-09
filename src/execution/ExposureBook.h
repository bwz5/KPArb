#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace kparb {

struct RiskSettings {
    double maximumDollarsPerTrade = 10.0;
    std::uint32_t maximumContractsPerTrade = 100;
    double maximumDollarsPerPair = 50.0;
    double kalshiBudgetDollars = 250.0;
    double polymarketBudgetDollars = 250.0;
    double rebalanceHours = 24.0;
    double maximumLossDollars = 25.0;
    std::filesystem::path killSwitchPath = "data/KILL";
};

class ExposureBook {
public:
    explicit ExposureBook(RiskSettings riskSettings);

    void advanceTo(std::int64_t nowNanos);
    std::uint32_t affordableContracts(std::uint32_t pairIndex, double kalshiDollarsPerContract, double polymarketDollarsPerContract) const;
    void recordSpend(std::uint32_t pairIndex, double kalshiDollars, double polymarketDollars);
    void recordLoss(double dollars);
    void halt(std::string reason);

    bool isTradingAllowed() const;
    const std::string& haltReason() const noexcept;
    double kalshiSpent() const noexcept;
    double polymarketSpent() const noexcept;
    double lossThisPeriod() const noexcept;
    const RiskSettings& riskSettings() const noexcept;

private:
    RiskSettings settings;
    std::int64_t periodEndsAtNanos = 0;
    double kalshiSpentDollars = 0.0;
    double polymarketSpentDollars = 0.0;
    double lossDollars = 0.0;
    std::unordered_map<std::uint32_t, double> spentByPair;
    std::string reasonForHalt;
};

}
