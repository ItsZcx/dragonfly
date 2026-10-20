// Unit tests for contracts/include/df/msg/market_data.hpp.

#include <cstddef>
#include <df/msg/market_data.hpp>
#include <type_traits>

namespace
{

using df::msg::BookDeltaMsg;
using df::msg::BookSnapshotMsg;
using df::msg::BookSnapshotRequestMsg;
using df::msg::Level;
using df::msg::QuoteMsg;
using df::msg::TradeMsg;

static_assert(sizeof(Level) == 16, "Level is 16 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(Level, price), "Level.price is at offset 0");
static_assert(8 == offsetof(Level, qty), "Level.qty is at offset 8");
static_assert(std::is_trivially_copyable_v<Level>,
              "Level must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<Level>,
              "Level must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<Level>,
              "keep Level an aggregate: T{} must zero its bytes (docs/contracts.md)");

static_assert(sizeof(TradeMsg) == 64, "TradeMsg is 64 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(TradeMsg, hdr), "TradeMsg.hdr is at offset 0");
static_assert(32 == offsetof(TradeMsg, instrument_id), "TradeMsg.instrument_id is at offset 32");
static_assert(36 == offsetof(TradeMsg, aggressor_side), "TradeMsg.aggressor_side is at offset 36");
static_assert(40 == offsetof(TradeMsg, price), "TradeMsg.price is at offset 40");
static_assert(48 == offsetof(TradeMsg, qty), "TradeMsg.qty is at offset 48");
static_assert(56 == offsetof(TradeMsg, exchange_ts_ns), "TradeMsg.exchange_ts_ns is at offset 56");
static_assert(std::is_trivially_copyable_v<TradeMsg>,
              "TradeMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<TradeMsg>,
              "TradeMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<TradeMsg>,
              "keep TradeMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(QuoteMsg) == 80, "QuoteMsg is 80 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(QuoteMsg, hdr), "QuoteMsg.hdr is at offset 0");
static_assert(32 == offsetof(QuoteMsg, instrument_id), "QuoteMsg.instrument_id is at offset 32");
static_assert(40 == offsetof(QuoteMsg, bid_price), "QuoteMsg.bid_price is at offset 40");
static_assert(48 == offsetof(QuoteMsg, bid_qty), "QuoteMsg.bid_qty is at offset 48");
static_assert(56 == offsetof(QuoteMsg, ask_price), "QuoteMsg.ask_price is at offset 56");
static_assert(64 == offsetof(QuoteMsg, ask_qty), "QuoteMsg.ask_qty is at offset 64");
static_assert(72 == offsetof(QuoteMsg, exchange_ts_ns), "QuoteMsg.exchange_ts_ns is at offset 72");
static_assert(std::is_trivially_copyable_v<QuoteMsg>,
              "QuoteMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<QuoteMsg>,
              "QuoteMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<QuoteMsg>,
              "keep QuoteMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(BookDeltaMsg) == 64, "BookDeltaMsg is 64 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(BookDeltaMsg, hdr), "BookDeltaMsg.hdr is at offset 0");
static_assert(32 == offsetof(BookDeltaMsg, instrument_id), "BookDeltaMsg.instrument_id is at offset 32");
static_assert(36 == offsetof(BookDeltaMsg, side), "BookDeltaMsg.side is at offset 36");
static_assert(37 == offsetof(BookDeltaMsg, action), "BookDeltaMsg.action is at offset 37");
static_assert(40 == offsetof(BookDeltaMsg, price_level), "BookDeltaMsg.price_level is at offset 40");
static_assert(48 == offsetof(BookDeltaMsg, qty), "BookDeltaMsg.qty is at offset 48");
static_assert(56 == offsetof(BookDeltaMsg, exchange_ts_ns), "BookDeltaMsg.exchange_ts_ns is at offset 56");
static_assert(std::is_trivially_copyable_v<BookDeltaMsg>,
              "BookDeltaMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<BookDeltaMsg>,
              "BookDeltaMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<BookDeltaMsg>,
              "keep BookDeltaMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(BookSnapshotMsg) == 2096, "BookSnapshotMsg is 2096 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(BookSnapshotMsg, hdr), "BookSnapshotMsg.hdr is at offset 0");
static_assert(32 == offsetof(BookSnapshotMsg, instrument_id), "BookSnapshotMsg.instrument_id is at offset 32");
static_assert(36 == offsetof(BookSnapshotMsg, bid_levels), "BookSnapshotMsg.bid_levels is at offset 36");
static_assert(38 == offsetof(BookSnapshotMsg, ask_levels), "BookSnapshotMsg.ask_levels is at offset 38");
static_assert(40 == offsetof(BookSnapshotMsg, exchange_ts_ns), "BookSnapshotMsg.exchange_ts_ns is at offset 40");
static_assert(48 == offsetof(BookSnapshotMsg, bids), "BookSnapshotMsg.bids is at offset 48");
static_assert(1072 == offsetof(BookSnapshotMsg, asks), "BookSnapshotMsg.asks is at offset 1072");
static_assert(std::is_trivially_copyable_v<BookSnapshotMsg>,
              "BookSnapshotMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<BookSnapshotMsg>,
              "BookSnapshotMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<BookSnapshotMsg>,
              "keep BookSnapshotMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(BookSnapshotRequestMsg) == 48,
              "BookSnapshotRequestMsg is 48 bytes, see docs/contracts.md \"Market data\"");
static_assert(0 == offsetof(BookSnapshotRequestMsg, hdr), "BookSnapshotRequestMsg.hdr is at offset 0");
static_assert(32 == offsetof(BookSnapshotRequestMsg, instrument_id),
              "BookSnapshotRequestMsg.instrument_id is at offset 32");
static_assert(40 == offsetof(BookSnapshotRequestMsg, seq_floor), "BookSnapshotRequestMsg.seq_floor is at offset 40");
static_assert(std::is_trivially_copyable_v<BookSnapshotRequestMsg>,
              "BookSnapshotRequestMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<BookSnapshotRequestMsg>,
              "BookSnapshotRequestMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<BookSnapshotRequestMsg>,
              "keep BookSnapshotRequestMsg an aggregate: T{} must zero padding (docs/contracts.md)");

}  // namespace
