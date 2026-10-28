#pragma once

#include <cstdint>

namespace df::clock
{

class IClock
{
public:
    IClock() = default;

    IClock(const IClock&)            = delete;
    IClock(IClock&&)                 = delete;
    IClock& operator=(const IClock&) = delete;
    IClock& operator=(IClock&&)      = delete;

    [[nodiscard]] virtual std::uint64_t NsNow() const = 0;

protected:
    ~IClock() = default;  // cpp core guideline C.35
};

}  // namespace df::clock
