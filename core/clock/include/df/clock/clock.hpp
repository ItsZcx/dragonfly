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

class LiveClock final : public IClock
{
public:
    [[nodiscard]] std::uint64_t NsNow() const override;
};

class SimulationClock final : public IClock
{
public:
    [[nodiscard]] std::uint64_t NsNow() const override;

    void ClockAdvanceTo(std::uint64_t ts_ns);

private:
    std::uint64_t now_ns_{};
};

}  // namespace df::clock
