// Unit tests for contracts/include/df/msg/reconciliation.hpp.

// docs/decisions.md, "Layout and trait checks live in the contracts tests, not
// the headers". The offsets are the wire layout: a wrong value breaks every
// reader and every logged reconciliation result.

#include <array>
#include <cstddef>
#include <cstdint>
#include <df/msg/common.hpp>
#include <df/msg/reconciliation.hpp>
#include <type_traits>

namespace
{

using df::msg::kVenueOrderClaimMax;
using df::msg::ReconMismatch;
using df::msg::ReconOutcome;
using df::msg::ReconRequestMsg;
using df::msg::ReconResultMsg;
using df::msg::ReconStatus;
using df::msg::ReconTrigger;
using df::msg::ReconVerdictMsg;

static_assert(sizeof(ReconRequestMsg) == 56, "ReconRequestMsg is 56 bytes, see docs/contracts.md \"Reconciliation\"");
static_assert(0 == offsetof(ReconRequestMsg, hdr), "ReconRequestMsg.hdr is at offset 0");
static_assert(32 == offsetof(ReconRequestMsg, recon_id), "ReconRequestMsg.recon_id is at offset 32");
static_assert(40 == offsetof(ReconRequestMsg, venue_id), "ReconRequestMsg.venue_id is at offset 40");
static_assert(44 == offsetof(ReconRequestMsg, trigger), "ReconRequestMsg.trigger is at offset 44");
static_assert(48 == offsetof(ReconRequestMsg, requested_ts_ns), "ReconRequestMsg.requested_ts_ns is at offset 48");
static_assert(std::is_trivially_copyable_v<ReconRequestMsg>,
              "ReconRequestMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<ReconRequestMsg>,
              "ReconRequestMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<ReconRequestMsg>,
              "keep ReconRequestMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(ReconResultMsg) == 600, "ReconResultMsg is 600 bytes, see docs/contracts.md \"Reconciliation\"");
static_assert(0 == offsetof(ReconResultMsg, hdr), "ReconResultMsg.hdr is at offset 0");
static_assert(32 == offsetof(ReconResultMsg, recon_id), "ReconResultMsg.recon_id is at offset 32");
static_assert(40 == offsetof(ReconResultMsg, venue_id), "ReconResultMsg.venue_id is at offset 40");
static_assert(44 == offsetof(ReconResultMsg, instrument_id), "ReconResultMsg.instrument_id is at offset 44");
static_assert(48 == offsetof(ReconResultMsg, venue_net_qty), "ReconResultMsg.venue_net_qty is at offset 48");
static_assert(56 == offsetof(ReconResultMsg, venue_avg_px), "ReconResultMsg.venue_avg_px is at offset 56");
static_assert(64 == offsetof(ReconResultMsg, venue_open_orders), "ReconResultMsg.venue_open_orders is at offset 64");
static_assert(68 == offsetof(ReconResultMsg, venue_fill_count), "ReconResultMsg.venue_fill_count is at offset 68");
static_assert(72 == offsetof(ReconResultMsg, status), "ReconResultMsg.status is at offset 72");
static_assert(76 == offsetof(ReconResultMsg, venue_order_count), "ReconResultMsg.venue_order_count is at offset 76");
static_assert(80 == offsetof(ReconResultMsg, venue_order_ids), "ReconResultMsg.venue_order_ids is at offset 80");
static_assert(592 == offsetof(ReconResultMsg, venue_ts_ns), "ReconResultMsg.venue_ts_ns is at offset 592");
static_assert(std::is_trivially_copyable_v<ReconResultMsg>,
              "ReconResultMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<ReconResultMsg>,
              "ReconResultMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<ReconResultMsg>,
              "keep ReconResultMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(ReconVerdictMsg) == 72, "ReconVerdictMsg is 72 bytes, see docs/contracts.md \"Reconciliation\"");
static_assert(0 == offsetof(ReconVerdictMsg, hdr), "ReconVerdictMsg.hdr is at offset 0");
static_assert(32 == offsetof(ReconVerdictMsg, recon_id), "ReconVerdictMsg.recon_id is at offset 32");
static_assert(40 == offsetof(ReconVerdictMsg, venue_id), "ReconVerdictMsg.venue_id is at offset 40");
static_assert(44 == offsetof(ReconVerdictMsg, instrument_id), "ReconVerdictMsg.instrument_id is at offset 44");
static_assert(48 == offsetof(ReconVerdictMsg, qty_delta), "ReconVerdictMsg.qty_delta is at offset 48");
static_assert(56 == offsetof(ReconVerdictMsg, order_delta), "ReconVerdictMsg.order_delta is at offset 56");
static_assert(60 == offsetof(ReconVerdictMsg, outcome), "ReconVerdictMsg.outcome is at offset 60");
static_assert(61 == offsetof(ReconVerdictMsg, mismatch_class), "ReconVerdictMsg.mismatch_class is at offset 61");
static_assert(64 == offsetof(ReconVerdictMsg, finalize_ts_ns), "ReconVerdictMsg.finalize_ts_ns is at offset 64");
static_assert(std::is_trivially_copyable_v<ReconVerdictMsg>,
              "ReconVerdictMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<ReconVerdictMsg>,
              "ReconVerdictMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<ReconVerdictMsg>,
              "keep ReconVerdictMsg an aggregate: T{} must zero padding (docs/contracts.md)");

// The claim block is a std::array, so the capacity is part of the field type.
static_assert(kVenueOrderClaimMax == 64,
              "kVenueOrderClaimMax is 64: the fixed claim capacity (docs/contracts.md \"Reconciliation\")");
static_assert(std::is_same_v<decltype(ReconResultMsg::venue_order_ids), std::array<std::uint64_t, kVenueOrderClaimMax>>,
              "ReconResultMsg.venue_order_ids is a std::array of our client order ids, not a C array");

// Each enum must fit the field that carries it. The header stores the value in
// a fixed-width integer, so a widened enum would silently truncate on the wire.
static_assert(sizeof(ReconTrigger) == 1,
              "ReconTrigger must fit ReconRequestMsg.trigger (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(ReconStatus) == 1,
              "ReconStatus must fit ReconResultMsg.status (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(ReconOutcome) == 1,
              "ReconOutcome must fit ReconVerdictMsg.outcome (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(ReconMismatch) == 1,
              "ReconMismatch must fit ReconVerdictMsg.mismatch_class (docs/contracts.md \"Shared vocabulary\")");

}  // namespace
