#pragma once

#include "ingestion/MarketInfo.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kparb {

struct CandidatePair {
    std::size_t kalshiIndex;
    std::size_t polymarketIndex;
    double similarity;
};

struct CandidateSettings {
    std::size_t candidatesPerMarket = 2;
    double minimumSimilarity = 0.45;
    std::int64_t maximumCloseTimeDifferenceSeconds = 14 * 86400;
    double maximumDocumentFrequencyFraction = 0.05;
    std::size_t minimumDocumentFrequencyCap = 50;
    bool requireCompatibleNumbers = true;
};

std::string matchingTextFor(const MarketInfo& market);

class CandidateFinder {
public:
    CandidateFinder(const std::vector<MarketInfo>& polymarketMarkets, CandidateSettings candidateSettings);

    std::vector<CandidatePair> findCandidates(const std::vector<MarketInfo>& kalshiMarkets) const;

private:
    using SparseVector = std::vector<std::pair<std::uint32_t, double>>;

    SparseVector weightedVector(const std::vector<std::string>& tokens, bool isQuery) const;
    static std::vector<std::string> numericTokens(const std::vector<std::string>& tokens);
    static bool numbersAreCompatible(const std::vector<std::string>& first, const std::vector<std::string>& second);

    CandidateSettings settings;
    const std::vector<MarketInfo>& polymarket;
    std::unordered_map<std::string, std::uint32_t> vocabulary;
    std::vector<double> inverseDocumentFrequency;
    std::vector<std::size_t> documentFrequency;
    std::vector<std::vector<std::pair<std::uint32_t, double>>> postings;
    std::vector<std::vector<std::string>> polymarketNumbers;
    std::size_t documentFrequencyCap = 0;
};

}
