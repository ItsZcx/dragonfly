#include "df/clock/clock.hpp"

#include <ctime>

std::uint64_t df::clock::LiveClock::NsNow() const
{
    std::timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (1'000'000'000 * static_cast<std::uint64_t>(ts.tv_sec)) + ts.tv_nsec;
}
