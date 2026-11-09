#pragma once

#include "execution/OrderTypes.h"

#include <vector>

namespace kparb {

class PaperOrderClient final : public VenueOrderClient {
public:
    OrderResult placeImmediateOrder(const OrderRequest& request) override;
    bool isLive() const noexcept override;

    const std::vector<OrderRequest>& placedOrders() const noexcept;

private:
    std::vector<OrderRequest> orders;
};

}
