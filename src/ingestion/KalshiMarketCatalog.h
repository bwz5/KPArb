#pragma once

#include "ingestion/HttpClient.h"
#include "ingestion/MarketInfo.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

std::string parseKalshiMarketsPage(std::string_view body, std::vector<MarketInfo>& markets);

struct KalshiCatalogSettings {
    std::string restBaseUrl = "https://api.elections.kalshi.com/trade-api/v2";
    std::size_t pageLimit = 1000;
    std::size_t maximumPages = 500;
    bool excludeMultivariateMarkets = true;
    std::int64_t minimumCloseTimestamp = 0;
    std::int64_t maximumCloseTimestamp = 0;
};

class KalshiMarketCatalog {
public:
    using ProgressCallback = std::function<void(std::size_t pagesFetched, std::size_t marketsSoFar)>;

    KalshiMarketCatalog(KalshiCatalogSettings catalogSettings, HttpClient& httpClient);

    std::vector<MarketInfo> fetchOpenMarkets(const ProgressCallback& onProgress = {});

private:
    KalshiCatalogSettings settings;
    HttpClient& client;
};

}
