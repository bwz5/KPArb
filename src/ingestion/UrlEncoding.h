#pragma once

#include <string>
#include <string_view>

namespace kparb {

std::string encodeUrlComponent(std::string_view text);

std::string extractUrlPath(std::string_view url);

std::string extractUrlOrigin(std::string_view url);

}
