// Unit tests for contracts/include/df/msg/execution.hpp.

#include <cstddef>
#include <df/msg/common.hpp>
#include <df/msg/execution.hpp>
#include <type_traits>

namespace
{

using df::msg::ExecutionReportMsg;
using df::msg::OrderStatus;
using df::msg::Side;

static_assert(sizeof(ExecutionReportMsg) == 104,
              "ExecutionReportMsg is 104 bytes, see docs/contracts.md \"Execution\"");
static_assert(0 == offsetof(ExecutionReportMsg, hdr), "ExecutionReportMsg.hdr is at offset 0");
static_assert(32 == offsetof(ExecutionReportMsg, client_order_id),
              "ExecutionReportMsg.client_order_id is at offset 32");
static_assert(40 == offsetof(ExecutionReportMsg, exchange_order_id),
              "ExecutionReportMsg.exchange_order_id is at offset 40");
static_assert(48 == offsetof(ExecutionReportMsg, instrument_id), "ExecutionReportMsg.instrument_id is at offset 48");
static_assert(52 == offsetof(ExecutionReportMsg, venue_id), "ExecutionReportMsg.venue_id is at offset 52");
static_assert(56 == offsetof(ExecutionReportMsg, side), "ExecutionReportMsg.side is at offset 56");
static_assert(57 == offsetof(ExecutionReportMsg, status), "ExecutionReportMsg.status is at offset 57");
static_assert(58 == offsetof(ExecutionReportMsg, liquidity), "ExecutionReportMsg.liquidity is at offset 58");
static_assert(64 == offsetof(ExecutionReportMsg, filled_qty), "ExecutionReportMsg.filled_qty is at offset 64");
static_assert(72 == offsetof(ExecutionReportMsg, cumulative_qty), "ExecutionReportMsg.cumulative_qty is at offset 72");
static_assert(80 == offsetof(ExecutionReportMsg, fill_price), "ExecutionReportMsg.fill_price is at offset 80");
static_assert(88 == offsetof(ExecutionReportMsg, fee), "ExecutionReportMsg.fee is at offset 88");
static_assert(96 == offsetof(ExecutionReportMsg, exchange_ts_ns), "ExecutionReportMsg.exchange_ts_ns is at offset 96");
static_assert(std::is_trivially_copyable_v<ExecutionReportMsg>,
              "ExecutionReportMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<ExecutionReportMsg>,
              "ExecutionReportMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<ExecutionReportMsg>,
              "keep ExecutionReportMsg an aggregate: T{} must zero padding (docs/contracts.md)");

// Each enum must fit the field that carries it. The header stores the value in
// a fixed-width integer, so a widened enum would silently truncate on the wire.
static_assert(sizeof(Side) == 1, "Side must fit ExecutionReportMsg.side (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(OrderStatus) == 1,
              "OrderStatus must fit ExecutionReportMsg.status (docs/contracts.md \"Shared vocabulary\")");

}  // namespace
