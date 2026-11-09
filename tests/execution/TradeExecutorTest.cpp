#include "TestConstants.h"
#include "execution/ExposureBook.h"
#include "execution/KalshiOrderClient.h"
#include "execution/PaperOrderClient.h"
#include "execution/TradeExecutor.h"
#include "execution/TradePlanner.h"

#include <gtest/gtest.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace kparb {
namespace {

using namespace test_constants::execution_test;

class ScriptedOrderClient final : public VenueOrderClient {
public:
    explicit ScriptedOrderClient(std::deque<std::uint32_t> scriptedFills)
        : fills(std::move(scriptedFills))
    {
    }

    OrderResult placeImmediateOrder(const OrderRequest& request) override
    {
        requests.push_back(request);
        OrderResult result;
        result.isAccepted = true;
        const std::uint32_t scripted = fills.empty() ? request.contracts : fills.front();
        if (!fills.empty()) {
            fills.pop_front();
        }
        result.filledContracts = std::min(scripted, request.contracts);
        result.averagePrice = result.filledContracts > 0 ? request.limitPrice : noPrice;
        return result;
    }

    bool isLive() const noexcept override
    {
        return false;
    }

    std::deque<std::uint32_t> fills;
    std::vector<OrderRequest> requests;
};

ExecutionSignal openSignal(std::int64_t detectedAt = signalTimeNanos)
{
    ExecutionSignal signal;
    signal.detectedAtNanos = detectedAt;
    signal.pairIndex = 0;
    signal.executableQuantity = signalContracts * quantityUnitsPerContract;
    signal.kalshiTopQuantity = thinBookQuantity;
    signal.polymarketTopQuantity = deepBookQuantity;
    signal.kalshiPrice = kalshiAsk;
    signal.polymarketPrice = polymarketAsk;
    signal.kalshiWorstPrice = kalshiAsk;
    signal.polymarketWorstPrice = polymarketAsk;
    signal.kalshiOppositeAsk = kalshiOppositeAsk;
    signal.polymarketOppositeAsk = polymarketOppositeAsk;
    signal.edgeTicks = signalEdge;
    signal.direction = TradeDirection::BuyKalshiYesBuyPolymarketNo;
    signal.kind = SignalKind::Opened;
    return signal;
}

PairRoute route()
{
    return PairRoute{"KXNBAGAME-26NOV13OKCLAC-OKC", "token-okc", "token-lac"};
}

class TradeExecutorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() / "kparb_executor_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        settings.mode = ExecutionMode::Paper;
        settings.journalPath = directory / "executions.jsonl";
        settings.risk.killSwitchPath = directory / "KILL";
        settings.risk.maximumDollarsPerTrade = generousDollars;
        settings.risk.maximumDollarsPerPair = generousDollars;
        settings.risk.kalshiBudgetDollars = generousDollars;
        settings.risk.polymarketBudgetDollars = generousDollars;
        settings.risk.maximumLossDollars = generousDollars;
    }

    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }

    std::filesystem::path directory;
    ExecutionSettings settings;
};

TEST(TradePlannerTest, ThinnerBookGoesFirstAndUnwindBuysTheOtherOutcome)
{
    const auto plan = planTrade(openSignal(), route(), PlannerSettings{});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->contracts, signalContracts);
    EXPECT_EQ(plan->first.venue, Venue::Kalshi);
    EXPECT_EQ(plan->first.side, ContractSide::Yes);
    EXPECT_EQ(plan->first.unwindSide, ContractSide::No);
    EXPECT_EQ(plan->first.unwindLimitPrice, kalshiOppositeAsk + PlannerSettings{}.unwindSlippageTicks);
    EXPECT_EQ(plan->second.venue, Venue::Polymarket);
    EXPECT_EQ(plan->second.marketIdentifier, "token-lac");
    EXPECT_EQ(plan->second.unwindMarketIdentifier, "token-okc");
    EXPECT_EQ(plan->second.chaseLimitPrice, polymarketAsk + PlannerSettings{}.chaseTicks);

    ExecutionSignal deeperKalshi = openSignal();
    deeperKalshi.kalshiTopQuantity = deepBookQuantity;
    deeperKalshi.polymarketTopQuantity = thinBookQuantity;
    deeperKalshi.direction = TradeDirection::BuyKalshiNoBuyPolymarketYes;
    const auto reversed = planTrade(deeperKalshi, route(), PlannerSettings{});
    ASSERT_TRUE(reversed.has_value());
    EXPECT_EQ(reversed->first.venue, Venue::Polymarket);
    EXPECT_EQ(reversed->first.marketIdentifier, "token-okc");
    EXPECT_EQ(reversed->second.side, ContractSide::No);
}

