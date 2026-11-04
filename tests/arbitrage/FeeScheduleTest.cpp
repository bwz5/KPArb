#include "arbitrage/FeeSchedule.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(FeeScheduleTest, ZeroScheduleChargesNothing)
{
    const auto schedule = FeeSchedule::zero();
    EXPECT_EQ(schedule.feeAt(0), 0);
    EXPECT_EQ(schedule.feeAt(500), 0);
    EXPECT_EQ(schedule.feeAt(1000), 0);
}

TEST(FeeScheduleTest, KalshiTakerFeePeaksAtFiftyCents)
{
    const auto schedule = FeeSchedule::quadratic(0.07);
    EXPECT_EQ(schedule.feeAt(500), 18);
    EXPECT_EQ(schedule.feeAt(100), 7);
    EXPECT_EQ(schedule.feeAt(900), 7);
    EXPECT_EQ(schedule.feeAt(0), 0);
    EXPECT_EQ(schedule.feeAt(1000), 0);
    EXPECT_GT(schedule.feeAt(500), schedule.feeAt(300));
}

TEST(FeeScheduleTest, PolymarketCurveMatchesPublishedTable)
{
    const auto politics = FeeSchedule::curve(0.04, 1.0);
    EXPECT_EQ(politics.feeAt(500), 10);
    const auto crypto = FeeSchedule::curve(0.07, 1.0);
    EXPECT_EQ(crypto.feeAt(300), 15);
    EXPECT_EQ(FeeSchedule::curve(0.0, 1.0), FeeSchedule::zero());
    EXPECT_LT(FeeSchedule::curve(0.04, 2.0).feeAt(500), politics.feeAt(500));
}

}
}
