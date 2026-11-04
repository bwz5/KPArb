#pragma once

#include "matching/MatchedPair.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kparb {

struct ManualMatch {
    std::string kalshiTicker;
    std::string polymarketIdentifier;
    bool isInverted = false;
};

class MatchStore {
public:
    MatchStore(std::filesystem::path verdictCachePath, std::filesystem::path manualMatchesPath);

    void load();
    void save() const;

    std::optional<MatchVerdict> findVerdict(const std::string& kalshiTicker, const std::string& polymarketIdentifier) const;
    void recordVerdict(const std::string& kalshiTicker, const std::string& polymarketIdentifier, const MatchVerdict& verdict);

    const std::vector<ManualMatch>& manualInclusions() const noexcept;
    bool isManuallyExcluded(const std::string& kalshiTicker, const std::string& polymarketIdentifier) const;
    std::size_t verdictCount() const noexcept;

private:
    using PairKey = std::pair<std::string, std::string>;

    std::filesystem::path cachePath;
    std::filesystem::path manualPath;
    std::map<PairKey, MatchVerdict> verdicts;
    std::vector<ManualMatch> inclusions;
    std::vector<PairKey> exclusions;
};

}
