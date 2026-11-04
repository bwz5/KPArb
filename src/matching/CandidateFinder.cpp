#include "matching/CandidateFinder.h"

#include "matching/TextNormalizer.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace kparb {

std::string matchingTextFor(const MarketInfo& market)
{
    if (market.subtitle.empty() || market.title.find(market.subtitle) != std::string::npos) {
        return market.title;
    }
    return market.title + " " + market.subtitle;
}

CandidateFinder::CandidateFinder(const std::vector<MarketInfo>& polymarketMarkets, CandidateSettings candidateSettings)
    : settings(candidateSettings)
    , polymarket(polymarketMarkets)
{
    std::vector<std::vector<std::string>> documents;
    documents.reserve(polymarket.size());
    for (const MarketInfo& market : polymarket) {
        documents.push_back(tokenizeForMatching(matchingTextFor(market)));
        polymarketNumbers.push_back(numericTokens(documents.back()));
        std::unordered_set<std::string> unique(documents.back().begin(), documents.back().end());
        for (const std::string& token : unique) {
            const auto [entry, inserted] = vocabulary.try_emplace(token, static_cast<std::uint32_t>(vocabulary.size()));
            if (inserted) {
                documentFrequency.push_back(0);
            }
            ++documentFrequency[entry->second];
        }
    }
    const double documentCount = static_cast<double>(polymarket.size());
    inverseDocumentFrequency.resize(documentFrequency.size());
    for (std::size_t term = 0; term < documentFrequency.size(); ++term) {
        inverseDocumentFrequency[term] = std::log((documentCount + 1.0) / (static_cast<double>(documentFrequency[term]) + 1.0)) + 1.0;
    }
    documentFrequencyCap = std::max(settings.minimumDocumentFrequencyCap,
        static_cast<std::size_t>(settings.maximumDocumentFrequencyFraction * documentCount));

    postings.resize(vocabulary.size());
    for (std::size_t documentIndex = 0; documentIndex < documents.size(); ++documentIndex) {
        for (const auto& [term, weight] : weightedVector(documents[documentIndex], false)) {
            postings[term].emplace_back(static_cast<std::uint32_t>(documentIndex), weight);
        }
    }
}

CandidateFinder::SparseVector CandidateFinder::weightedVector(const std::vector<std::string>& tokens, bool isQuery) const
{
    std::unordered_map<std::uint32_t, double> termCounts;
    for (const std::string& token : tokens) {
        const auto found = vocabulary.find(token);
        if (found != vocabulary.end()) {
            termCounts[found->second] += 1.0;
        }
    }
    SparseVector weights;
    double squaredNorm = 0.0;
    for (const auto& [term, count] : termCounts) {
        const double weight = (1.0 + std::log(count)) * inverseDocumentFrequency[term];
        weights.emplace_back(term, weight);
        squaredNorm += weight * weight;
    }
    if (isQuery) {
        for (const std::string& token : tokens) {
            if (vocabulary.find(token) == vocabulary.end()) {
                const double unseenWeight = std::log(static_cast<double>(polymarket.size()) + 1.0) + 1.0;
                squaredNorm += unseenWeight * unseenWeight;
            }
        }
    }
    const double norm = std::sqrt(squaredNorm);
    if (norm > 0.0) {
        for (auto& entry : weights) {
            entry.second /= norm;
        }
    }
    return weights;
}

std::vector<std::string> CandidateFinder::numericTokens(const std::vector<std::string>& tokens)
{
    std::vector<std::string> numbers;
    for (const std::string& token : tokens) {
        if (isNumericToken(token) && !isYearToken(token)) {
            numbers.push_back(token);
        }
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    return numbers;
}

bool CandidateFinder::numbersAreCompatible(const std::vector<std::string>& first, const std::vector<std::string>& second)
{
    return std::includes(first.begin(), first.end(), second.begin(), second.end())
        || std::includes(second.begin(), second.end(), first.begin(), first.end());
}

std::vector<CandidatePair> CandidateFinder::findCandidates(const std::vector<MarketInfo>& kalshiMarkets) const
{
    std::vector<CandidatePair> candidates;
    std::vector<double> scores(polymarket.size(), 0.0);
    std::vector<std::uint32_t> touched;

    for (std::size_t kalshiIndex = 0; kalshiIndex < kalshiMarkets.size(); ++kalshiIndex) {
        const MarketInfo& kalshiMarket = kalshiMarkets[kalshiIndex];
        const auto tokens = tokenizeForMatching(matchingTextFor(kalshiMarket));
        const auto kalshiNumbers = numericTokens(tokens);
        touched.clear();
        for (const auto& [term, queryWeight] : weightedVector(tokens, true)) {
            if (documentFrequency[term] > documentFrequencyCap) {
                continue;
            }
            for (const auto& [documentIndex, documentWeight] : postings[term]) {
                if (scores[documentIndex] == 0.0) {
                    touched.push_back(documentIndex);
                }
                scores[documentIndex] += queryWeight * documentWeight;
            }
        }

        std::vector<CandidatePair> marketCandidates;
        for (const std::uint32_t documentIndex : touched) {
            const double similarity = scores[documentIndex];
            scores[documentIndex] = 0.0;
            if (similarity < settings.minimumSimilarity) {
                continue;
            }
            const MarketInfo& polymarketMarket = polymarket[documentIndex];
            if (kalshiMarket.closeTimeSeconds > 0 && polymarketMarket.closeTimeSeconds > 0
                && std::llabs(kalshiMarket.closeTimeSeconds - polymarketMarket.closeTimeSeconds) > settings.maximumCloseTimeDifferenceSeconds) {
                continue;
            }
            if (settings.requireCompatibleNumbers && !numbersAreCompatible(kalshiNumbers, polymarketNumbers[documentIndex])) {
                continue;
            }
            marketCandidates.push_back(CandidatePair{kalshiIndex, documentIndex, similarity});
        }
        std::sort(marketCandidates.begin(), marketCandidates.end(),
            [](const CandidatePair& left, const CandidatePair& right) { return left.similarity > right.similarity; });
        if (marketCandidates.size() > settings.candidatesPerMarket) {
            marketCandidates.resize(settings.candidatesPerMarket);
        }
        candidates.insert(candidates.end(), marketCandidates.begin(), marketCandidates.end());
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const CandidatePair& left, const CandidatePair& right) { return left.similarity > right.similarity; });
    return candidates;
}

}
