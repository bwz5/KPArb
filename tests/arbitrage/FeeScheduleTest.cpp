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

}
}
