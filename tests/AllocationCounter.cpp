#include "AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> totalAllocations{0};

void* allocateOrThrow(std::size_t size)
{
    totalAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::align_val_t alignment)
{
    totalAllocations.fetch_add(1, std::memory_order_relaxed);
    void* pointer = nullptr;
    const auto alignmentBytes = static_cast<std::size_t>(alignment);
    if (posix_memalign(&pointer, alignmentBytes < sizeof(void*) ? sizeof(void*) : alignmentBytes, size == 0 ? 1 : size) == 0) {
        return pointer;
    }
    throw std::bad_alloc();
}

}

void* operator new(std::size_t size)
{
    return allocateOrThrow(size);
}

void* operator new[](std::size_t size)
{
    return allocateOrThrow(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow(size, alignment);
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}

namespace kparb::testing_support {

std::size_t allocationCount() noexcept
{
    return totalAllocations.load(std::memory_order_relaxed);
}

AllocationScope::AllocationScope() noexcept
    : startCount(allocationCount())
{
}

std::size_t AllocationScope::allocationsSinceStart() const noexcept
{
    return allocationCount() - startCount;
}

}
