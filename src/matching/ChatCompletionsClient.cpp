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
    models.push_back(settings.model);
    for (const std::string& fallback : settings.fallbackModels) {
        if (!fallback.empty() && std::find(models.begin(), models.end(), fallback) == models.end()) {
            models.push_back(fallback);
        }
    }
    modelAvailableAt.assign(models.size(), std::chrono::steady_clock::now());
}

std::string ChatCompletionsClient::modelName() const
{
    return settings.model;
}

std::string ChatCompletionsClient::buildRequestBody(const ChatCompletionsSettings& settings, const std::vector<ChatMessage>& messages)
{
    return buildRequestBody(settings, settings.model, messages);
}

std::string ChatCompletionsClient::buildRequestBody(const ChatCompletionsSettings& settings, const std::string& model,
    const std::vector<ChatMessage>& messages)
{
    nlohmann::json body{
        {"model", model},
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
    const double intervalSeconds = settings.requestsPerMinute > 0.0 ? 60.0 / settings.requestsPerMinute : 0.0;
    const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(intervalSeconds));
    std::chrono::steady_clock::time_point slot;
    {
        const std::lock_guard<std::mutex> lock(rateLimitMutex);
        slot = std::max(std::chrono::steady_clock::now(), nextAllowedRequest);
        nextAllowedRequest = slot + interval;
    }
    std::this_thread::sleep_until(slot);
}

std::size_t ChatCompletionsClient::availableModel(std::int64_t& secondsUntilAvailable)
{
    const std::lock_guard<std::mutex> lock(rateLimitMutex);
    const auto now = std::chrono::steady_clock::now();
    auto soonest = std::chrono::steady_clock::time_point::max();
    for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
        if (modelAvailableAt[modelIndex] <= now) {
            secondsUntilAvailable = 0;
            return modelIndex;
        }
        soonest = std::min(soonest, modelAvailableAt[modelIndex]);
    }
    secondsUntilAvailable = std::max<std::int64_t>(1, std::chrono::ceil<std::chrono::seconds>(soonest - now).count());
    return models.size();
}

void ChatCompletionsClient::pauseModel(std::size_t modelIndex, std::int64_t seconds)
{
    const std::lock_guard<std::mutex> lock(rateLimitMutex);
    modelAvailableAt[modelIndex] = std::max(modelAvailableAt[modelIndex], std::chrono::steady_clock::now() + std::chrono::seconds(seconds));
}

LanguageModelResponse ChatCompletionsClient::complete(const std::vector<ChatMessage>& messages)
{
    constexpr std::int64_t unavailableModelPauseSeconds = 3600;
    const std::string url = settings.baseUrl + "/chat/completions";
    HttpHeaders headers{{"Content-Type", "application/json"}};
    if (!settings.apiKey.empty()) {
        headers.emplace_back("Authorization", "Bearer " + settings.apiKey);
    }

    HttpClient httpClient;
    LanguageModelResponse lastFailure;
    for (int attempt = 0; attempt < settings.maximumAttempts; ++attempt) {
        std::int64_t secondsUntilAvailable = 0;
        const std::size_t modelIndex = availableModel(secondsUntilAvailable);
        if (modelIndex == models.size()) {
            lastFailure.errorMessage = "every model is waiting for quota; " + lastFailure.errorMessage;
            lastFailure.retryAfterSeconds = secondsUntilAvailable;
            return lastFailure;
        }
        waitForRateLimit();
        const HttpResponse response = httpClient.post(url, buildRequestBody(settings, models[modelIndex], messages), headers);
        if (response.isSuccess()) {
            LanguageModelResponse parsed = parseResponseBody(response.body);
            parsed.model = models[modelIndex];
            return parsed;
        }
        lastFailure.errorMessage = models[modelIndex] + ": HTTP " + std::to_string(response.statusCode) + " " + response.errorMessage + " "
            + response.body.substr(0, 300);
        lastFailure.retryAfterSeconds = response.retryAfterSeconds;
        if (response.statusCode == 404 && models.size() > 1) {
            pauseModel(modelIndex, unavailableModelPauseSeconds);
            continue;
        }
        const bool isRetryable = response.statusCode == 0 || response.statusCode == 429 || response.statusCode >= 500;
        if (!isRetryable) {
            break;
        }
        if (response.retryAfterSeconds > settings.maximumRetryAfterSeconds) {
            pauseModel(modelIndex, response.retryAfterSeconds);
            continue;
        }
        const std::int64_t backoffSeconds = response.retryAfterSeconds > 0 ? response.retryAfterSeconds
                                                                           : std::min<std::int64_t>(60, 2LL << attempt);
        if (models.size() > 1) {
            pauseModel(modelIndex, backoffSeconds);
            continue;
        }
        std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
    }
    return lastFailure;
}

}
