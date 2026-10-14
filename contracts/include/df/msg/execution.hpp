#pragma once

#include "df/msg/header.hpp"

namespace df::msg
{

struct ExecutionReportMsg
{
    MsgHeader     hdr;
    std::uint64_t client_order_id;    // our order id
    std::uint64_t exchange_order_id;  // the venue's order id
    std::uint32_t instrument_id;
    std::uint32_t venue_id;        // which venue this came from
    std::uint8_t  side;            // Side
    std::uint8_t  status;          // OrderStatus
    std::uint8_t  liquidity_flag;  // 0=maker, 1=taker, 2=unknown

    // 5 byte padding

    std::int64_t  filled_qty;      // this report's increment
    std::int64_t  cumulative_qty;  // total filled so far
    std::int64_t  fill_price;      // if no fill
    std::int64_t  fee;             // signed, negative = a rebate to us
    std::uint64_t exchange_ts_ns;
};

}  // namespace df::msg
