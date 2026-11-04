#include "common/EnvironmentFile.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kparb {

namespace {

std::string_view trimWhitespace(std::string_view text)
{
    const auto isSpace = [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string unescapeDoubleQuoted(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (std::size_t position = 0; position < text.size(); ++position) {
        const char character = text[position];
        if (character == '\\' && position + 1 < text.size()) {
            const char escaped = text[++position];
            switch (escaped) {
            case 'n':
                result.push_back('\n');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'r':
                result.push_back('\r');
                break;
            default:
                result.push_back(escaped);
                break;
            }
        } else {
            result.push_back(character);
        }
    }
    return result;
}

}

std::vector<std::pair<std::string, std::string>> parseEnvironmentFileContents(std::string_view contents)
{
    std::vector<std::pair<std::string, std::string>> entries;
    std::size_t lineStart = 0;
    while (lineStart <= contents.size()) {
        std::size_t lineEnd = contents.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = contents.size();
        }
        std::string_view line = trimWhitespace(contents.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;

        if (line.empty() || line.front() == '#') {
            continue;
        }
        constexpr std::string_view exportPrefix = "export ";
        if (line.substr(0, exportPrefix.size()) == exportPrefix) {
            line.remove_prefix(exportPrefix.size());
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trimWhitespace(line.substr(0, separator));
        std::string_view rawValue = trimWhitespace(line.substr(separator + 1));
        if (key.empty()) {
            continue;
        }

        std::string value;
        if (rawValue.size() >= 2 && rawValue.front() == '"' && rawValue.back() == '"') {
            value = unescapeDoubleQuoted(rawValue.substr(1, rawValue.size() - 2));
        } else if (rawValue.size() >= 2 && rawValue.front() == '\'' && rawValue.back() == '\'') {
            value = std::string(rawValue.substr(1, rawValue.size() - 2));
        } else {
            value = std::string(rawValue);
        }
        entries.emplace_back(std::string(key), std::move(value));
    }
    return entries;
}

std::size_t loadEnvironmentFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return 0;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::size_t applied = 0;
    for (const auto& [key, value] : parseEnvironmentFileContents(buffer.str())) {
        if (std::getenv(key.c_str()) == nullptr) {
            ::setenv(key.c_str(), value.c_str(), 0);
            ++applied;
        }
    }
    return applied;
}

std::optional<std::string> readEnvironmentVariable(const std::string& name)
{
    const char* value = std::getenv(name.c_str());
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

}
