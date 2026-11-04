#include "matching/MatchStore.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace kparb {
namespace {

std::filesystem::path temporaryDirectory()
{
    auto directory = std::filesystem::temp_directory_path() / "kparb_match_store_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

TEST(MatchStoreTest, VerdictsSurviveSaveAndLoad)
{
    const auto directory = temporaryDirectory();
    {
        MatchStore store(directory / "cache.json", directory / "manual.json");
        store.load();
        MatchVerdict verdict;
        verdict.isSameEvent = true;
        verdict.isInverted = true;
        verdict.confidence = 0.93;
        verdict.reason = "Same event";
        verdict.model = "test-model";
        store.recordVerdict("K1", "P1", verdict);
        store.save();
    }
    MatchStore reloaded(directory / "cache.json", directory / "manual.json");
    reloaded.load();
    const auto verdict = reloaded.findVerdict("K1", "P1");
    ASSERT_TRUE(verdict.has_value());
    EXPECT_TRUE(verdict->isSameEvent);
    EXPECT_TRUE(verdict->isInverted);
    EXPECT_DOUBLE_EQ(verdict->confidence, 0.93);
    EXPECT_EQ(verdict->model, "test-model");
    EXPECT_FALSE(reloaded.findVerdict("K1", "P2").has_value());
    std::filesystem::remove_all(directory);
}

TEST(MatchStoreTest, ReadsManualOverrides)
{
    const auto directory = temporaryDirectory();
    std::ofstream(directory / "manual.json") << R"({"include":[{"kalshiTicker":"K9","polymarketIdentifier":"P9","isInverted":true}],"exclude":[{"kalshiTicker":"K1","polymarketIdentifier":"P1"}]})";
    MatchStore store(directory / "cache.json", directory / "manual.json");
    store.load();
    ASSERT_EQ(store.manualInclusions().size(), 1U);
    EXPECT_EQ(store.manualInclusions()[0].kalshiTicker, "K9");
    EXPECT_TRUE(store.manualInclusions()[0].isInverted);
    EXPECT_TRUE(store.isManuallyExcluded("K1", "P1"));
    EXPECT_FALSE(store.isManuallyExcluded("K9", "P9"));
    std::filesystem::remove_all(directory);
}

}
}
