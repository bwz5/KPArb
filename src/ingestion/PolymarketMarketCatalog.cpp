#include "ingestion/PolymarketMarketCatalog.h"

#include "common/TimeParsing.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace kparb {

namespace {

std::string readString(const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return {};
    }
    return found->get<std::string>();
}

bool readBoolean(const nlohmann::json& object, const char* key, bool fallback)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_boolean()) {
        return fallback;
    }
    return found->get<bool>();
}

std::vector<std::string> readEmbeddedStringArray(const nlohmann::json& object, const char* key)
{
    std::vector<std::string> values;
    const auto found = object.find(key);
    if (found == object.end()) {
        return values;
    }
    nlohmann::json array;
    if (found->is_string()) {
        array = nlohmann::json::parse(found->get<std::string>(), nullptr, false);
    } else {
        array = *found;
    }
    if (!array.is_array()) {
        return values;
    }
    for (const auto& element : array) {
        if (element.is_string()) {
            values.push_back(element.get<std::string>());
        }
    }
    return values;
}

}

std::size_t parsePolymarketMarketsPage(std::string_view body, std::vector<MarketInfo>& markets)
{
    const auto document = nlohmann::json::parse(body);
    const nlohmann::json* marketList = &document;
    if (document.is_object()) {
        const auto data = document.find("data");
        if (data == document.end()) {
            return 0;
        }
        marketList = &*data;
    }
    if (!marketList->is_array()) {
        return 0;
    }
    for (const auto& market : *marketList) {
        if (!readBoolean(market, "enableOrderBook", true) || readBoolean(market, "closed", false)
            || !readBoolean(market, "active", true)) {
            continue;
        }
        const auto outcomes = readEmbeddedStringArray(market, "outcomes");
        const auto tokens = readEmbeddedStringArray(market, "clobTokenIds");
        if (outcomes.size() != 2 || tokens.size() != 2) {
            continue;
        }
        MarketInfo info;
        info.venue = Venue::Polymarket;
        info.identifier = readString(market, "conditionId");
        if (info.identifier.empty()) {
            info.identifier = readString(market, "id");
        }
        info.eventIdentifier = readString(market, "slug");
        info.title = readString(market, "question");
        info.subtitle = readString(market, "groupItemTitle");
        info.rules = readString(market, "description");
        info.yesOutcomeName = outcomes[0];
        info.noOutcomeName = outcomes[1];
        info.yesTokenIdentifier = tokens[0];
        info.noTokenIdentifier = tokens[1];
        info.closeTimeSeconds = parseIsoTimestampSeconds(readString(market, "endDate")).value_or(0);
        const auto feeSchedule = market.find("feeSchedule");
        if (readBoolean(market, "feesEnabled", false) && feeSchedule != market.end() && feeSchedule->is_object()) {
            info.takerFeeRate = feeSchedule->value("rate", 0.0);
            info.takerFeeExponent = feeSchedule->value("exponent", 1.0);
        }
        if (info.identifier.empty() || info.title.empty()) {
            continue;
        }
        markets.push_back(std::move(info));
    }
    return marketList->size();
}

PolymarketMarketCatalog::PolymarketMarketCatalog(PolymarketCatalogSettings catalogSettings, HttpClient& httpClient)
    : settings(std::move(catalogSettings))
    , client(httpClient)
{
}

std::vector<MarketInfo> PolymarketMarketCatalog::fetchOpenMarkets(const ProgressCallback& onProgress)
{
    using namespace std::chrono_literals;
    std::vector<MarketInfo> markets;
    for (std::size_t page = 0; page < settings.maximumPages; ++page) {
        const std::string url = settings.gammaBaseUrl + "/markets?active=true&closed=false&archived=false&limit="
            + std::to_string(settings.pageLimit) + "&offset=" + std::to_string(page * settings.pageLimit);
        HttpResponse response;
        for (int attempt = 0; attempt < 5; ++attempt) {
            response = client.get(url);
            if (response.isSuccess()) {
                break;
            }
            const auto wait = response.retryAfterSeconds > 0 ? std::chrono::seconds(response.retryAfterSeconds)
                                                             : std::chrono::seconds(1LL << attempt);
            std::this_thread::sleep_for(wait);
        }
        if (!response.isSuccess()) {
            throw std::runtime_error("Polymarket markets request failed with status " + std::to_string(response.statusCode)
                + ": " + response.errorMessage + " " + response.body.substr(0, 300));
        }
        const std::size_t entriesOnPage = parsePolymarketMarketsPage(response.body, markets);
        if (onProgress) {
            onProgress(page + 1, markets.size());
        }
        if (entriesOnPage < settings.pageLimit) {
            break;
        }
        std::this_thread::sleep_for(100ms);
    }
    return markets;
}

}
