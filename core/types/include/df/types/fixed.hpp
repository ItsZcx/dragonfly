#pragma once

#include <cassert>
#include <cstdint>
#include <limits>

namespace df
{

inline constexpr std::int64_t kScale = 1'000'000'000;

// (a * b) / kScale, computed at 128 bits so the product cannot wrap.
constexpr std::int64_t MulDiv(std::int64_t a, std::int64_t b)
{
    const __int128 kWide = (static_cast<__int128>(a) * b) / kScale;

    assert(kWide >= std::numeric_limits<std::int64_t>::min() && kWide <= std::numeric_limits<std::int64_t>::max());
    return static_cast<std::int64_t>(kWide);
}

}  // namespace df
