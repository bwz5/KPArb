#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kparb {

std::string seriesOfKalshiEvent(std::string_view eventTicker);

std::string kalshiMarketType(std::string_view eventTicker);

std::string polymarketMarketType(std::string_view sportsMarketType);

std::vector<std::string> polymarketSportsTypesFor(std::string_view kalshiType);

bool areMarketTypesCompatible(std::string_view kalshiType, std::string_view polymarketType) noexcept;

}
