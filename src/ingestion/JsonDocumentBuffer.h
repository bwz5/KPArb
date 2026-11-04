#pragma once

#include <simdjson.h>

#include <cstddef>
#include <memory>
#include <string_view>

namespace kparb {

class JsonDocumentBuffer {
public:
    explicit JsonDocumentBuffer(std::size_t initialCapacity);

    simdjson::error_code parse(std::string_view text, simdjson::dom::element& root);

private:
    void ensureCapacity(std::size_t size);

    std::unique_ptr<char[]> storage;
    std::size_t storageCapacity = 0;
    simdjson::dom::parser parser;
};

}
