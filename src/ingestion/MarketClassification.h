#pragma once

#include <string>
#include <string_view>

namespace kparb {

std::string seriesOfKalshiEvent(std::string_view eventTicker);

std::string kalshiMarketType(std::string_view eventTicker);

std::string polymarketMarketType(std::string_view sportsMarketType);

bool areMarketTypesCompatible(std::string_view kalshiType, std::string_view polymarketType) noexcept;

}
