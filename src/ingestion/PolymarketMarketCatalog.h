#pragma once

#include "ingestion/HttpClient.h"
#include "ingestion/MarketInfo.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

struct PolymarketPage {
    std::size_t entryCount = 0;
    std::string nextCursor;
};

PolymarketPage parsePolymarketMarketsPage(std::string_view body, std::vector<MarketInfo>& markets);

struct PolymarketCatalogSettings {
    std::string gammaBaseUrl = "https://gamma-api.polymarket.com";
    std::size_t pageLimit = 100;
    std::size_t maximumPages = 2000;
    std::int64_t minimumEndTimestamp = 0;
    std::int64_t maximumEndTimestamp = 0;
    std::string sportsMarketType;
};

class PolymarketMarketCatalog {
public:
    using ProgressCallback = std::function<void(std::size_t pagesFetched, std::size_t marketsSoFar)>;

    PolymarketMarketCatalog(PolymarketCatalogSettings catalogSettings, HttpClient& httpClient);

    std::vector<MarketInfo> fetchOpenMarkets(const ProgressCallback& onProgress = {});

private:
    PolymarketCatalogSettings settings;
    HttpClient& client;
};

}