TEST_F(TradeExecutorTest, FullyHedgedTradeIsJournaledOncePerEpisode)
{
    PaperOrderClient kalshi;
    PaperOrderClient polymarket;
    ExposureBook exposure(settings.risk);
    TradeExecutor executor(settings, {route()}, kalshi, polymarket, exposure);
    executor.handle(openSignal(), signalTimeNanos);
    ExecutionSignal changed = openSignal();
    changed.kind = SignalKind::Changed;
    executor.handle(changed, signalTimeNanos);
    EXPECT_EQ(executor.counters().tradesAttempted, 1U);
    EXPECT_EQ(executor.counters().fullyHedged, 1U);
    EXPECT_EQ(kalshi.placedOrders().size(), 1U);
    EXPECT_EQ(polymarket.placedOrders().size(), 1U);
    EXPECT_EQ(polymarket.placedOrders()[0].contracts, signalContracts);

    ExecutionSignal closed = openSignal();
    closed.kind = SignalKind::Closed;
    executor.handle(closed, signalTimeNanos);
    executor.handle(openSignal(), signalTimeNanos);
    EXPECT_EQ(executor.counters().tradesAttempted, 2U);

    std::ifstream journal(settings.journalPath);
    std::string line;
    std::getline(journal, line);
    const auto entry = nlohmann::json::parse(line);
    EXPECT_EQ(entry["second_filled"].get<int>(), static_cast<int>(signalContracts));
    EXPECT_EQ(entry["mode"].get<std::string>(), "paper");
}

TEST_F(TradeExecutorTest, ShortSecondLegIsChasedThenUnwound)
{
    ScriptedOrderClient kalshi({signalContracts, unwindableContracts});
    ScriptedOrderClient polymarket({hedgedContracts, chasedContracts});
    ExposureBook exposure(settings.risk);
    TradeExecutor executor(settings, {route()}, kalshi, polymarket, exposure);
    executor.handle(openSignal(), signalTimeNanos);
    const ExecutionReport& report = executor.lastReport();
    EXPECT_EQ(report.firstFilled, signalContracts);
    EXPECT_EQ(report.secondFilled, hedgedContracts);
    EXPECT_EQ(report.chasedFilled, chasedContracts);
    EXPECT_EQ(report.unwoundContracts, signalContracts - hedgedContracts - chasedContracts);
    EXPECT_EQ(report.unhedgedContracts, 0U);
    ASSERT_EQ(kalshi.requests.size(), 2U);
    EXPECT_EQ(kalshi.requests[1].side, ContractSide::No);
    EXPECT_EQ(polymarket.requests[1].limitPrice, polymarketAsk + PlannerSettings{}.chaseTicks);
    const double expectedLoss = static_cast<double>(report.unwoundContracts)
        * ticksToDollars(kalshiAsk + kalshiOppositeAsk + PlannerSettings{}.unwindSlippageTicks - ticksPerDollar);
    EXPECT_NEAR(report.lossDollars, expectedLoss, dollarTolerance);
    EXPECT_TRUE(exposure.isTradingAllowed());
}

TEST_F(TradeExecutorTest, LeftoverExposureHaltsTrading)
{
    ScriptedOrderClient kalshi({signalContracts, noFill});
    ScriptedOrderClient polymarket({noFill, noFill});
    ExposureBook exposure(settings.risk);
    TradeExecutor executor(settings, {route()}, kalshi, polymarket, exposure);
    executor.handle(openSignal(), signalTimeNanos);
    EXPECT_EQ(executor.lastReport().unhedgedContracts, signalContracts);
    EXPECT_FALSE(exposure.isTradingAllowed());
    EXPECT_NE(exposure.haltReason().find("unhedged"), std::string::npos);
}

