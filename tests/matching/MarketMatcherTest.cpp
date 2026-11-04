#include "matching/ChatCompletionsClient.h"
#include "matching/MarketMatcher.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace kparb {
namespace {

class FakeLanguageModel final : public LanguageModelClient {
public:
    LanguageModelResponse complete(const std::vector<ChatMessage>& messages) override
    {
        ++requestCount;
        lastUserMessage = messages.back().content;
        if (lastUserMessage.find("Super Bowl") != std::string::npos) {
            return {true, R"({"same_event": false, "is_inverted": false, "confidence": 0.9, "reason": "Different"})", ""};
        }
        if (lastUserMessage.find("Bitcoin") != std::string::npos) {
            return {true, "```json\n{\"same_event\": true, \"is_inverted\": true, \"confidence\": 95, \"reason\": \"Mirrored\"}\n```", ""};
        }
        return {true, R"({"same_event": true, "is_inverted": false, "confidence": 0.97, "reason": "Same FOMC meeting"})", ""};
    }

    std::string modelName() const override
    {
        return "fake-model";
    }

    int requestCount = 0;
    std::string lastUserMessage;
};

MarketInfo market(Venue venue, std::string identifier, std::string title)
{
    MarketInfo info;
    info.venue = venue;
    info.identifier = std::move(identifier);
    info.title = std::move(title);
    info.yesOutcomeName = "Yes";
    info.noOutcomeName = "No";
    info.yesTokenIdentifier = info.identifier + "-yes";
    info.noTokenIdentifier = info.identifier + "-no";
    info.closeTimeSeconds = 1'000'000;
    return info;
}

class MarketMatcherTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() / "kparb_matcher_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        store = std::make_unique<MatchStore>(directory / "cache.json", directory / "manual.json");
        settings.candidates.minimumSimilarity = 0.3;
    }

    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }

    std::vector<MarketInfo> kalshi{
        market(Venue::Kalshi, "K-FED", "Will the Fed cut rates in December 2026?"),
        market(Venue::Kalshi, "K-BTC", "Bitcoin below $100,000 on December 31?"),
        market(Venue::Kalshi, "K-SB", "Super Bowl champion Kansas City?"),
    };
    std::vector<MarketInfo> polymarket{
        market(Venue::Polymarket, "P-FED", "Fed cuts rates in December 2026?"),
        market(Venue::Polymarket, "P-BTC", "Bitcoin above $100,000 on December 31?"),
        market(Venue::Polymarket, "P-SB", "Super Bowl champion Kansas City Chiefs?"),
    };
    std::filesystem::path directory;
    std::unique_ptr<MatchStore> store;
    std::unique_ptr<LogChannel> channel = std::make_unique<LogChannel>(0);
    FakeLanguageModel model;
    MatcherSettings settings;
};

