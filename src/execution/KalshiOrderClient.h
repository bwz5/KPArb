#pragma once

#include "execution/OrderTypes.h"
#include "ingestion/HttpClient.h"
#include "ingestion/KalshiSigner.h"

#include <optional>
#include <string>
#include <string_view>

namespace kparb {

class KalshiOrderClient final : public VenueOrderClient {
public:
    KalshiOrderClient(std::string restBaseUrl, std::string apiKeyIdentifier, const KalshiSigner& requestSigner, HttpClient& httpClient);

    OrderResult placeImmediateOrder(const OrderRequest& request) override;
    bool isLive() const noexcept override;

    std::optional<std::int64_t> fetchBalanceCents();

    static std::string buildOrderBody(const OrderRequest& request);
    static OrderResult parseOrderResponse(std::string_view body, ContractSide side);
    static std::string formatDollars(std::int64_t ticks);

private:
    HttpHeaders signedHeaders(std::string_view method, const std::string& url) const;

    std::string baseUrl;
    std::string keyIdentifier;
    const KalshiSigner& signer;
    HttpClient& client;
};

}
