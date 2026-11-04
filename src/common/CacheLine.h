#pragma once

#include <cstddef>

namespace kparb {

#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr std::size_t cacheLineSize = 128;
#else
inline constexpr std::size_t cacheLineSize = 64;
#endif

template <typename Value>
struct alignas(cacheLineSize) CacheLinePadded {
    Value value{};
};

}
