#include "matching/ChatCompletionsClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <thread>

namespace kparb {

ChatCompletionsClient::ChatCompletionsClient(ChatCompletionsSettings clientSettings)
    : settings(std::move(clientSettings))
{
    while (!settings.baseUrl.empty() && settings.baseUrl.back() == '/') {
        settings.baseUrl.pop_back();
    }
}

std::string ChatCompletionsClient::modelName() const
{
    return settings.model;
}

std::string ChatCompletionsClient::buildRequestBody(const ChatCompletionsSettings& settings, const std::vector<ChatMessage>& messages)
{
    nlohmann::json body{
        {"model", settings.model},
        {"temperature", 0},
        {"max_tokens", settings.maximumOutputTokens},
        {"messages", nlohmann::json::array()},
    };
    for (const ChatMessage& message : messages) {
        body["messages"].push_back({{"role", message.role}, {"content", message.content}});
    }
    if (settings.requestJsonObject) {
        body["response_format"] = {{"type", "json_object"}};
    }
    if (!settings.reasoningEffort.empty()) {
        body["reasoning_effort"] = settings.reasoningEffort;
    }
    return body.dump();
}

LanguageModelResponse ChatCompletionsClient::parseResponseBody(const std::string& body)
{
    LanguageModelResponse response;
    const auto document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded()) {
        response.errorMessage = "Response was not JSON";
        return response;
    }
    const auto choices = document.find("choices");
    if (choices == document.end() || !choices->is_array() || choices->empty()) {
        response.errorMessage = "Response had no choices";
        return response;
    }
    const auto& message = (*choices)[0].value("message", nlohmann::json::object());
    const auto content = message.find("content");
    if (content == message.end() || !content->is_string()) {
        response.errorMessage = "Response had no message content";
        return response;
    }
    response.succeeded = true;
    response.content = content->get<std::string>();
    return response;
}

void ChatCompletionsClient::waitForRateLimit()
{
    const auto now = std::chrono::steady_clock::now();
    if (now < nextAllowedRequest) {
        std::this_thread::sleep_until(nextAllowedRequest);
    }
    const double intervalSeconds = settings.requestsPerMinute > 0.0 ? 60.0 / settings.requestsPerMinute : 0.0;
    nextAllowedRequest = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(intervalSeconds));
}

LanguageModelResponse ChatCompletionsClient::complete(const std::vector<ChatMessage>& messages)
{
    const std::string url = settings.baseUrl + "/chat/completions";
    const std::string requestBody = buildRequestBody(settings, messages);
    HttpHeaders headers{{"Content-Type", "application/json"}};
    if (!settings.apiKey.empty()) {
        headers.emplace_back("Authorization", "Bearer " + settings.apiKey);
    }

    LanguageModelResponse lastFailure;
    for (int attempt = 0; attempt < settings.maximumAttempts; ++attempt) {
        waitForRateLimit();
        const HttpResponse response = httpClient.post(url, requestBody, headers);
        if (response.isSuccess()) {
            return parseResponseBody(response.body);
        }
        lastFailure.errorMessage = "HTTP " + std::to_string(response.statusCode) + " " + response.errorMessage + " "
            + response.body.substr(0, 300);
        lastFailure.retryAfterSeconds = response.retryAfterSeconds;
        const bool isRetryable = response.statusCode == 0 || response.statusCode == 429 || response.statusCode >= 500;
        if (!isRetryable || response.retryAfterSeconds > settings.maximumRetryAfterSeconds) {
            break;
        }
        const std::int64_t backoffSeconds = response.retryAfterSeconds > 0 ? response.retryAfterSeconds
                                                                           : std::min<std::int64_t>(60, 2LL << attempt);
        std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
    }
    return lastFailure;
}

}
