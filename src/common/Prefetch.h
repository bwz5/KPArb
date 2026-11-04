#pragma once

namespace kparb {

inline void prefetchForRead(const void* address) noexcept
{
    __builtin_prefetch(address, 0, 3);
}

inline void prefetchForWrite(const void* address) noexcept
{
    __builtin_prefetch(address, 1, 3);
}

}
