#include "arbitrage/AskLadder.h"

#include <gtest/gtest.h>

#include <memory>

namespace kparb {
namespace {

class AskLadderTest : public ::testing::Test {
protected:
    std::unique_ptr<AskLadder> ladder = std::make_unique<AskLadder>();
};

TEST_F(AskLadderTest, StartsEmpty)
{
    EXPECT_TRUE(ladder->isEmpty());
    EXPECT_EQ(ladder->bestPrice(), noPrice);
    EXPECT_EQ(ladder->bestQuantity(), 0U);
}

TEST_F(AskLadderTest, TracksLowestPriceAsBest)
{
    ladder->setLevel(600, 10);
    ladder->setLevel(450, 20);
    ladder->setLevel(700, 30);
    EXPECT_EQ(ladder->bestPrice(), 450);
    EXPECT_EQ(ladder->bestQuantity(), 20U);
}

TEST_F(AskLadderTest, RemovingBestFindsNextLevelAcrossWords)
{
    ladder->setLevel(5, 1);
    ladder->setLevel(900, 2);
    ladder->setLevel(5, 0);
    EXPECT_EQ(ladder->bestPrice(), 900);
    EXPECT_EQ(ladder->bestQuantity(), 2U);
    ladder->setLevel(900, 0);
    EXPECT_TRUE(ladder->isEmpty());
}

TEST_F(AskLadderTest, UpdatingBestQuantityKeepsPrice)
{
    ladder->setLevel(300, 10);
    ladder->setLevel(300, 25);
    EXPECT_EQ(ladder->bestPrice(), 300);
    EXPECT_EQ(ladder->bestQuantity(), 25U);
}

TEST_F(AskLadderTest, AddToLevelAccumulatesAndRemovesAtZero)
{
    ladder->addToLevel(400, 50);
    ladder->addToLevel(400, 25);
    EXPECT_EQ(ladder->quantityAt(400), 75U);
    ladder->addToLevel(400, -100);
    EXPECT_EQ(ladder->quantityAt(400), 0U);
    EXPECT_TRUE(ladder->isEmpty());
}

TEST_F(AskLadderTest, RemovingNonBestLevelLeavesBestAlone)
{
    ladder->setLevel(100, 1);
    ladder->setLevel(200, 2);
    ladder->setLevel(200, 0);
    EXPECT_EQ(ladder->bestPrice(), 100);
    EXPECT_EQ(ladder->occupiedLevelCount(), 1U);
}

TEST_F(AskLadderTest, ClearForgetsStaleQuantities)
{
    ladder->setLevel(250, 40);
    ladder->clear();
    EXPECT_EQ(ladder->quantityAt(250), 0U);
    ladder->addToLevel(250, 5);
    EXPECT_EQ(ladder->quantityAt(250), 5U);
}

TEST_F(AskLadderTest, IteratesOccupiedLevelsInOrder)
{
    ladder->setLevel(0, 1);
    ladder->setLevel(63, 1);
    ladder->setLevel(64, 1);
    ladder->setLevel(1000, 1);
    EXPECT_EQ(ladder->bestPrice(), 0);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(0), 63);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(63), 64);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(64), 1000);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(1000), noPrice);
}

}
}
