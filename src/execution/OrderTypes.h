#pragma once

#include "common/Price.h"
#include "common/Venue.h"

#include <cstdint>
#include <string>

namespace kparb {

enum class ContractSide : std::uint8_t {
    Yes,
    No,
};

struct OrderRequest {
    Venue venue = Venue::Kalshi;
    std::string marketIdentifier;
    ContractSide side = ContractSide::Yes;
    std::uint32_t contracts = 0;
    Ticks limitPrice = noPrice;
    std::string clientOrderIdentifier;
};

struct OrderResult {
    bool isAccepted = false;
    std::uint32_t filledContracts = 0;
    Ticks averagePrice = noPrice;
    std::string errorMessage;
};

class VenueOrderClient {
public:
    virtual ~VenueOrderClient() = default;
    virtual OrderResult placeImmediateOrder(const OrderRequest& request) = 0;
    virtual bool isLive() const noexcept = 0;
};

}
