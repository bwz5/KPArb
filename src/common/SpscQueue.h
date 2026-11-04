#pragma once

#include "common/CacheLine.h"
#include "common/Prefetch.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace kparb {

template <typename Element, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<Element>, "Element must be trivially copyable");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "Index atomics must be lock free");

public:
    static constexpr std::size_t capacity = Capacity;
    static constexpr std::size_t readPrefetchDistance = 4;

    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool tryPush(const Element& element) noexcept
    {
        const std::size_t currentWriteIndex = producer.writeIndex.load(std::memory_order_relaxed);
        if (currentWriteIndex - producer.cachedReadIndex == Capacity) {
            producer.cachedReadIndex = consumer.readIndex.load(std::memory_order_acquire);
            if (currentWriteIndex - producer.cachedReadIndex == Capacity) {
                return false;
            }
        }
        slots[currentWriteIndex & indexMask] = element;
        prefetchForWrite(&slots[(currentWriteIndex + 1) & indexMask]);
        producer.writeIndex.store(currentWriteIndex + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(Element& destination) noexcept
    {
        const std::size_t currentReadIndex = consumer.readIndex.load(std::memory_order_relaxed);
        if (currentReadIndex == consumer.cachedWriteIndex) {
            consumer.cachedWriteIndex = producer.writeIndex.load(std::memory_order_acquire);
            if (currentReadIndex == consumer.cachedWriteIndex) {
                return false;
            }
        }
        destination = slots[currentReadIndex & indexMask];
        prefetchForRead(&slots[(currentReadIndex + readPrefetchDistance) & indexMask]);
        consumer.readIndex.store(currentReadIndex + 1, std::memory_order_release);
        return true;
    }

    std::size_t tryPopBatch(Element* destination, std::size_t maximumCount) noexcept
    {
        const std::size_t currentReadIndex = consumer.readIndex.load(std::memory_order_relaxed);
        if (consumer.cachedWriteIndex - currentReadIndex < maximumCount) {
            consumer.cachedWriteIndex = producer.writeIndex.load(std::memory_order_acquire);
        }
        std::size_t available = consumer.cachedWriteIndex - currentReadIndex;
        if (available == 0) {
            return 0;
        }
        const std::size_t count = available < maximumCount ? available : maximumCount;
        for (std::size_t offset = 0; offset < count; ++offset) {
            destination[offset] = slots[(currentReadIndex + offset) & indexMask];
        }
        prefetchForRead(&slots[(currentReadIndex + count) & indexMask]);
        consumer.readIndex.store(currentReadIndex + count, std::memory_order_release);
        return count;
    }

    std::size_t approximateSize() const noexcept
    {
        const std::size_t currentWriteIndex = producer.writeIndex.load(std::memory_order_acquire);
        const std::size_t currentReadIndex = consumer.readIndex.load(std::memory_order_acquire);
        return currentWriteIndex - currentReadIndex;
    }

    bool isEmpty() const noexcept
    {
        return approximateSize() == 0;
    }

private:
    static constexpr std::size_t indexMask = Capacity - 1;

    struct alignas(cacheLineSize) ProducerState {
        std::atomic<std::size_t> writeIndex{0};
        std::size_t cachedReadIndex{0};
    };

    struct alignas(cacheLineSize) ConsumerState {
        std::atomic<std::size_t> readIndex{0};
        std::size_t cachedWriteIndex{0};
    };

    ProducerState producer;
    ConsumerState consumer;
    alignas(cacheLineSize) std::array<Element, Capacity> slots{};
};

}
