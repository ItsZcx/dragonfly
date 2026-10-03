#pragma once

#include <cstdint>
#include <optional>

namespace df
{

// The ceiling on a strategy_id. It occupies the top 16 bits of a signal_id, so
// a value above this would truncate and collide with another strategy's ids.
// See docs/operations.md, "Strategies: config/strategies.yaml", and
// docs/contracts.md, AlphaSignalMsg: "Its top 16 bits are the strategy_id".
inline constexpr std::uint32_t kMaxStrategyId = 65'535;

struct ProducerId
{
    std::uint32_t value{};
};

struct InstrumentId
{
    std::uint32_t value{};
};

struct SignalId
{
    std::uint64_t value{};
};

struct StrategyId
{
    std::uint32_t value{};
};

// The only sanctioned way to build a StrategyId from a raw integer.
//
// Returns nullopt when the value would not fit, rather than asserting. The
// check is enforceable in every build, including the Release preset, so the
// rule is testable where an assert would have compiled out. The caller should
// not discard the null: docs/operations.md, "P8, strategy registration" makes
// naming the offending strategy file the caller's job, which is why the type
// reports the failure instead of aborting.
constexpr std::optional<StrategyId> MakeStrategyId(std::uint32_t value)
{
    if (value > kMaxStrategyId)
    {
        return std::nullopt;
    }
    return StrategyId{value};
}

struct ClientOrderId
{
    std::uint64_t value{};
};

// Copied from docs/contracts.md, "Venue identity". This enum is not the
// registry of venues: a venue's id is declared and validated in its config
// file, so that adding a venue changes no code here. Only Any is load-bearing,
// because the routing check needs one value every layer agrees on. The named
// constants are conveniences for the adapters that exist, and L2, L3, and L4
// must never switch on them. A venue whose adapter does not exist yet takes the
// next free number and no C++ changes.
enum class VenueId : uint32_t
{
    // no preference: whichever gateway receives the order claims it
    Any = 0,
    // 1 to 99 are reserved. A stale config or a truncated read must not resolve
    // to a live venue.

    // convenience alias. The authority is config/gateways/binance.yaml
    Binance = 100,
};

struct ExchangeOrderId
{
    std::uint64_t value{};
};
struct ReconId
{
    std::uint64_t value{};
};

struct AdjustmentId
{
    std::uint64_t value{};
};

}  // namespace df
