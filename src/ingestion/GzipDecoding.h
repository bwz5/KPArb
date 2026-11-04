#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kparb {

bool isGzipCompressed(std::string_view body) noexcept;

std::optional<std::string> decompressGzip(std::string_view compressed);

}
