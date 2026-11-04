#pragma once

#include <cstddef>

namespace kparb::testing_support {

std::size_t allocationCount() noexcept;

class AllocationScope {
public:
    AllocationScope() noexcept;
    std::size_t allocationsSinceStart() const noexcept;

private:
    std::size_t startCount;
};

}
