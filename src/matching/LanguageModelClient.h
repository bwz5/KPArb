#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kparb {

struct ChatMessage {
    std::string role;
    std::string content;
};

struct LanguageModelResponse {
    bool succeeded = false;
    std::string content;
    std::string errorMessage;
    std::int64_t retryAfterSeconds = 0;
    std::string model;
};

class LanguageModelClient {
public:
    virtual ~LanguageModelClient() = default;
    virtual LanguageModelResponse complete(const std::vector<ChatMessage>& messages) = 0;
    virtual std::string modelName() const = 0;
};

}
