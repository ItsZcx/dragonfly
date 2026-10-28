// Unit tests for SimulationClock, from core/clock/include/df/clock/clock.hpp.

// Proves that a replay reads the times the driver sets, and only those times.
// Nothing moves this clock except ClockAdvanceTo, so two runs over the same
// events produce the same readings.

// Obligation: issue #31, "core/clock: add SimulationClock with an explicit advance".

// The file checks three separate things.

//   1. That a fixed sequence of advances produces exactly the times that were
//      set, starting from zero, and that repeating a timestamp is allowed.
//   2. That ClockAdvanceTo is absent from IClock, so code holding an IClock&
//      cannot move time.
//   3. That a backwards advance trips the assert, where the assert exists.

#include "gtest/gtest.h"
#include <cstdint>
#include <df/clock/clock.hpp>

namespace
{

// Check 2, at compile time. Adding ClockAdvanceTo to IClock makes the first
// assertion fail, which is the point: the seam hands out IClock& and must not
// hand out the ability to move time. The type is a template parameter because a
// name lookup that fails on a concrete type is a hard error, not a requirement
// that evaluates to false.
template <typename T>
concept CanAdvance = requires(T& clock_ref, std::uint64_t ts_ns) { clock_ref.ClockAdvanceTo(ts_ns); };

static_assert(!CanAdvance<df::clock::IClock>, "ClockAdvanceTo must not exist on IClock");
static_assert(CanAdvance<df::clock::SimulationClock>, "ClockAdvanceTo must exist on SimulationClock");

}  // namespace

TEST(CoreClockSimulation, StartsAtZero)
{
    const df::clock::SimulationClock kClock;

    EXPECT_EQ(kClock.NsNow(), std::uint64_t{0});
}

TEST(CoreClockSimulation, FixedSequenceOfAdvances)
{
    df::clock::SimulationClock clock;

    clock.ClockAdvanceTo(1'000);
    EXPECT_EQ(clock.NsNow(), std::uint64_t{1'000});

    clock.ClockAdvanceTo(2'500);
    EXPECT_EQ(clock.NsNow(), std::uint64_t{2'500});

    // Two events in the same nanosecond are legal, so the guard is >= and not >.
    clock.ClockAdvanceTo(2'500);
    EXPECT_EQ(clock.NsNow(), std::uint64_t{2'500});

    clock.ClockAdvanceTo(1'000'000'000);
    EXPECT_EQ(clock.NsNow(), std::uint64_t{1'000'000'000});
}

// The assert compiles out under NDEBUG, so the backwards case exists only where
// the guard does. The debug preset is the build that runs it.
#ifndef NDEBUG
TEST(CoreClockSimulation, BackwardsAdvanceTripsTheAssert)
{
    df::clock::SimulationClock clock;
    clock.ClockAdvanceTo(1'000);

    EXPECT_DEATH(clock.ClockAdvanceTo(900), "ts_ns >= now_ns_");
}
#endif
