#include "TestConstants.h"
#include "arbitrage/AskLadder.h"

#include <gtest/gtest.h>

#include <memory>

namespace kparb {
namespace {

using namespace test_constants::ladder_test;
using test_constants::prices::freePrice;
using test_constants::prices::oneDollar;

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
    ladder->setLevel(middleAsk, middleQuantity);
    ladder->setLevel(lowAsk, lowQuantity);
    ladder->setLevel(highAsk, highQuantity);
    EXPECT_EQ(ladder->bestPrice(), lowAsk);
    EXPECT_EQ(ladder->bestQuantity(), lowQuantity);
}

TEST_F(AskLadderTest, RemovingBestFindsNextLevelAcrossWords)
{
    ladder->setLevel(firstWordAsk, firstWordQuantity);
    ladder->setLevel(lastWordAsk, lastWordQuantity);
    ladder->setLevel(firstWordAsk, removedQuantity);
    EXPECT_EQ(ladder->bestPrice(), lastWordAsk);
    EXPECT_EQ(ladder->bestQuantity(), lastWordQuantity);
    ladder->setLevel(lastWordAsk, removedQuantity);
    EXPECT_TRUE(ladder->isEmpty());
}

TEST_F(AskLadderTest, UpdatingBestQuantityKeepsPrice)
{
    ladder->setLevel(updatedAsk, initialQuantity);
    ladder->setLevel(updatedAsk, replacedQuantity);
    EXPECT_EQ(ladder->bestPrice(), updatedAsk);
    EXPECT_EQ(ladder->bestQuantity(), replacedQuantity);
}

TEST_F(AskLadderTest, AddToLevelAccumulatesAndRemovesAtZero)
{
    ladder->addToLevel(accumulatedAsk, firstAddition);
    ladder->addToLevel(accumulatedAsk, secondAddition);
    EXPECT_EQ(ladder->quantityAt(accumulatedAsk), static_cast<Quantity>(firstAddition + secondAddition));
    ladder->addToLevel(accumulatedAsk, oversizedRemoval);
    EXPECT_EQ(ladder->quantityAt(accumulatedAsk), removedQuantity);
    EXPECT_TRUE(ladder->isEmpty());
}

TEST_F(AskLadderTest, RemovingNonBestLevelLeavesBestAlone)
{
    ladder->setLevel(bestAsk, bestQuantity);
    ladder->setLevel(worseAsk, worseQuantity);
    ladder->setLevel(worseAsk, removedQuantity);
    EXPECT_EQ(ladder->bestPrice(), bestAsk);
    EXPECT_EQ(ladder->occupiedLevelCount(), 1U);
}

TEST_F(AskLadderTest, ClearForgetsStaleQuantities)
{
    ladder->setLevel(clearedAsk, staleQuantity);
    ladder->clear();
    EXPECT_EQ(ladder->quantityAt(clearedAsk), removedQuantity);
    ladder->addToLevel(clearedAsk, static_cast<std::int32_t>(freshQuantity));
    EXPECT_EQ(ladder->quantityAt(clearedAsk), freshQuantity);
}

TEST_F(AskLadderTest, IteratesOccupiedLevelsInOrder)
{
    constexpr auto lastPriceOfFirstWord = static_cast<Ticks>(AskLadder::bitsPerWord - 1);
    constexpr auto firstPriceOfSecondWord = static_cast<Ticks>(AskLadder::bitsPerWord);
    ladder->setLevel(freePrice, anyQuantity);
    ladder->setLevel(lastPriceOfFirstWord, anyQuantity);
    ladder->setLevel(firstPriceOfSecondWord, anyQuantity);
    ladder->setLevel(oneDollar, anyQuantity);
    EXPECT_EQ(ladder->bestPrice(), freePrice);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(freePrice), lastPriceOfFirstWord);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(lastPriceOfFirstWord), firstPriceOfSecondWord);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(firstPriceOfSecondWord), oneDollar);
    EXPECT_EQ(ladder->nextOccupiedPriceAfter(oneDollar), noPrice);
}

}
}
