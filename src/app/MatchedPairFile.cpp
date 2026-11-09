#include "app/MatchedPairFile.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace kparb {

void saveMatchedPairs(const std::filesystem::path& path, const std::vector<MatchedPair>& pairs)
{
    nlohmann::json entries = nlohmann::json::array();
    for (const MatchedPair& pair : pairs) {
        entries.push_back({
            {"kalshiTicker", pair.kalshiTicker},
            {"kalshiTitle", pair.kalshiTitle},
            {"polymarketIdentifier", pair.polymarketIdentifier},
            {"polymarketSlug", pair.polymarketSlug},
            {"polymarketTitle", pair.polymarketTitle},
            {"polymarketYesToken", pair.polymarketYesToken},
            {"polymarketNoToken", pair.polymarketNoToken},
            {"isInverted", pair.isInverted},
            {"confidence", pair.confidence},
            {"polymarketTakerFeeRate", pair.polymarketTakerFeeRate},
            {"polymarketTakerFeeExponent", pair.polymarketTakerFeeExponent},
            {"isManual", pair.isManual},
            {"noResultProbability", pair.noResultProbability},
            {"polymarketYesNoResultValue", pair.polymarketYesNoResultValue},
            {"polymarketNoNoResultValue", pair.polymarketNoNoResultValue},
        });
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Unable to write " + path.string());
    }
    output << nlohmann::json{{"pairs", entries}}.dump(2) << '\n';
}

std::vector<MatchedPair> loadMatchedPairs(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to read " + path.string());
    }
    const auto document = nlohmann::json::parse(input);
    std::vector<MatchedPair> pairs;
    for (const auto& entry : document.value("pairs", nlohmann::json::array())) {
        MatchedPair pair;
        pair.kalshiTicker = entry.value("kalshiTicker", "");
        pair.kalshiTitle = entry.value("kalshiTitle", "");
        pair.polymarketIdentifier = entry.value("polymarketIdentifier", "");
        pair.polymarketSlug = entry.value("polymarketSlug", "");
        pair.polymarketTitle = entry.value("polymarketTitle", "");
        pair.polymarketYesToken = entry.value("polymarketYesToken", "");
        pair.polymarketNoToken = entry.value("polymarketNoToken", "");
        pair.isInverted = entry.value("isInverted", false);
        pair.confidence = entry.value("confidence", 0.0);
        pair.polymarketTakerFeeRate = entry.value("polymarketTakerFeeRate", 0.0);
        pair.polymarketTakerFeeExponent = entry.value("polymarketTakerFeeExponent", 1.0);
        pair.isManual = entry.value("isManual", false);
        pair.noResultProbability = entry.value("noResultProbability", 0.0);
        pair.polymarketYesNoResultValue = entry.value("polymarketYesNoResultValue", 0.0);
        pair.polymarketNoNoResultValue = entry.value("polymarketNoNoResultValue", 0.0);
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

}
