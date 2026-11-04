#pragma once

#include "ingestion/HttpClient.h"
#include "matching/LanguageModelClient.h"

#include <chrono>
#include <string>

namespace kparb {

struct ChatCompletionsSettings {
    std::string baseUrl = "https://api.groq.com/openai/v1";
    std::string model = "llama-3.3-70b-versatile";
    std::string apiKey;
    double requestsPerMinute = 25.0;
    int maximumAttempts = 6;
    int maximumOutputTokens = 300;
    bool requestJsonObject = true;
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
    HttpClient httpClient;
    std::chrono::steady_clock::time_point nextAllowedRequest = std::chrono::steady_clock::now();
};

}
