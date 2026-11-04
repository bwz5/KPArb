#pragma once

#include "ingestion/HttpClient.h"
#include "ingestion/MarketInfo.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

std::size_t parsePolymarketMarketsPage(std::string_view body, std::vector<MarketInfo>& markets);

struct PolymarketCatalogSettings {
    std::string gammaBaseUrl = "https://gamma-api.polymarket.com";
    std::size_t pageLimit = 500;
    std::size_t maximumPages = 400;
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
