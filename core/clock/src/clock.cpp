#include "df/clock/clock.hpp"

#include <cassert>
#include <ctime>

std::uint64_t df::clock::LiveClock::NsNow() const
{
    std::timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (1'000'000'000 * static_cast<std::uint64_t>(ts.tv_sec)) + ts.tv_nsec;
}

std::uint64_t df::clock::SimulationClock::NsNow() const
{ return now_ns_; }

void df::clock::SimulationClock::ClockAdvanceTo(std::uint64_t ts_ns)
{
    assert(ts_ns >= now_ns_);
    now_ns_ = ts_ns;
}
