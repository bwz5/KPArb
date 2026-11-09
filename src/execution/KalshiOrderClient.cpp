#include "execution/KalshiOrderClient.h"

#include "ingestion/UrlEncoding.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

namespace kparb {

namespace {

constexpr std::int64_t ticksPerCent = 10;

std::int64_t roundDownToCent(std::int64_t ticks) noexcept
{
    return ticks / ticksPerCent * ticksPerCent;
}

std::int64_t roundUpToCent(std::int64_t ticks) noexcept
{
    return (ticks + ticksPerCent - 1) / ticksPerCent * ticksPerCent;
}

double readDecimal(const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return 0.0;
    }
    if (found->is_number()) {
        return found->get<double>();
    }
    if (found->is_string()) {
        try {
            return std::stod(found->get<std::string>());
        } catch (const std::exception&) {
            return 0.0;
        }
    }
    return 0.0;
}

}

KalshiOrderClient::KalshiOrderClient(std::string restBaseUrl, std::string apiKeyIdentifier, const KalshiSigner& requestSigner,
    HttpClient& httpClient)
    : baseUrl(std::move(restBaseUrl))
    , keyIdentifier(std::move(apiKeyIdentifier))
    , signer(requestSigner)
    , client(httpClient)
{
}

std::string KalshiOrderClient::formatDollars(std::int64_t ticks)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(ticks) / static_cast<double>(ticksPerDollar));
    return buffer;
}

std::string KalshiOrderClient::buildOrderBody(const OrderRequest& request)
{
    const bool buysYes = request.side == ContractSide::Yes;
    const std::int64_t yesPriceTicks = buysYes ? roundDownToCent(request.limitPrice)
                                               : roundUpToCent(static_cast<std::int64_t>(ticksPerDollar) - request.limitPrice);
    nlohmann::json body{
        {"ticker", request.marketIdentifier},
        {"side", buysYes ? "bid" : "ask"},
        {"count", std::to_string(request.contracts)},
        {"price", formatDollars(yesPriceTicks)},
        {"time_in_force", "immediate_or_cancel"},
        {"self_trade_prevention_type", "taker_at_cross"},
    };
    if (!request.clientOrderIdentifier.empty()) {
        body["client_order_id"] = request.clientOrderIdentifier;
    }
    return body.dump();
}

OrderResult KalshiOrderClient::parseOrderResponse(std::string_view body, ContractSide side)
{
    OrderResult result;
    const auto document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        result.errorMessage = "unreadable order response";
        return result;
    }
    const nlohmann::json& order = document.contains("order") && document["order"].is_object() ? document["order"] : document;
    if (!order.contains("order_id")) {
        result.errorMessage = std::string(body.substr(0, 300));
        return result;
    }
    result.isAccepted = true;
    result.filledContracts = static_cast<std::uint32_t>(std::floor(readDecimal(order, "fill_count") + 1e-9));
    if (result.filledContracts > 0) {
        const auto yesTicks = static_cast<std::int64_t>(std::lround(readDecimal(order, "average_fill_price") * ticksPerDollar));
        const std::int64_t paidTicks = side == ContractSide::Yes ? yesTicks : static_cast<std::int64_t>(ticksPerDollar) - yesTicks;
        result.averagePrice = static_cast<Ticks>(paidTicks < 0 ? 0 : (paidTicks > ticksPerDollar ? ticksPerDollar : paidTicks));
    }
    return result;
}

HttpHeaders KalshiOrderClient::signedHeaders(std::string_view method, const std::string& url) const
{
    const std::string path = url.substr(extractUrlOrigin(url).size());
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    HttpHeaders headers = signer.authenticationHeaders(keyIdentifier, method, path.substr(0, path.find('?')), now);
    headers.emplace_back("Content-Type", "application/json");
    return headers;
}

OrderResult KalshiOrderClient::placeImmediateOrder(const OrderRequest& request)
{
    const std::string url = baseUrl + "/portfolio/events/orders";
    const HttpResponse response = client.post(url, buildOrderBody(request), signedHeaders("POST", url));
    if (!response.isSuccess()) {
        OrderResult failed;
        failed.errorMessage = "status " + std::to_string(response.statusCode) + ": " + response.errorMessage + " " + response.body.substr(0, 300);
        return failed;
    }
    return parseOrderResponse(response.body, request.side);
}

bool KalshiOrderClient::isLive() const noexcept
{
    return true;
}

std::optional<std::int64_t> KalshiOrderClient::fetchBalanceCents()
{
    const std::string url = baseUrl + "/portfolio/balance";
    const HttpResponse response = client.get(url, signedHeaders("GET", url));
    if (!response.isSuccess()) {
        return std::nullopt;
    }
    const auto document = nlohmann::json::parse(response.body, nullptr, false);
    if (document.is_discarded() || !document.contains("balance") || !document["balance"].is_number()) {
        return std::nullopt;
    }
    return document["balance"].get<std::int64_t>();
}

}
