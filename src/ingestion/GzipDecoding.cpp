#include "ingestion/GzipDecoding.h"

#include <zlib.h>

#include <array>

namespace kparb {

bool isGzipCompressed(std::string_view body) noexcept
{
    return body.size() >= 2 && static_cast<unsigned char>(body[0]) == 0x1F && static_cast<unsigned char>(body[1]) == 0x8B;
}

std::optional<std::string> decompressGzip(std::string_view compressed)
{
    constexpr int gzipWindowBits = 16 + MAX_WBITS;
    z_stream stream{};
    if (inflateInit2(&stream, gzipWindowBits) != Z_OK) {
        return std::nullopt;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    std::string output;
    std::array<char, 65536> buffer{};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&stream);
            return std::nullopt;
        }
        output.append(buffer.data(), buffer.size() - stream.avail_out);
        if (status == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) {
            inflateEnd(&stream);
            return std::nullopt;
        }
    }
    inflateEnd(&stream);
    return output;
}

}
