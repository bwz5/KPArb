#include "ingestion/KalshiMarketCatalog.h"

#include "common/TimeParsing.h"
#include "ingestion/UrlEncoding.h"

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

std::int64_t readTimestamp(const nlohmann::json& object, const char* key)
{
    const auto parsed = parseIsoTimestampSeconds(readString(object, key));
    return parsed.value_or(0);
}

}

std::string parseKalshiMarketsPage(std::string_view body, std::vector<MarketInfo>& markets)
{
    const auto document = nlohmann::json::parse(body);
    const auto marketList = document.find("markets");
    if (marketList != document.end() && marketList->is_array()) {
        for (const auto& market : *marketList) {
            const std::string marketType = readString(market, "market_type");
            if (!marketType.empty() && marketType != "binary") {
                continue;
            }
            MarketInfo info;
            info.venue = Venue::Kalshi;
            info.identifier = readString(market, "ticker");
            if (info.identifier.empty()) {
                continue;
            }
            info.eventIdentifier = readString(market, "event_ticker");
            info.title = readString(market, "title");
            info.subtitle = readString(market, "yes_sub_title");
            if (info.subtitle.empty()) {
                info.subtitle = readString(market, "subtitle");
            }
            info.rules = readString(market, "rules_primary");
            const std::string secondaryRules = readString(market, "rules_secondary");
            if (!secondaryRules.empty()) {
                info.rules += "\n" + secondaryRules;
            }
            info.yesOutcomeName = readString(market, "yes_sub_title");
            info.noOutcomeName = readString(market, "no_sub_title");
            const std::string status = readString(market, "status");
            info.isAcceptingOrders = status == "active" || status == "open";
            info.openTimeSeconds = readTimestamp(market, "open_time");
            info.closeTimeSeconds = readTimestamp(market, "expected_expiration_time");
            if (info.closeTimeSeconds == 0) {
                info.closeTimeSeconds = readTimestamp(market, "close_time");
            }
            markets.push_back(std::move(info));
        }
    }
    return readString(document, "cursor");
}

KalshiMarketCatalog::KalshiMarketCatalog(KalshiCatalogSettings catalogSettings, HttpClient& httpClient)
    : settings(std::move(catalogSettings))
    , client(httpClient)
{
}

std::vector<MarketInfo> KalshiMarketCatalog::fetchOpenMarkets(const ProgressCallback& onProgress)
{
    using namespace std::chrono_literals;
    std::vector<MarketInfo> markets;
    std::string cursor;
    for (std::size_t page = 0; page < settings.maximumPages; ++page) {
        std::string url = settings.restBaseUrl + "/markets?status=open&limit=" + std::to_string(settings.pageLimit);
        if (settings.excludeMultivariateMarkets) {
            url += "&mve_filter=exclude";
        }
        if (settings.minimumCloseTimestamp > 0) {
            url += "&min_close_ts=" + std::to_string(settings.minimumCloseTimestamp);
        }
        if (settings.maximumCloseTimestamp > 0) {
            url += "&max_close_ts=" + std::to_string(settings.maximumCloseTimestamp);
        }
        if (!cursor.empty()) {
            url += "&cursor=" + encodeUrlComponent(cursor);
        }

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
            throw std::runtime_error("Kalshi markets request failed with status " + std::to_string(response.statusCode) + ": "
                + response.errorMessage + " " + response.body.substr(0, 300));
        }
        cursor = parseKalshiMarketsPage(response.body, markets);
        if (onProgress) {
            onProgress(page + 1, markets.size());
        }
        if (cursor.empty()) {
            break;
        }
        std::this_thread::sleep_for(100ms);
    }
    return markets;
}

}
