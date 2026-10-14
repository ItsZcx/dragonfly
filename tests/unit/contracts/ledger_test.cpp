// Unit tests for contracts/include/df/msg/ledger.hpp.

// docs/decisions.md, "Layout and trait checks live in the contracts tests, not
// the headers". The offsets are the wire layout: a wrong value breaks every
// reader and every recorded ledger adjustment.

#include <cstddef>
#include <df/msg/ledger.hpp>
#include <type_traits>

namespace
{

using df::msg::BasisAdoptedMsg;
using df::msg::OrderAbandonedMsg;
using df::msg::PositionAdoptedMsg;

static_assert(sizeof(OrderAbandonedMsg) == 64,
              "OrderAbandonedMsg is 64 bytes, see docs/contracts.md \"Ledger adjustments\"");
static_assert(0 == offsetof(OrderAbandonedMsg, hdr), "OrderAbandonedMsg.hdr is at offset 0");
static_assert(32 == offsetof(OrderAbandonedMsg, adjustment_id), "OrderAbandonedMsg.adjustment_id is at offset 32");
static_assert(40 == offsetof(OrderAbandonedMsg, client_order_id), "OrderAbandonedMsg.client_order_id is at offset 40");
static_assert(48 == offsetof(OrderAbandonedMsg, instrument_id), "OrderAbandonedMsg.instrument_id is at offset 48");
static_assert(52 == offsetof(OrderAbandonedMsg, venue_id), "OrderAbandonedMsg.venue_id is at offset 52");
static_assert(56 == offsetof(OrderAbandonedMsg, applied_ts_ns), "OrderAbandonedMsg.applied_ts_ns is at offset 56");
static_assert(std::is_trivially_copyable_v<OrderAbandonedMsg>,
              "OrderAbandonedMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<OrderAbandonedMsg>,
              "OrderAbandonedMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<OrderAbandonedMsg>,
              "keep OrderAbandonedMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(PositionAdoptedMsg) == 72,
              "PositionAdoptedMsg is 72 bytes, see docs/contracts.md \"Ledger adjustments\"");
static_assert(0 == offsetof(PositionAdoptedMsg, hdr), "PositionAdoptedMsg.hdr is at offset 0");
static_assert(32 == offsetof(PositionAdoptedMsg, adjustment_id), "PositionAdoptedMsg.adjustment_id is at offset 32");
static_assert(40 == offsetof(PositionAdoptedMsg, recon_id), "PositionAdoptedMsg.recon_id is at offset 40");
static_assert(48 == offsetof(PositionAdoptedMsg, instrument_id), "PositionAdoptedMsg.instrument_id is at offset 48");
static_assert(52 == offsetof(PositionAdoptedMsg, venue_id), "PositionAdoptedMsg.venue_id is at offset 52");
static_assert(56 == offsetof(PositionAdoptedMsg, new_net_qty), "PositionAdoptedMsg.new_net_qty is at offset 56");
static_assert(64 == offsetof(PositionAdoptedMsg, applied_ts_ns), "PositionAdoptedMsg.applied_ts_ns is at offset 64");
static_assert(std::is_trivially_copyable_v<PositionAdoptedMsg>,
              "PositionAdoptedMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<PositionAdoptedMsg>,
              "PositionAdoptedMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<PositionAdoptedMsg>,
              "keep PositionAdoptedMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(BasisAdoptedMsg) == 72,
              "BasisAdoptedMsg is 72 bytes, see docs/contracts.md \"Ledger adjustments\"");
static_assert(0 == offsetof(BasisAdoptedMsg, hdr), "BasisAdoptedMsg.hdr is at offset 0");
static_assert(32 == offsetof(BasisAdoptedMsg, adjustment_id), "BasisAdoptedMsg.adjustment_id is at offset 32");
static_assert(40 == offsetof(BasisAdoptedMsg, recon_id), "BasisAdoptedMsg.recon_id is at offset 40");
static_assert(48 == offsetof(BasisAdoptedMsg, instrument_id), "BasisAdoptedMsg.instrument_id is at offset 48");
static_assert(52 == offsetof(BasisAdoptedMsg, venue_id), "BasisAdoptedMsg.venue_id is at offset 52");
static_assert(56 == offsetof(BasisAdoptedMsg, new_avg_entry), "BasisAdoptedMsg.new_avg_entry is at offset 56");
static_assert(64 == offsetof(BasisAdoptedMsg, applied_ts_ns), "BasisAdoptedMsg.applied_ts_ns is at offset 64");
static_assert(std::is_trivially_copyable_v<BasisAdoptedMsg>,
              "BasisAdoptedMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<BasisAdoptedMsg>,
              "BasisAdoptedMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<BasisAdoptedMsg>,
              "keep BasisAdoptedMsg an aggregate: T{} must zero padding (docs/contracts.md)");

}  // namespace
