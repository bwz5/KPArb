#pragma once

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
};

class LanguageModelClient {
public:
    virtual ~LanguageModelClient() = default;
    virtual LanguageModelResponse complete(const std::vector<ChatMessage>& messages) = 0;
    virtual std::string modelName() const = 0;
};

}
