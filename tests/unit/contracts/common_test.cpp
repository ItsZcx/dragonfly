// Unit tests for contracts/include/df/msg/common.hpp.

#include <cstdint>
#include <df/msg/common.hpp>
#include <type_traits>

namespace
{

using df::msg::KillCommand;
using df::msg::OrderStatus;
using df::msg::OrderType;
using df::msg::ProcessState;
using df::msg::ReconMismatch;
using df::msg::ReconOutcome;
using df::msg::ReconStatus;
using df::msg::ReconTrigger;
using df::msg::RiskRejectReason;
using df::msg::Side;
using df::msg::TimeInForce;

static_assert(std::is_same_v<std::underlying_type_t<Side>, std::uint8_t>,
              "Side's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<OrderType>, std::uint8_t>,
              "OrderType's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<OrderStatus>, std::uint8_t>,
              "OrderStatus's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<TimeInForce>, std::uint8_t>,
              "TimeInForce's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<ProcessState>, std::uint8_t>,
              "ProcessState's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<KillCommand>, std::uint8_t>,
              "KillCommand's underlying type should be uint8_t, see docs/contracts.md \"Shared vocabulary\"");
static_assert(std::is_same_v<std::underlying_type_t<ReconOutcome>, std::uint8_t>,
              "ReconOutcome's underlying type should be uint8_t, see docs/contracts.md \"Reconciliation\"");
static_assert(std::is_same_v<std::underlying_type_t<ReconMismatch>, std::uint8_t>,
              "ReconMismatch's underlying type should be uint8_t, see docs/contracts.md \"Reconciliation\"");
static_assert(std::is_same_v<std::underlying_type_t<ReconStatus>, std::uint8_t>,
              "ReconStatus's underlying type should be uint8_t, see docs/contracts.md \"Reconciliation\"");
static_assert(std::is_same_v<std::underlying_type_t<ReconTrigger>, std::uint8_t>,
              "ReconTrigger's underlying type should be uint8_t, see docs/contracts.md \"Reconciliation\"");
static_assert(
    std::is_same_v<std::underlying_type_t<RiskRejectReason>, std::uint16_t>,
    "RiskRejectReason's underlying type should be uint16_t, see docs/contracts.md \"Pre-trade rejection reasons\"");

// The sentinels that fail closed. A zeroed message must read as Unknown or
// None, so it cannot pass as a value that is used as it stands: the trading
// gate stays shut, and a value-initialized order carries no lifetime.
static_assert(static_cast<std::underlying_type_t<ReconOutcome>>(ReconOutcome::Unknown) == 0,
              "ReconOutcome::Unknown should be 0, see docs/contracts.md \"Reconciliation\"");
static_assert(static_cast<std::underlying_type_t<ReconStatus>>(ReconStatus::Unknown) == 0,
              "ReconStatus::Unknown should be 0, see docs/contracts.md \"Reconciliation\"");
static_assert(static_cast<std::underlying_type_t<ReconMismatch>>(ReconMismatch::None) == 0,
              "ReconMismatch::None should be 0, see docs/contracts.md \"Reconciliation\"");
static_assert(static_cast<std::underlying_type_t<RiskRejectReason>>(RiskRejectReason::None) == 0,
              "RiskRejectReason::None should be 0, see docs/contracts.md \"Pre-trade rejection reasons\"");
static_assert(static_cast<std::underlying_type_t<TimeInForce>>(TimeInForce::Unknown) == 0,
              "TimeInForce::Unknown should be 0, see docs/contracts.md \"Shared vocabulary\"");

}  // namespace
