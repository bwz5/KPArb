#include "execution/PaperOrderClient.h"

namespace kparb {

OrderResult PaperOrderClient::placeImmediateOrder(const OrderRequest& request)
{
    orders.push_back(request);
    OrderResult result;
    result.isAccepted = request.contracts > 0 && request.limitPrice <= ticksPerDollar;
    result.filledContracts = result.isAccepted ? request.contracts : 0;
    result.averagePrice = result.isAccepted ? request.limitPrice : noPrice;
    return result;
}

bool PaperOrderClient::isLive() const noexcept
{
    return false;
}

const std::vector<OrderRequest>& PaperOrderClient::placedOrders() const noexcept
{
    return orders;
}

}