TEST_F(TradeExecutorTest, StaleSignalsKillSwitchAndBudgetsBlockTrades)
{
    PaperOrderClient kalshi;
    PaperOrderClient polymarket;
    ExposureBook exposure(settings.risk);
    TradeExecutor executor(settings, {route()}, kalshi, polymarket, exposure);
    executor.handle(openSignal(), signalTimeNanos + settings.maximumSignalAgeNanos + 1);
    EXPECT_EQ(executor.counters().staleSignals, 1U);

    std::ofstream(settings.risk.killSwitchPath) << "stop";
    executor.handle(openSignal(), signalTimeNanos);
    EXPECT_EQ(executor.counters().blockedByRisk, 1U);
    std::filesystem::remove(settings.risk.killSwitchPath);

    RiskSettings tight = settings.risk;
    tight.maximumDollarsPerTrade = tinyTradeDollars;
    ExposureBook tightExposure(tight);
    const double perContract = ticksToDollars(kalshiAsk) + ticksToDollars(polymarketAsk);
    EXPECT_EQ(tightExposure.affordableContracts(0, ticksToDollars(kalshiAsk), ticksToDollars(polymarketAsk)),
        static_cast<std::uint32_t>(tinyTradeDollars / perContract));
    tight.maximumDollarsPerTrade = generousDollars;
    tight.kalshiBudgetDollars = ticksToDollars(kalshiAsk) * budgetContracts;
    ExposureBook budgeted(tight);
    EXPECT_EQ(budgeted.affordableContracts(0, ticksToDollars(kalshiAsk), ticksToDollars(polymarketAsk)), budgetContracts);
    budgeted.recordSpend(0, tight.kalshiBudgetDollars, 0.0);
    EXPECT_EQ(budgeted.affordableContracts(0, ticksToDollars(kalshiAsk), ticksToDollars(polymarketAsk)), 0U);
    budgeted.advanceTo(signalTimeNanos);
    budgeted.advanceTo(signalTimeNanos + static_cast<std::int64_t>(tight.rebalanceHours * nanosPerHour));
    EXPECT_EQ(budgeted.affordableContracts(0, ticksToDollars(kalshiAsk), ticksToDollars(polymarketAsk)), budgetContracts);
}

TEST(KalshiOrderClientTest, BuildsImmediateOrdersOnTheYesLeg)
{
    OrderRequest buyYes{Venue::Kalshi, "KXNBAGAME-26NOV13OKCLAC-OKC", ContractSide::Yes, signalContracts, subCentAsk, "id-1"};
    const auto yesBody = nlohmann::json::parse(KalshiOrderClient::buildOrderBody(buyYes));
    EXPECT_EQ(yesBody["side"], "bid");
    EXPECT_EQ(yesBody["price"], "0.4500");
    EXPECT_EQ(yesBody["count"], std::to_string(signalContracts));
    EXPECT_EQ(yesBody["time_in_force"], "immediate_or_cancel");
    EXPECT_EQ(yesBody["client_order_id"], "id-1");

    OrderRequest buyNo{Venue::Kalshi, "KXNBAGAME-26NOV13OKCLAC-OKC", ContractSide::No, signalContracts, subCentAsk, ""};
    const auto noBody = nlohmann::json::parse(KalshiOrderClient::buildOrderBody(buyNo));
    EXPECT_EQ(noBody["side"], "ask");
    EXPECT_EQ(noBody["price"], "0.5500");
    EXPECT_FALSE(noBody.contains("client_order_id"));
}

TEST(KalshiOrderClientTest, ReadsFillsAndConvertsNoPrices)
{
    const OrderResult yes = KalshiOrderClient::parseOrderResponse(
        R"({"order_id":"abc","fill_count":"7.00","remaining_count":"3.00","average_fill_price":"0.4400","ts_ms":1})", ContractSide::Yes);
    EXPECT_TRUE(yes.isAccepted);
    EXPECT_EQ(yes.filledContracts, partialKalshiFill);
    EXPECT_EQ(yes.averagePrice, kalshiYesFillPrice);
    const OrderResult no = KalshiOrderClient::parseOrderResponse(
        R"({"order_id":"abc","fill_count":"7.00","remaining_count":"3.00","average_fill_price":"0.4400","ts_ms":1})", ContractSide::No);
    EXPECT_EQ(no.averagePrice, ticksPerDollar - kalshiYesFillPrice);
    const OrderResult rejected = KalshiOrderClient::parseOrderResponse(R"({"error":{"code":"insufficient_balance"}})", ContractSide::Yes);
    EXPECT_FALSE(rejected.isAccepted);
    EXPECT_NE(rejected.errorMessage.find("insufficient_balance"), std::string::npos);
}

}
}