TEST_F(MarketMatcherTest, AcceptsConfirmedPairsAndRecordsPolarity)
{
    store->load();
    MarketMatcher matcher(settings, model, *store, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    ASSERT_EQ(pairs.size(), 2U);
    EXPECT_EQ(model.requestCount, 3);

    const auto findPair = [&pairs](const std::string& ticker) {
        return std::find_if(pairs.begin(), pairs.end(), [&ticker](const MatchedPair& pair) { return pair.kalshiTicker == ticker; });
    };
    const auto fed = findPair("K-FED");
    ASSERT_NE(fed, pairs.end());
    EXPECT_EQ(fed->polymarketIdentifier, "P-FED");
    EXPECT_FALSE(fed->isInverted);
    EXPECT_EQ(fed->polymarketYesToken, "P-FED-yes");

    const auto bitcoin = findPair("K-BTC");
    ASSERT_NE(bitcoin, pairs.end());
    EXPECT_TRUE(bitcoin->isInverted);
    EXPECT_DOUBLE_EQ(bitcoin->confidence, 0.95);
    EXPECT_EQ(findPair("K-SB"), pairs.end());
}

TEST_F(MarketMatcherTest, CachedVerdictsSkipTheLanguageModel)
{
    store->load();
    {
        MarketMatcher matcher(settings, model, *store, *channel);
        matcher.match(kalshi, polymarket);
    }
    EXPECT_EQ(model.requestCount, 3);
    MatchStore reloaded(directory / "cache.json", directory / "manual.json");
    reloaded.load();
    EXPECT_EQ(reloaded.verdictCount(), 3U);
    MarketMatcher matcher(settings, model, reloaded, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    EXPECT_EQ(model.requestCount, 3);
    EXPECT_EQ(pairs.size(), 2U);
}

TEST_F(MarketMatcherTest, ManualOverridesIncludeAndExclude)
{
    std::ofstream(directory / "manual.json")
        << R"({"include":[{"kalshiTicker":"K-SB","polymarketIdentifier":"P-SB","isInverted":false}],"exclude":[{"kalshiTicker":"K-FED","polymarketIdentifier":"P-FED"}]})";
    store->load();
    MarketMatcher matcher(settings, model, *store, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    ASSERT_EQ(pairs.size(), 2U);
    EXPECT_EQ(model.requestCount, 2);
    EXPECT_TRUE(std::any_of(pairs.begin(), pairs.end(), [](const MatchedPair& pair) { return pair.isManual && pair.kalshiTicker == "K-SB"; }));
    EXPECT_FALSE(std::any_of(pairs.begin(), pairs.end(), [](const MatchedPair& pair) { return pair.kalshiTicker == "K-FED"; }));
}

TEST_F(MarketMatcherTest, RequestBudgetIsRespected)
{
    settings.maximumLanguageModelRequests = 1;
    store->load();
    MarketMatcher matcher(settings, model, *store, *channel);
    matcher.match(kalshi, polymarket);
    EXPECT_EQ(model.requestCount, 1);
}

TEST_F(MarketMatcherTest, PromptNamesBothOutcomes)
{
    MarketMatcher matcher(settings, model, *store, *channel);
    MarketInfo polymarketMarket = polymarket[0];
    polymarketMarket.yesOutcomeName = "Chiefs";
    polymarketMarket.noOutcomeName = "Eagles";
    const auto messages = matcher.buildMessages(kalshi[0], polymarketMarket);
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].role, "system");
    EXPECT_NE(messages[1].content.find("\"Chiefs\""), std::string::npos);
    EXPECT_NE(messages[1].content.find("\"Eagles\""), std::string::npos);
    EXPECT_NE(messages[1].content.find("JSON"), std::string::npos);
}

class FailingLanguageModel final : public LanguageModelClient {
public:
    LanguageModelResponse complete(const std::vector<ChatMessage>&) override
    {
        ++requestCount;
        return {false, "", "HTTP 429 rate limited"};
    }

    std::string modelName() const override
    {
        return "failing-model";
    }

    int requestCount = 0;
};

TEST_F(MarketMatcherTest, RepeatedFailuresDisableTheLanguageModel)
{
    settings.maximumConsecutiveFailures = 2;
    store->load();
    FailingLanguageModel failing;
    MarketMatcher matcher(settings, failing, *store, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    EXPECT_TRUE(pairs.empty());
    EXPECT_EQ(failing.requestCount, 2);
    EXPECT_TRUE(matcher.lastProgress().isLanguageModelDisabled);
    EXPECT_EQ(store->verdictCount(), 0U);
}

TEST(VerdictParsingTest, ToleratesFormattingVariations)
{
    EXPECT_FALSE(MarketMatcher::parseVerdict("no json here").has_value());
    const auto verdict = MarketMatcher::parseVerdict(R"(Sure! {"same_event": "true", "is_inverted": false, "confidence": "0.8", "reason": "ok"})");
    ASSERT_TRUE(verdict.has_value());
    EXPECT_TRUE(verdict->isSameEvent);
    EXPECT_DOUBLE_EQ(verdict->confidence, 0.8);
}

TEST(ChatCompletionsClientTest, BuildsOpenAiCompatibleRequest)
{
    ChatCompletionsSettings settings;
    settings.model = "test-model";
    const auto body = nlohmann::json::parse(ChatCompletionsClient::buildRequestBody(settings, {{"system", "s"}, {"user", "u"}}));
    EXPECT_EQ(body["model"], "test-model");
    EXPECT_EQ(body["temperature"], 0);
    EXPECT_EQ(body["messages"].size(), 2U);
    EXPECT_EQ(body["messages"][1]["content"], "u");
    EXPECT_EQ(body["response_format"]["type"], "json_object");
}

TEST(ChatCompletionsClientTest, ParsesChoiceContent)
{
    const auto response = ChatCompletionsClient::parseResponseBody(R"({"choices":[{"index":0,"message":{"role":"assistant","content":"{\"same_event\":true}"}}]})");
    EXPECT_TRUE(response.succeeded);
    EXPECT_EQ(response.content, R"({"same_event":true})");
    EXPECT_FALSE(ChatCompletionsClient::parseResponseBody(R"({"error":{"message":"bad"}})").succeeded);
    EXPECT_FALSE(ChatCompletionsClient::parseResponseBody("oops").succeeded);
}

}
}
