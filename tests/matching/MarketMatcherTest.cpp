#include "matching/ChatCompletionsClient.h"
#include "matching/MarketMatcher.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
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
        settings.concurrentRequests = 1;
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

TEST_F(MarketMatcherTest, OutcomeEntitiesDecidePolarityOverTheLanguageModel)
{
    store->load();
    std::vector<MarketInfo> kalshiGames{
        market(Venue::Kalshi, "K-DUKE", "Fed Duke Blue Devils wins"),
        market(Venue::Kalshi, "K-FED", "Fed Coastal Carolina Chanticleers wins"),
    };
    kalshiGames[0].subjectTeamKey = "cbb:DUKE";
    kalshiGames[1].subjectTeamKey = "cbb:TCU";
    std::vector<MarketInfo> polymarketGames{market(Venue::Polymarket, "P-GAME", "Fed Coastal Carolina Chanticleers vs. Duke Blue Devils")};
    polymarketGames[0].yesOutcomeEntityKey = "cbb:CCAR";
    polymarketGames[0].noOutcomeEntityKey = "cbb:DUKE";
    settings.candidates.minimumSimilarity = 0.0;
    MarketMatcher matcher(settings, model, *store, *channel);
    const auto pairs = matcher.match(kalshiGames, polymarketGames);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].kalshiTicker, "K-DUKE");
    EXPECT_TRUE(pairs[0].isInverted);
    EXPECT_EQ(matcher.lastProgress().polarityCorrections, 1U);
    EXPECT_EQ(entityPolarity(kalshiGames[1], polymarketGames[0]), EntityPolarity::Conflicting);
    MarketInfo yesNoMarket = market(Venue::Polymarket, "P-YES", "Will Duke win?");
    EXPECT_EQ(entityPolarity(kalshiGames[0], yesNoMarket), EntityPolarity::Unknown);
    yesNoMarket.yesOutcomeEntityKey = "cbb:DUKE";
    EXPECT_EQ(entityPolarity(kalshiGames[0], yesNoMarket), EntityPolarity::Aligned);
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

class QuotaLimitedLanguageModel final : public LanguageModelClient {
public:
    LanguageModelResponse complete(const std::vector<ChatMessage>& messages) override
    {
        ++requestCount;
        if (requestCount == 1) {
            return {false, "", "HTTP 429 tokens per day", 1};
        }
        return fallback.complete(messages);
    }

    std::string modelName() const override
    {
        return "quota-model";
    }

    int requestCount = 0;
    FakeLanguageModel fallback;
};

TEST_F(MarketMatcherTest, ShortQuotaWaitsRetryTheSameCandidate)
{
    store->load();
    QuotaLimitedLanguageModel limited;
    MarketMatcher matcher(settings, limited, *store, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    EXPECT_EQ(pairs.size(), 2U);
    EXPECT_EQ(limited.requestCount, 4);
    EXPECT_EQ(matcher.lastProgress().quotaWaits, 1U);
    EXPECT_FALSE(matcher.lastProgress().isLanguageModelDisabled);
}

TEST_F(MarketMatcherTest, LongQuotaWaitsCountAsFailures)
{
    settings.maximumQuotaWaitSeconds = 0;
    store->load();
    QuotaLimitedLanguageModel limited;
    MarketMatcher matcher(settings, limited, *store, *channel);
    const auto pairs = matcher.match(kalshi, polymarket);
    EXPECT_EQ(matcher.lastProgress().quotaWaits, 0U);
    EXPECT_EQ(limited.requestCount, 3);
    EXPECT_EQ(store->verdictCount(), 2U);
    EXPECT_LE(pairs.size(), 2U);
}

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

class ConcurrentLanguageModel final : public LanguageModelClient {
public:
    LanguageModelResponse complete(const std::vector<ChatMessage>& messages) override
    {
        const int active = ++activeRequests;
        int previousPeak = peakActiveRequests.load();
        while (active > previousPeak && !peakActiveRequests.compare_exchange_weak(previousPeak, active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ++requestCount;
        --activeRequests;
        const bool isSuperBowl = messages.back().content.find("Super Bowl") != std::string::npos;
        return {true, isSuperBowl ? R"({"same_event": false, "confidence": 0.9, "reason": "Different"})"
                                  : R"({"same_event": true, "confidence": 0.95, "reason": "Same"})", ""};
    }

    std::string modelName() const override
    {
        return "concurrent-model";
    }

    std::atomic<int> activeRequests{0};
    std::atomic<int> peakActiveRequests{0};
    std::atomic<int> requestCount{0};
};

TEST_F(MarketMatcherTest, ConcurrentRequestsRunInParallelAndRecordEveryVerdict)
{
    settings.concurrentRequests = 3;
    store->load();
    ConcurrentLanguageModel concurrent;
    MarketMatcher matcher(settings, concurrent, *store, *channel);
    std::size_t progressCallbacks = 0;
    const auto pairs = matcher.match(kalshi, polymarket,
        [&progressCallbacks](const MatcherProgress&, const MarketInfo&, const MarketInfo&, const MatchVerdict&) { ++progressCallbacks; });
    EXPECT_EQ(concurrent.requestCount.load(), static_cast<int>(matcher.lastProgress().candidatesTotal));
    EXPECT_GE(concurrent.peakActiveRequests.load(), 2);
    EXPECT_EQ(store->verdictCount(), matcher.lastProgress().candidatesTotal);
    EXPECT_EQ(progressCallbacks, matcher.lastProgress().candidatesTotal);
    EXPECT_EQ(matcher.lastProgress().candidatesProcessed, matcher.lastProgress().candidatesTotal);
    for (const auto& pair : pairs) {
        EXPECT_NE(pair.kalshiTicker, "K-SB");
    }
}

TEST_F(MarketMatcherTest, CachedVerdictsAreAcceptedBeforeNewRequests)
{
    store->load();
    MatchVerdict cached;
    cached.isSameEvent = true;
    cached.confidence = 0.99;
    store->recordVerdict("K-BTC", "P-BTC", cached);
    std::vector<std::string> acceptedOrder;
    MarketMatcher matcher(settings, model, *store, *channel);
    matcher.match(kalshi, polymarket, {}, [&acceptedOrder](const MatchedPair& pair) { acceptedOrder.push_back(pair.kalshiTicker); });
    ASSERT_FALSE(acceptedOrder.empty());
    EXPECT_EQ(acceptedOrder.front(), "K-BTC");
}

TEST_F(MarketMatcherTest, PromptIncludesPolymarketEventAndDeadline)
{
    MarketMatcher matcher(settings, model, *store, *channel);
    MarketInfo polymarketMarket = polymarket[0];
    polymarketMarket.eventTitle = "Fed decision in December";
    polymarketMarket.resolutionDeadlineSeconds = polymarketMarket.closeTimeSeconds + 7 * 86400;
    const auto messages = matcher.buildMessages(kalshi[0], polymarketMarket);
    EXPECT_NE(messages[1].content.find("Event: Fed decision in December"), std::string::npos);
    EXPECT_NE(messages[1].content.find("Resolution deadline:"), std::string::npos);
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
    EXPECT_EQ(body["reasoning_effort"], "low");
    EXPECT_FALSE(body.contains("response_format"));

    settings.requestJsonObject = true;
    settings.reasoningEffort.clear();
    const auto jsonModeBody = nlohmann::json::parse(ChatCompletionsClient::buildRequestBody(settings, {{"user", "u"}}));
    EXPECT_EQ(jsonModeBody["response_format"]["type"], "json_object");
    EXPECT_FALSE(jsonModeBody.contains("reasoning_effort"));
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
