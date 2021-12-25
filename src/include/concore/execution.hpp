#pragma once

#include <concore/detail/cxx_features.hpp>

#if CONCORE_USE_CXX2020 && CONCORE_CPP_VERSION >= 20
#include <concore/_gen/c++20/execution.hpp>

namespace  concore {
    using namespace execution = concore::concore::detail::execution;
}
#endif
