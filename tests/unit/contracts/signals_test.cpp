// Unit tests for contracts/include/df/msg/signals.hpp.

// docs/decisions.md, "Layout and trait checks live in the contracts tests, not
// the headers". The offsets are the wire layout: a wrong value breaks every
// reader and every logged signal.

#include <cstddef>
#include <df/msg/common.hpp>
#include <df/msg/signals.hpp>
#include <type_traits>

namespace
{

using df::msg::AlphaSignalMsg;
using df::msg::OrderType;
using df::msg::RiskRejectMsg;
using df::msg::RiskRejectReason;
using df::msg::Side;
using df::msg::TimeInForce;

static_assert(sizeof(AlphaSignalMsg) == 80, "AlphaSignalMsg is 80 bytes, see docs/contracts.md \"Signals\"");
static_assert(0 == offsetof(AlphaSignalMsg, hdr), "AlphaSignalMsg.hdr is at offset 0");
static_assert(32 == offsetof(AlphaSignalMsg, signal_id), "AlphaSignalMsg.signal_id is at offset 32");
static_assert(40 == offsetof(AlphaSignalMsg, strategy_id), "AlphaSignalMsg.strategy_id is at offset 40");
static_assert(44 == offsetof(AlphaSignalMsg, instrument_id), "AlphaSignalMsg.instrument_id is at offset 44");
static_assert(48 == offsetof(AlphaSignalMsg, side), "AlphaSignalMsg.side is at offset 48");
static_assert(49 == offsetof(AlphaSignalMsg, order_type), "AlphaSignalMsg.order_type is at offset 49");
static_assert(50 == offsetof(AlphaSignalMsg, tif), "AlphaSignalMsg.tif is at offset 50");
static_assert(56 == offsetof(AlphaSignalMsg, target_qty), "AlphaSignalMsg.target_qty is at offset 56");
static_assert(64 == offsetof(AlphaSignalMsg, limit_price), "AlphaSignalMsg.limit_price is at offset 64");
static_assert(72 == offsetof(AlphaSignalMsg, created_ts_ns), "AlphaSignalMsg.created_ts_ns is at offset 72");
static_assert(std::is_trivially_copyable_v<AlphaSignalMsg>,
              "AlphaSignalMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<AlphaSignalMsg>,
              "AlphaSignalMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<AlphaSignalMsg>,
              "keep AlphaSignalMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(RiskRejectMsg) == 56, "RiskRejectMsg is 56 bytes, see docs/contracts.md \"Signals\"");
static_assert(0 == offsetof(RiskRejectMsg, hdr), "RiskRejectMsg.hdr is at offset 0");
static_assert(32 == offsetof(RiskRejectMsg, signal_id), "RiskRejectMsg.signal_id is at offset 32");
static_assert(40 == offsetof(RiskRejectMsg, instrument_id), "RiskRejectMsg.instrument_id is at offset 40");
static_assert(44 == offsetof(RiskRejectMsg, reason), "RiskRejectMsg.reason is at offset 44");
static_assert(48 == offsetof(RiskRejectMsg, rejected_ts_ns), "RiskRejectMsg.rejected_ts_ns is at offset 48");
static_assert(std::is_trivially_copyable_v<RiskRejectMsg>,
              "RiskRejectMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<RiskRejectMsg>,
              "RiskRejectMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<RiskRejectMsg>,
              "keep RiskRejectMsg an aggregate: T{} must zero padding (docs/contracts.md)");

// Each enum must fit the field that carries it. The header stores the value in
// a fixed-width integer, so a widened enum would silently truncate on the wire.
static_assert(sizeof(Side) == 1, "Side must fit AlphaSignalMsg.side (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(OrderType) == 1,
              "OrderType must fit AlphaSignalMsg.order_type (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(TimeInForce) == 1,
              "TimeInForce must fit AlphaSignalMsg.tif (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(RiskRejectReason) == 2,
              "RiskRejectReason must fit RiskRejectMsg.reason (docs/contracts.md \"Pre-trade rejection reasons\")");

}  // namespace
