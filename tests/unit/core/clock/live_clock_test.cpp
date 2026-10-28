// Unit tests for core/clock/include/df/clock/clock.hpp.

// Proves the rule in docs/decisions.md, "Timestamps are monotonic, never the
// CPU cycle counter": a reading comes from CLOCK_MONOTONIC, so a later reading
// never comes before an earlier one.

// Obligation: issue #30, "core/clock: add LiveClock over CLOCK_MONOTONIC".

// The file checks two separate things.

//   1. That 1000 readings in a row never decrease. This is the failure a test
//      can reach: a wrong clock id. A clock that steps cannot be provoked
//      without replacing the syscall, which would test the replacement.
//   2. That two readings in a row differ by less than a second, which is how
//      the test shows the unit is the nanosecond.

#include "gtest/gtest.h"
#include <cstdint>
#include <df/clock/clock.hpp>

namespace
{

constexpr int kReadings = 1000;

}  // namespace

TEST(CoreClockLive, ReadingsNeverDecrease)
{
    df::clock::LiveClock clock;

    std::uint64_t previous = clock.NsNow();
    for (int i = 0; i < kReadings; ++i)
    {
        const std::uint64_t kNow = clock.NsNow();
        ASSERT_GE(kNow, previous) << "reading " << i << " is earlier than the reading before it";
        previous = kNow;
    }
}

TEST(CoreClockLive, ReadsNanoseconds)
{
    df::clock::LiveClock clock;

    const std::uint64_t kFirst  = clock.NsNow();
    const std::uint64_t kSecond = clock.NsNow();

    ASSERT_LT(kSecond - kFirst, std::uint64_t{1'000'000'000}) << "two readings are a second apart or more";
}
