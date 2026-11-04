#include "ingestion/GzipDecoding.h"

#include <gtest/gtest.h>
#include <zlib.h>

#include <string>

namespace kparb {
namespace {

std::string compressGzip(const std::string& plain)
{
    z_stream stream{};
    deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string output(deflateBound(&stream, static_cast<uLong>(plain.size())) + 32, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(plain.data()));
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    deflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

TEST(GzipDecodingTest, RecognizesAndInflatesGzipBodies)
{
    std::string plain = "{\"markets\":[";
    for (int index = 0; index < 20000; ++index) {
        plain += "{\"ticker\":\"KXNHLGAME-26NOV04CGYSEA-CGY\"},";
    }
    plain += "{}]}";
    const std::string compressed = compressGzip(plain);
    EXPECT_TRUE(isGzipCompressed(compressed));
    EXPECT_FALSE(isGzipCompressed(plain));
    const auto inflated = decompressGzip(compressed);
    ASSERT_TRUE(inflated.has_value());
    EXPECT_EQ(*inflated, plain);
}

TEST(GzipDecodingTest, TruncatedInputFails)
{
    const std::string compressed = compressGzip(std::string(100000, 'k'));
    EXPECT_FALSE(decompressGzip(compressed.substr(0, compressed.size() / 2)).has_value());
    EXPECT_FALSE(decompressGzip("\x1f\x8bnot gzip").has_value());
}

}
}
