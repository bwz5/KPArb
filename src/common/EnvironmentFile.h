#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kparb {

std::vector<std::pair<std::string, std::string>> parseEnvironmentFileContents(std::string_view contents);

std::size_t loadEnvironmentFile(const std::filesystem::path& path);

std::optional<std::string> readEnvironmentVariable(const std::string& name);

}
