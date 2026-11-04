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

std::string extractUrlOrigin(std::string_view url)
{
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return {};
    }
    std::string scheme(url.substr(0, schemeEnd));
    if (scheme == "wss") {
        scheme = "https";
    } else if (scheme == "ws") {
        scheme = "http";
    }
    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd = url.find_first_of("/?", hostStart);
    std::string host(url.substr(hostStart, hostEnd == std::string_view::npos ? std::string_view::npos : hostEnd - hostStart));
    const std::size_t portStart = host.find(':');
    const bool isDefaultPort = portStart != std::string::npos
        && ((scheme == "https" && host.substr(portStart) == ":443") || (scheme == "http" && host.substr(portStart) == ":80"));
    if (isDefaultPort) {
        host.erase(portStart);
    }
    return scheme + "://" + host;
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
