#include "TestConstants.h"
#include "arbitrage/FeeSchedule.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::fee_test;
using namespace test_constants::fees;
using namespace test_constants::prices;

TEST(FeeScheduleTest, ZeroScheduleChargesNothing)
{
    const auto schedule = FeeSchedule::zero();
    EXPECT_EQ(schedule.feeAt(freePrice), noFee);
    EXPECT_EQ(schedule.feeAt(fiftyCents), noFee);
    EXPECT_EQ(schedule.feeAt(oneDollar), noFee);
}

TEST(FeeScheduleTest, KalshiTakerFeePeaksAtFiftyCents)
{
    const auto schedule = FeeSchedule::quadratic(kalshiTakerFeeRate);
    EXPECT_EQ(schedule.feeAt(fiftyCents), kalshiFeeAtFiftyCents);
    EXPECT_EQ(schedule.feeAt(tenCents), kalshiFeeAtTenOrNinetyCents);
    EXPECT_EQ(schedule.feeAt(ninetyCents), kalshiFeeAtTenOrNinetyCents);
    EXPECT_EQ(schedule.feeAt(freePrice), noFee);
    EXPECT_EQ(schedule.feeAt(oneDollar), noFee);
    EXPECT_GT(schedule.feeAt(fiftyCents), schedule.feeAt(thirtyCents));
}

TEST(FeeScheduleTest, PolymarketCurveMatchesPublishedTable)
{
    const auto politics = FeeSchedule::curve(politicsTakerFeeRate, linearFeeExponent);
    EXPECT_EQ(politics.feeAt(fiftyCents), politicsFeeAtFiftyCents);
    const auto crypto = FeeSchedule::curve(cryptoTakerFeeRate, linearFeeExponent);
    EXPECT_EQ(crypto.feeAt(thirtyCents), cryptoFeeAtThirtyCents);
    EXPECT_EQ(FeeSchedule::curve(noFeeRate, linearFeeExponent), FeeSchedule::zero());
    EXPECT_LT(FeeSchedule::curve(politicsTakerFeeRate, quadraticFeeExponent).feeAt(fiftyCents), politics.feeAt(fiftyCents));
}

}
}
