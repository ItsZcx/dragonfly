#pragma once

#include "df/msg/header.hpp"

namespace df::msg
{
// Enum values for some variables in the structs are at common.hpp, check the docs as well

struct NewOrderMsg
{
    df::msg::MsgHeader hdr;
    std::uint64_t      client_order_id;  // id, unique system-wide
    std::uint64_t      signal_id;        // backlink to the originating signal
    std::uint32_t      strategy_id;
    std::uint32_t      instrument_id;  // canonical id
    std::uint32_t      venue_id;       // 0 = any venue, else a specific venue
    std::uint8_t       side;
    std::uint8_t       order_type;
    std::uint8_t       tif;

    // one byte of padding

    std::int64_t  qty;
    std::int64_t  price;  // 0 = market
    std::uint64_t sent_ts_ns;
};

struct CancelOrderMsg
{
    MsgHeader     hdr;
    std::uint64_t client_order_id;
    std::uint32_t instrument_id;
    std::uint32_t venue_id;  // 0 = any
    std::uint64_t sent_ts_ns;
};

struct ReplaceOrderMsg
{
    MsgHeader     hdr;
    std::uint64_t client_order_id;  // unchanged id of the order being amended
    std::uint32_t instrument_id;
    std::uint32_t venue_id;   // 0 = any
    std::int64_t  new_qty;    // absolute new total quantity, not a delta
    std::int64_t  new_price;  // absolute new limit price
    std::uint64_t sent_ts_ns;
};

}  // namespace df::msg
