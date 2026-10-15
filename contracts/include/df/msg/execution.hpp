#pragma once

#include "df/msg/header.hpp"

namespace df::msg
{

struct ExecutionReportMsg
{
    MsgHeader     hdr;
    std::uint64_t client_order_id;
    std::uint64_t exchange_order_id;
    std::uint32_t instrument_id;
    std::uint32_t venue_id;
    std::uint8_t  side;    // Side
    std::uint8_t  status;  // OrderStatus
    std::uint8_t  liquidity;

    // 5 byte padding

    std::int64_t  filled_qty;
    std::int64_t  cumulative_qty;
    std::int64_t  fill_price;
    std::int64_t  fee;  // signed, negative = a rebate to us
    std::uint64_t exchange_ts_ns;
};

}  // namespace df::msg
