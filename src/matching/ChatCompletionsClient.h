#pragma once

#include "ingestion/HttpClient.h"
#include "matching/LanguageModelClient.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace kparb {

struct ChatCompletionsSettings {
    std::string baseUrl = "https://api.groq.com/openai/v1";
    std::string model = "openai/gpt-oss-120b";
    std::string apiKey;
    double requestsPerMinute = 250.0;
    int maximumAttempts = 6;
    int maximumOutputTokens = 400;
    std::string reasoningEffort = "low";
    bool requestJsonObject = false;
    std::int64_t maximumRetryAfterSeconds = 120;
};

class ChatCompletionsClient final : public LanguageModelClient {
public:
    explicit ChatCompletionsClient(ChatCompletionsSettings clientSettings);

    LanguageModelResponse complete(const std::vector<ChatMessage>& messages) override;
    std::string modelName() const override;

    static std::string buildRequestBody(const ChatCompletionsSettings& settings, const std::vector<ChatMessage>& messages);
    static LanguageModelResponse parseResponseBody(const std::string& body);

private:
    void waitForRateLimit();

    ChatCompletionsSettings settings;
    std::mutex rateLimitMutex;
    std::chrono::steady_clock::time_point nextAllowedRequest = std::chrono::steady_clock::now();
};

}
