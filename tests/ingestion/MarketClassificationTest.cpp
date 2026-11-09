#include "ingestion/MarketClassification.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(MarketClassificationTest, KalshiTypesMapToThePolymarketSportsTypesTheyCanPairWith)
{
    EXPECT_EQ(polymarketSportsTypesFor(kalshiMarketType("KXNBASPREAD-26NOV09SASPHX")), (std::vector<std::string>{"spreads", "soccer_spreads"}));
    EXPECT_EQ(polymarketSportsTypesFor(kalshiMarketType("KXNFL1HTEAMTOTAL-26NOV16LACBAL")),
        (std::vector<std::string>{"first_half_team_totals", "soccer_first_half_team_totals"}));
    EXPECT_EQ(polymarketSportsTypesFor(kalshiMarketType("KXNBAGAME-26NOV13OKCLAC")), (std::vector<std::string>{"moneyline", "soccer_moneyline"}));
    EXPECT_EQ(polymarketSportsTypesFor(kalshiMarketType("KXNFL1QSPREAD-26NOV16LACBAL")), (std::vector<std::string>{"q1_spreads", "soccer_q1_spreads"}));
    EXPECT_TRUE(polymarketSportsTypesFor(kalshiMarketType("KXFEDDECISION-26DEC")).empty());
    for (const std::string& type : polymarketSportsTypesFor("team_total")) {
        EXPECT_EQ(polymarketMarketType(type), "team_total");
    }
}

}
}
