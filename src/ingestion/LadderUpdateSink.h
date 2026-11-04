#pragma once

#include "arbitrage/LadderUpdate.h"

#include <concepts>
#include <type_traits>

namespace kparb {

class LadderUpdateSink {
public:
    template <typename Callable>
        requires(!std::same_as<std::remove_cvref_t<Callable>, LadderUpdateSink>)
    explicit LadderUpdateSink(Callable& callable) noexcept
        : context(&callable)
        , invoke([](void* target, const LadderUpdate& update) { (*static_cast<Callable*>(target))(update); })
    {
    }

    void operator()(const LadderUpdate& update) const
    {
        invoke(context, update);
    }

private:
    void* context;
    void (*invoke)(void*, const LadderUpdate&);
};

}
