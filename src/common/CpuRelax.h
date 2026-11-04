#pragma once

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace kparb {

inline void cpuRelax() noexcept
{
#if defined(__aarch64__) || defined(__arm__)
    __builtin_arm_yield();
#elif defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#endif
}

}
