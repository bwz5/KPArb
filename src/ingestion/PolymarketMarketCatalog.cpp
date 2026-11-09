#include "ingestion/PolymarketMarketCatalog.h"

#include "common/TimeParsing.h"
#include "ingestion/MarketClassification.h"
#include "ingestion/UrlEncoding.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iterator>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace kparb {

namespace {

constexpr std::size_t rulesSummaryCharacters = 400;
constexpr int maximumPageAttempts = 5;

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

std::string firstEventTitle(const nlohmann::json& market)
{
    const auto events = market.find("events");
    if (events == market.end() || !events->is_array() || events->empty() || !(*events)[0].is_object()) {
        return {};
    }
    return readString((*events)[0], "title");
}

std::string summarizeDescription(const std::string& description, std::size_t limit)
{
    std::string summary = description.substr(0, description.find("\n\n"));
    if (summary.size() > limit) {
        summary.resize(limit);
    }
    return summary;
}

}

PolymarketPage parsePolymarketMarketsPage(std::string_view body, std::vector<MarketInfo>& markets)
{
    PolymarketPage page;
    const auto document = nlohmann::json::parse(body);
    const nlohmann::json* marketList = &document;
    if (document.is_object()) {
        page.nextCursor = readString(document, "next_cursor");
        auto found = document.find("markets");
        if (found == document.end()) {
            found = document.find("data");
        }
        if (found == document.end()) {
            return page;
        }
        marketList = &*found;
    }
    if (!marketList->is_array()) {
        return page;
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
        info.eventTitle = firstEventTitle(market);
        info.title = readString(market, "question");
        info.subtitle = readString(market, "groupItemTitle");
        info.rules = readString(market, "description");
        info.rulesSummary = summarizeDescription(info.rules, rulesSummaryCharacters);
        info.marketType = polymarketMarketType(readString(market, "sportsMarketType"));
        const auto line = market.find("line");
        if (line != market.end() && line->is_number()) {
            info.strike = std::fabs(line->get<double>());
            info.hasStrike = true;
        }
        info.yesOutcomeName = outcomes[0];
        info.noOutcomeName = outcomes[1];
        info.yesTokenIdentifier = tokens[0];
        info.noTokenIdentifier = tokens[1];
        info.resolutionDeadlineSeconds = parseIsoTimestampSeconds(readString(market, "endDate")).value_or(0);
        info.scheduledStartSeconds = parseIsoTimestampSeconds(readString(market, "gameStartTime")).value_or(0);
        info.closeTimeSeconds = info.scheduledStartSeconds != 0 ? info.scheduledStartSeconds : info.resolutionDeadlineSeconds;
        info.isAcceptingOrders = readBoolean(market, "acceptingOrders", false);
        info.openTimeSeconds = parseIsoTimestampSeconds(readString(market, "acceptingOrdersTimestamp")).value_or(0);
        if (info.openTimeSeconds == 0) {
            info.openTimeSeconds = parseIsoTimestampSeconds(readString(market, "startDate")).value_or(0);
        }
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
    page.entryCount = marketList->size();
    return page;
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
    std::string cursor;
    for (std::size_t page = 0; page < settings.maximumPages; ++page) {
        std::string url = settings.gammaBaseUrl + "/markets/keyset?active=true&closed=false&archived=false&limit="
            + std::to_string(settings.pageLimit);
        if (settings.minimumEndTimestamp > 0) {
            url += "&end_date_min=" + encodeUrlComponent(formatIsoTimestampSeconds(settings.minimumEndTimestamp));
        }
        if (settings.maximumEndTimestamp > 0) {
            url += "&end_date_max=" + encodeUrlComponent(formatIsoTimestampSeconds(settings.maximumEndTimestamp));
        }
        if (!settings.sportsMarketType.empty()) {
            url += "&sports_market_types=" + encodeUrlComponent(settings.sportsMarketType);
        }
        if (!cursor.empty()) {
            url += "&after_cursor=" + encodeUrlComponent(cursor);
        }
        std::vector<MarketInfo> pageMarkets;
        PolymarketPage parsed;
        std::string failure;
        for (int attempt = 0; attempt < maximumPageAttempts; ++attempt) {
            const HttpResponse response = client.get(url);
            if (response.isSuccess()) {
                try {
                    pageMarkets.clear();
                    parsed = parsePolymarketMarketsPage(response.body, pageMarkets);
                    failure.clear();
                    break;
                } catch (const std::exception& error) {
                    failure = std::string("unreadable page: ") + error.what();
                }
            } else {
                failure = "status " + std::to_string(response.statusCode) + ": " + response.errorMessage + " " + response.body.substr(0, 300);
            }
            const auto wait = response.retryAfterSeconds > 0 ? std::chrono::seconds(response.retryAfterSeconds)
                                                             : std::chrono::seconds(1LL << attempt);
            std::this_thread::sleep_for(wait);
        }
        if (!failure.empty()) {
            throw std::runtime_error("Polymarket markets request failed with " + failure);
        }
        markets.insert(markets.end(), std::make_move_iterator(pageMarkets.begin()), std::make_move_iterator(pageMarkets.end()));
        if (onProgress) {
            onProgress(page + 1, markets.size());
        }
        if (parsed.entryCount == 0 || parsed.nextCursor.empty() || parsed.nextCursor == cursor) {
            break;
        }
        cursor = parsed.nextCursor;
        std::this_thread::sleep_for(100ms);
    }
    return markets;
}

}
