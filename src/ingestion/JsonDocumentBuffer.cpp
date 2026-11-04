#include "ingestion/JsonDocumentBuffer.h"

#include <cstring>

namespace kparb {

JsonDocumentBuffer::JsonDocumentBuffer(std::size_t initialCapacity)
{
    ensureCapacity(initialCapacity);
    static_cast<void>(parser.allocate(initialCapacity));
}

void JsonDocumentBuffer::ensureCapacity(std::size_t size)
{
    if (size <= storageCapacity) {
        return;
    }
    std::size_t newCapacity = storageCapacity == 0 ? 4096 : storageCapacity;
    while (newCapacity < size) {
        newCapacity *= 2;
    }
    storage = std::make_unique<char[]>(newCapacity + simdjson::SIMDJSON_PADDING);
    storageCapacity = newCapacity;
}

simdjson::error_code JsonDocumentBuffer::parse(std::string_view text, simdjson::dom::element& root)
{
    ensureCapacity(text.size());
    std::memcpy(storage.get(), text.data(), text.size());
    std::memset(storage.get() + text.size(), 0, simdjson::SIMDJSON_PADDING);
    return parser.parse(storage.get(), text.size(), false).get(root);
}

}
