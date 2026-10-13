// Unit tests for contracts/include/df/msg/orders.hpp.

// docs/decisions.md, "Layout and trait checks live in the contracts tests, not
// the headers". The offsets are the wire layout: a wrong value breaks every
// reader and every logged order.

#include <cstddef>
#include <df/msg/common.hpp>
#include <df/msg/orders.hpp>
#include <type_traits>

namespace
{

using df::msg::CancelOrderMsg;
using df::msg::NewOrderMsg;
using df::msg::OrderType;
using df::msg::ReplaceOrderMsg;
using df::msg::Side;
using df::msg::TimeInForce;

static_assert(sizeof(NewOrderMsg) == 88, "NewOrderMsg is 88 bytes, see docs/contracts.md \"Orders\"");
static_assert(0 == offsetof(NewOrderMsg, hdr), "NewOrderMsg.hdr is at offset 0");
static_assert(32 == offsetof(NewOrderMsg, client_order_id), "NewOrderMsg.client_order_id is at offset 32");
static_assert(40 == offsetof(NewOrderMsg, signal_id), "NewOrderMsg.signal_id is at offset 40");
static_assert(48 == offsetof(NewOrderMsg, strategy_id), "NewOrderMsg.strategy_id is at offset 48");
static_assert(52 == offsetof(NewOrderMsg, instrument_id), "NewOrderMsg.instrument_id is at offset 52");
static_assert(56 == offsetof(NewOrderMsg, venue_id), "NewOrderMsg.venue_id is at offset 56");
static_assert(60 == offsetof(NewOrderMsg, side), "NewOrderMsg.side is at offset 60");
static_assert(61 == offsetof(NewOrderMsg, order_type), "NewOrderMsg.order_type is at offset 61");
static_assert(62 == offsetof(NewOrderMsg, tif), "NewOrderMsg.tif is at offset 62");
static_assert(64 == offsetof(NewOrderMsg, qty), "NewOrderMsg.qty is at offset 64");
static_assert(72 == offsetof(NewOrderMsg, price), "NewOrderMsg.price is at offset 72");
static_assert(80 == offsetof(NewOrderMsg, sent_ts_ns), "NewOrderMsg.sent_ts_ns is at offset 80");
static_assert(std::is_trivially_copyable_v<NewOrderMsg>,
              "NewOrderMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<NewOrderMsg>,
              "NewOrderMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<NewOrderMsg>,
              "keep NewOrderMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(CancelOrderMsg) == 56, "CancelOrderMsg is 56 bytes, see docs/contracts.md \"Orders\"");
static_assert(0 == offsetof(CancelOrderMsg, hdr), "CancelOrderMsg.hdr is at offset 0");
static_assert(32 == offsetof(CancelOrderMsg, client_order_id), "CancelOrderMsg.client_order_id is at offset 32");
static_assert(40 == offsetof(CancelOrderMsg, instrument_id), "CancelOrderMsg.instrument_id is at offset 40");
static_assert(44 == offsetof(CancelOrderMsg, venue_id), "CancelOrderMsg.venue_id is at offset 44");
static_assert(48 == offsetof(CancelOrderMsg, sent_ts_ns), "CancelOrderMsg.sent_ts_ns is at offset 48");
static_assert(std::is_trivially_copyable_v<CancelOrderMsg>,
              "CancelOrderMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<CancelOrderMsg>,
              "CancelOrderMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<CancelOrderMsg>,
              "keep CancelOrderMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(ReplaceOrderMsg) == 72, "ReplaceOrderMsg is 72 bytes, see docs/contracts.md \"Orders\"");
static_assert(0 == offsetof(ReplaceOrderMsg, hdr), "ReplaceOrderMsg.hdr is at offset 0");
static_assert(32 == offsetof(ReplaceOrderMsg, client_order_id), "ReplaceOrderMsg.client_order_id is at offset 32");
static_assert(40 == offsetof(ReplaceOrderMsg, instrument_id), "ReplaceOrderMsg.instrument_id is at offset 40");
static_assert(44 == offsetof(ReplaceOrderMsg, venue_id), "ReplaceOrderMsg.venue_id is at offset 44");
static_assert(48 == offsetof(ReplaceOrderMsg, new_qty), "ReplaceOrderMsg.new_qty is at offset 48");
static_assert(56 == offsetof(ReplaceOrderMsg, new_price), "ReplaceOrderMsg.new_price is at offset 56");
static_assert(64 == offsetof(ReplaceOrderMsg, sent_ts_ns), "ReplaceOrderMsg.sent_ts_ns is at offset 64");
static_assert(std::is_trivially_copyable_v<ReplaceOrderMsg>,
              "ReplaceOrderMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<ReplaceOrderMsg>,
              "ReplaceOrderMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<ReplaceOrderMsg>,
              "keep ReplaceOrderMsg an aggregate: T{} must zero padding (docs/contracts.md)");

// Each enum must fit the field that carries it. The header stores the value in
// a fixed-width integer, so a widened enum would silently truncate on the wire.
static_assert(sizeof(Side) == 1, "Side must fit NewOrderMsg.side (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(OrderType) == 1,
              "OrderType must fit NewOrderMsg.order_type (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(TimeInForce) == 1,
              "TimeInForce must fit NewOrderMsg.tif (docs/contracts.md \"Shared vocabulary\")");

}  // namespace
