// Unit tests for the clock seam: one consumer, driven through both clocks.

// Proves that code written against IClock& behaves the same live and in
// backtest. The consumer never learns which clock it was given, which is the
// point of the seam in phases.md, P2.

// Obligation: issue #32, "tests/unit: drive one consumer through both clocks".

// The file checks three separate things.

//   1. Under SimulationClock the consumer returns exactly the time its work
//      advanced the clock by, and two runs of the same work agree.
//   2. Under LiveClock the consumer returns a duration under a second, because
//      the live clock cannot be given a chosen duration.
//   3. That the consumer takes IClock& and names no concrete clock, which its
//      signature and body show.

// The work is a parameter because the consumer sits between two readings, and
// only the work can move a SimulationClock between them.

#include "gtest/gtest.h"
#include <cstdint>
#include <df/clock/clock.hpp>

namespace
{

constexpr std::uint64_t kStepNs = 1'000;
constexpr std::uint64_t kSecond = 1'000'000'000;

// Stands in for a component that times its own work: read the clock, run the
// work, read again. It names IClock and no concrete clock.
template <typename Work> std::uint64_t Measure(df::clock::IClock& clock, int steps, Work work)
{
    const std::uint64_t kStart = clock.NsNow();
    for (int i = 0; i < steps; ++i)
    {
        work();
    }
    return clock.NsNow() - kStart;
}

}  // namespace

TEST(CoreClockSeam, SimulationMeasuresTheAdvance)
{
    df::clock::SimulationClock clock;

    const std::uint64_t kMeasured = Measure(clock, 3, [&clock] { clock.ClockAdvanceTo(clock.NsNow() + kStepNs); });

    EXPECT_EQ(kMeasured, 3 * kStepNs);
}

TEST(CoreClockSeam, SimulationRunsAgree)
{
    const auto kRun = [] {
        df::clock::SimulationClock clock;
        return Measure(clock, 3, [&clock] { clock.ClockAdvanceTo(clock.NsNow() + kStepNs); });
    };

    EXPECT_EQ(kRun(), kRun());
}

TEST(CoreClockSeam, LiveMeasuresUnderASecond)
{
    df::clock::LiveClock clock;

    const std::uint64_t kMeasured = Measure(clock, 1'000, [] {});

    EXPECT_LT(kMeasured, kSecond);
}
