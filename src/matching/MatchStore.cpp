#include "matching/MatchStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace kparb {

MatchStore::MatchStore(std::filesystem::path verdictCachePath, std::filesystem::path manualMatchesPath)
    : cachePath(std::move(verdictCachePath))
    , manualPath(std::move(manualMatchesPath))
{
}

void MatchStore::load()
{
    verdicts.clear();
    inclusions.clear();
    exclusions.clear();

    if (std::ifstream cacheFile(cachePath); cacheFile) {
        const auto document = nlohmann::json::parse(cacheFile, nullptr, false);
        if (document.is_object() && document.contains("verdicts") && document["verdicts"].is_array()) {
            for (const auto& entry : document["verdicts"]) {
                MatchVerdict verdict;
                verdict.isSameEvent = entry.value("sameEvent", false);
                verdict.isInverted = entry.value("isInverted", false);
                verdict.confidence = entry.value("confidence", 0.0);
                verdict.reason = entry.value("reason", "");
                verdict.model = entry.value("model", "");
                verdict.decidedAtSeconds = entry.value("decidedAtSeconds", std::int64_t{0});
                verdicts[{entry.value("kalshiTicker", ""), entry.value("polymarketIdentifier", "")}] = verdict;
            }
        }
    }

    if (std::ifstream manualFile(manualPath); manualFile) {
        const auto document = nlohmann::json::parse(manualFile, nullptr, false);
        if (document.is_object()) {
            for (const auto& entry : document.value("include", nlohmann::json::array())) {
                inclusions.push_back(ManualMatch{
                    entry.value("kalshiTicker", ""),
                    entry.value("polymarketIdentifier", ""),
                    entry.value("isInverted", false),
                });
            }
            for (const auto& entry : document.value("exclude", nlohmann::json::array())) {
                exclusions.emplace_back(entry.value("kalshiTicker", ""), entry.value("polymarketIdentifier", ""));
            }
        }
    }
}

void MatchStore::save() const
{
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& [key, verdict] : verdicts) {
        entries.push_back({
            {"kalshiTicker", key.first},
            {"polymarketIdentifier", key.second},
            {"sameEvent", verdict.isSameEvent},
            {"isInverted", verdict.isInverted},
            {"confidence", verdict.confidence},
            {"reason", verdict.reason},
            {"model", verdict.model},
            {"decidedAtSeconds", verdict.decidedAtSeconds},
        });
    }
    if (cachePath.has_parent_path()) {
        std::filesystem::create_directories(cachePath.parent_path());
    }
    const std::filesystem::path temporaryPath = cachePath.string() + ".tmp";
    {
        std::ofstream output(temporaryPath);
        if (!output) {
            throw std::runtime_error("Unable to write match cache " + temporaryPath.string());
        }
        output << nlohmann::json{{"verdicts", entries}}.dump(2) << '\n';
    }
    std::filesystem::rename(temporaryPath, cachePath);
}

std::optional<MatchVerdict> MatchStore::findVerdict(const std::string& kalshiTicker, const std::string& polymarketIdentifier) const
{
    const auto found = verdicts.find({kalshiTicker, polymarketIdentifier});
    if (found == verdicts.end()) {
        return std::nullopt;
    }
    return found->second;
}

void MatchStore::recordVerdict(const std::string& kalshiTicker, const std::string& polymarketIdentifier, const MatchVerdict& verdict)
{
    verdicts[{kalshiTicker, polymarketIdentifier}] = verdict;
}

const std::vector<ManualMatch>& MatchStore::manualInclusions() const noexcept
{
    return inclusions;
}

bool MatchStore::isManuallyExcluded(const std::string& kalshiTicker, const std::string& polymarketIdentifier) const
{
    return std::find(exclusions.begin(), exclusions.end(), PairKey{kalshiTicker, polymarketIdentifier}) != exclusions.end();
}

std::size_t MatchStore::verdictCount() const noexcept
{
    return verdicts.size();
}

}
