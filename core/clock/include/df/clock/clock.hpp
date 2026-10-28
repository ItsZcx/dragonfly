#pragma once

#include <cstdint>

namespace df::clock
{

class IClock
{
public:
    [[nodiscard]] virtual std::uint64_t NsNow() const = 0;

protected:
    ~IClock() = default;  // cpp core guideline C.35
};

}  // namespace df::clock
