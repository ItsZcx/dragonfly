#pragma once

#include "df/msg/header.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace df::msg
{

// The per-side depth of a BookSnapshotMsg
inline constexpr std::size_t kBookDepth = 64;

struct Level
{
    std::int64_t price;
    std::int64_t qty;
};

struct TradeMsg
{
    MsgHeader     hdr;
    std::uint32_t instrument_id;
    std::uint8_t  aggressor_side;  // 0 = buyer, 1 = sell

    // 3 bytes of padding

    std::int64_t  price;
    std::int64_t  qty;
    std::uint64_t exchange_ts_ns;
};

struct QuoteMsg
{
    MsgHeader     hdr;
    std::uint32_t instrument_id;
    std::int64_t  bid_price;
    std::int64_t  bid_qty;
    std::int64_t  ask_price;
    std::int64_t  ask_qty;
    std::uint64_t exchange_ts_ns;
};

struct BookDeltaMsg
{
    MsgHeader     hdr;
    std::uint32_t instrument_id;
    std::uint8_t  side;    // 0 = bid side, 1 = ask side
    std::uint8_t  action;  // 0 = add, 1 = modify, 2 = delete
    std::int64_t  price_level;
    std::int64_t  qty;
    std::uint64_t exchange_ts_ns;
};

struct BookSnapshotMsg
{
    MsgHeader                     hdr;
    std::uint32_t                 instrument_id;
    std::uint16_t                 bid_levels;  // how many of the kBookDepth value are populated
    std::uint16_t                 ask_levels;
    std::uint64_t                 exchange_ts_ns;
    std::array<Level, kBookDepth> bids;  // best first
    std::array<Level, kBookDepth> asks;  // best first
};

struct BookSnapshotRequestMsg
{
    MsgHeader     hdr;
    std::uint32_t instrument_id;
    std::uint64_t after_seq_num;
};

}  // namespace df::msg
