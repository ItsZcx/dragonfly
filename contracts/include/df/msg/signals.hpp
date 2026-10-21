#pragma once

#include "df/msg/header.hpp"

namespace df::msg
{

struct AlphaSignalMsg
{
    MsgHeader     hdr;
    std::uint64_t signal_id;
    std::uint32_t strategy_id;
    std::uint32_t instrument_id;  // canonical id
    std::uint8_t  side;           // Side enum displays values
    std::uint8_t  order_type;     // OrderType enum displays values
    std::uint8_t  tif;            // TimeInForce enum displays values

    // 5 bytes of paddign

    std::int64_t qty;
    std::int64_t limit_price;  // 0 = market
};

struct RiskRejectMsg
{
    df::msg::MsgHeader hdr;
    std::uint64_t      signal_id;
    std::uint32_t      instrument_id;
    std::uint16_t      reason;  // RiskRejectReason enum displays values

    // 2 bytes of padding
};

}  // namespace df::msg
