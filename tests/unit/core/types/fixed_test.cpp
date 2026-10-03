// Unit tests for core/types/include/df/types/fixed.hpp.

// Proves the rule in docs/contracts.md, "Numbers are fixed-point integers": a
// product of two kScale-scaled values widens to 128 bits before it divides.

// Obligation: issue #1, "core/types: add fixed.hpp with kScale and MulDiv".

// The file checks two separate things.

//   1. The result of MulDiv. It divides by kScale, so it survives operand pairs
//      whose raw product wraps.
//   2. The raw int64_t product. The boundary table in docs/contracts.md
//      describes this product: 1.0 x $9.22 is the ceiling, because 1e9 * 9.22e9
//      is exactly INT64_MAX. The table argues for why MulDiv must exist. It is
//      not a set of MulDiv cases, because MulDiv divides the answer back down.

#include "gtest/gtest.h"
#include <cstdint>
#include <df/types/fixed.hpp>
#include <limits>

namespace
{

using df::kScale;
using df::MulDiv;

// Decomposes a decimal literal into fixed-point. The helper never goes through
// floating point, because a test that wrote 105.5 as a double would check the
// compiler's rounding instead of our arithmetic.

// The sign comes from `whole`, and falls back to `nano` when `whole` is zero.
// The helper applies that sign to the magnitude, so a mixed decimal reads the
// way it is written, and a fractional-only value keeps its sign:

//   Fixed(-1, 500'000'000)  ==  -1.5     (not -0.5)
//   Fixed(0, -500'000'000)  ==  -0.5     (not +0.5)

// The plain form `whole * kScale + nano` returns -0.5 for the first case. It
// treats the two parts as separate numbers with separate signs. A decimal has
// one sign for the whole value.
constexpr std::int64_t Fixed(std::int64_t whole, std::int64_t nano = 0)
{
    const std::int64_t kMagnitude = whole < 0 ? -whole : whole;
    const std::int64_t kFraction = nano < 0 ? -nano : nano;

    const std::int64_t kValue = (kMagnitude * kScale) + kFraction;

    // The sign lives in `whole`. It lives in the fraction only when `whole` is
    // zero, which is the one case where the fraction is the whole value.
    const bool kNegative = whole < 0 || (whole == 0 && nano < 0);
    return kNegative ? -kValue : kValue;
}

// ---------------------------------------------------------------------------
// kScale, and the two conversions docs/contracts.md gives verbatim.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, ScaleMatchesTheDocumentedConstant)
{ EXPECT_EQ(kScale, 1'000'000'000); }

TEST(CoreTypesFixed, DocumentedConversionsHold)
{
    // $105.50 -> 105_500_000_000
    EXPECT_EQ(Fixed(105, 500'000'000), 105'500'000'000);
    // 1.25 BTC -> 1_250_000_000
    EXPECT_EQ(Fixed(1, 250'000'000), 1'250'000'000);
}

// The helper takes its sign from `whole`, so a mixed decimal reads the way it
// is written. This test guards a regression: the plain form returns -0.5 for
// the first case below, and a short position written that way would check the
// wrong number.
TEST(CoreTypesFixed, TheLiteralHelperHandlesSigns)
{
    EXPECT_EQ(Fixed(-1), -1'000'000'000);
    EXPECT_EQ(Fixed(-1, 500'000'000), -1'500'000'000);
    EXPECT_EQ(Fixed(1, 500'000'000), 1'500'000'000);
    EXPECT_EQ(Fixed(-1, -500'000'000), -1'500'000'000);
    EXPECT_EQ(Fixed(0, 500'000'000), 500'000'000);
    EXPECT_EQ(Fixed(0, -500'000'000), -500'000'000);
}

// ---------------------------------------------------------------------------
// The case MulDiv exists for. docs/contracts.md calls this operand pair "about
// ten thousand times too large" for a raw int64_t product.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, NotionalOfOneUnitAtOneHundredThousandIsExact)
{
    const std::int64_t kQty = Fixed(1);
    const std::int64_t kPrice = Fixed(100'000);

    // $100,000, as a scaled value.
    EXPECT_EQ(MulDiv(kQty, kPrice), Fixed(100'000));
}

TEST(CoreTypesFixed, WideningIsWhatMakesThatCaseWork)
{
    // Plain locals, so the multiply runs at runtime. Clang folds a literal
    // expression at compile time and diagnoses the overflow as
    // -Winteger-overflow, which -Werror makes fatal. The runtime behaviour is
    // the one worth pinning.
    std::int64_t qty = Fixed(1);
    std::int64_t price = Fixed(100'000);

    const std::int64_t kRaw = qty * price;

    // The same operands through a plain int64_t multiply. Signed overflow is
    // undefined behaviour, so this asserts only that the unguarded product is
    // NOT the correct answer, and not a specific wrapped value. That product is
    // the bug MulDiv prevents, and it is why the widening is a written rule
    // instead of a runtime check.
    EXPECT_NE(kRaw, Fixed(100'000));
    EXPECT_EQ(MulDiv(qty, price), Fixed(100'000));
}

// The exact product, computed at 128 bits so the magnitude is visible instead
// of clipped by an int64_t. The product is 1e23 against a ceiling of 9.22e18.
TEST(CoreTypesFixed, TheRawProductOverflowsByFourOrdersOfMagnitude)
{
    constexpr std::int64_t kQty = Fixed(1);
    constexpr std::int64_t kPrice = Fixed(100'000);
    const __int128 kRaw = static_cast<__int128>(kQty) * kPrice;

    EXPECT_GT(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::max()));
}

// ---------------------------------------------------------------------------
// The boundary table in docs/contracts.md. It describes the raw product, so the
// test checks the raw product. At 1.0 x $9.22 the product sits at the int64_t
// ceiling.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, RawProductAtTheDocumentedBoundary)
{
    constexpr std::int64_t kQty = Fixed(1);
    constexpr std::int64_t kPrice = Fixed(9, 220'000'000);  // $9.22

    const __int128 kRaw = static_cast<__int128>(kQty) * kPrice;

    // 1e9 * 9.22e9 == 9.22e18, which is INT64_MAX to three significant figures.
    EXPECT_LE(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::max()));
    EXPECT_GT(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::max()) / 10);
}

// The same boundary one row down the table. A quantity of 0.01 lifts the
// ceiling to $922, because the quantity is 100 times smaller.
TEST(CoreTypesFixed, RawProductBoundaryScalesWithQuantity)
{
    constexpr std::int64_t kQty = Fixed(0, 10'000'000);  // 0.01
    constexpr std::int64_t kPrice = Fixed(922);          // $922

    const __int128 kRaw = static_cast<__int128>(kQty) * kPrice;

    EXPECT_LE(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::max()));
    EXPECT_GT(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::max()) / 10);
}

// ---------------------------------------------------------------------------
// Signs. A sell is a negative quantity, and signed overflow runs in two
// directions. Tests usually cover the positive direction, so this covers the
// other one.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, ASellIsTheNegationOfABuy)
{
    const std::int64_t kQty = Fixed(1);
    const std::int64_t kPrice = Fixed(100'000);

    EXPECT_EQ(MulDiv(-kQty, kPrice), -MulDiv(kQty, kPrice));
    EXPECT_EQ(MulDiv(-kQty, kPrice), Fixed(-100'000));
}

TEST(CoreTypesFixed, BothOperandsNegativeIsPositive)
{
    const std::int64_t kQty = Fixed(-1);
    const std::int64_t kPrice = Fixed(-100'000);

    EXPECT_EQ(MulDiv(kQty, kPrice), Fixed(100'000));
}

TEST(CoreTypesFixed, TheNegativeBoundaryDoesNotWrapSilently)
{
    std::int64_t qty = Fixed(1);
    std::int64_t price = Fixed(100'000);

    const __int128 kRaw = static_cast<__int128>(-qty) * price;

    EXPECT_LT(kRaw, static_cast<__int128>(std::numeric_limits<std::int64_t>::min()));
}

// ---------------------------------------------------------------------------
// Exactness, and the one case that is not an overflow: truncation to zero.
// Sub-nano notionals are lost, which is a property of the fixed-point grid and
// not a bug. The test pins that behaviour, so a later change to rounding is a
// deliberate act.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, SmallProductsTruncateTowardZero)
{
    // 1e-9 units at $1 is $1e-9, which is one nano-unit and is representable.
    EXPECT_EQ(MulDiv(1, Fixed(1)), 1);

    // Half a nano-unit truncates to zero. The result goes toward zero for both
    // signs, so this truncates and does not round.
    EXPECT_EQ(MulDiv(1, Fixed(0, 500'000'000)), 0);
    EXPECT_EQ(MulDiv(-1, Fixed(0, 500'000'000)), 0);
}

TEST(CoreTypesFixed, ExactValuesRoundTripAtTheScale)
{
    // A value with nine decimal places survives conversion and back.
    constexpr std::int64_t kValue = 123'456'789'012'345;

    EXPECT_EQ(MulDiv(kValue, kScale), kValue);
    EXPECT_EQ(MulDiv(kScale, kValue), kValue);
}

// ---------------------------------------------------------------------------
// Compile-time use. docs/contracts.md relies on messages asserting their own
// layout, which needs MulDiv in a constant expression.
// ---------------------------------------------------------------------------

TEST(CoreTypesFixed, MulDivWorksInAConstantExpression)
{
    // static_assert also means a wrong MulDiv fails the build instead of a test
    // run. It is the earliest place an arithmetic mistake can be caught.
    static_assert(MulDiv(Fixed(1), Fixed(100'000)) == Fixed(100'000));
    static_assert(MulDiv(Fixed(2), Fixed(3)) == Fixed(6));
    static_assert(MulDiv(-Fixed(2), Fixed(3)) == Fixed(-6));
    SUCCEED();
}

}  // namespace
