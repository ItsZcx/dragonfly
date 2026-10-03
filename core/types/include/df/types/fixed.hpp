#pragma once

#include <cassert>
#include <cstdint>
#include <limits>

namespace df
{

inline constexpr std::int64_t kScale = 1'000'000'000;

// (a * b) / kScale, computed at 128 bits so the product cannot wrap.
//
// Both operands are scaled by kScale, so their product is scaled twice. Dividing
// by kScale puts it back. A raw int64_t product overflows above $9.22 at a
// quantity of 1.0: 1.0 BTC x $100,000 is 1e23 against an int64_t ceiling of
// 9.22e18. The wrapped result looks like a plausible number. That is why the
// widening is a rule instead of a runtime check.
//
// Precondition: the 128-bit result must fit in int64_t. L3 enforces this at the
// boundary. Its check 4 caps quantity with max_order_qty as a magnitude, so the
// cap covers sells. Its check 5 caps price with the collar. Together they hold
// |notional| far below INT64_MAX. The margin is wide: with quantity capped at
// 1.0, a price needs nineteen digits to overflow the result. The remaining
// dependency is a sane mid, which L1 provides. See docs/layers.md, L3 (risk)
// and L1 (ingestion).
//
// If the precondition fails, the assert aborts in a Debug build. Under NDEBUG,
// which covers Release and RelWithDebInfo, the assert compiles out and the
// narrowing cast is undefined behaviour. The assert is a development tripwire
// and not protection, because the boundary check above does the enforcing.
//
// The weighted-average cost basis is the one case that widens without dividing.
// Its numerator is divided by a scaled value. That case uses an explicit cast
// instead of MulDiv(a, 1), so the two situations cannot be confused.
constexpr std::int64_t MulDiv(std::int64_t a, std::int64_t b)
{
    const __int128 kWide = (static_cast<__int128>(a) * b) / kScale;

    assert(kWide >= std::numeric_limits<std::int64_t>::min() && kWide <= std::numeric_limits<std::int64_t>::max());
    return static_cast<std::int64_t>(kWide);
}

}  // namespace df
