#include "ingestion/UrlEncoding.h"

namespace kparb {

std::string encodeUrlComponent(std::string_view text)
{
    static constexpr char hexadecimalDigits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size() * 3);
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        const bool isUnreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (isUnreserved) {
            encoded.push_back(character);
        } else {
            encoded.push_back('%');
            encoded.push_back(hexadecimalDigits[byte >> 4]);
            encoded.push_back(hexadecimalDigits[byte & 0x0F]);
        }
    }
    return encoded;
}

std::string extractUrlPath(std::string_view url)
{
    const std::size_t schemeEnd = url.find("://");
    const std::size_t hostStart = schemeEnd == std::string_view::npos ? 0 : schemeEnd + 3;
    const std::size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string_view::npos) {
        return "/";
    }
    const std::size_t queryStart = url.find('?', pathStart);
    return std::string(url.substr(pathStart, queryStart == std::string_view::npos ? std::string_view::npos : queryStart - pathStart));
}

}
