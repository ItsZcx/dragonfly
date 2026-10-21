#pragma once

#include "df/msg/header.hpp"

#include <cstdint>

namespace df::msg
{

struct OrderAbandonedMsg
{
    MsgHeader     hdr;
    std::uint64_t adjustment_id;
    std::uint64_t client_order_id;
    std::uint32_t instrument_id;
    std::uint32_t venue_id;
};

struct PositionAdoptedMsg
{
    MsgHeader     hdr;
    std::uint64_t adjustment_id;
    std::uint64_t recon_id;  // recon pass that caused it
    std::uint32_t instrument_id;
    std::uint32_t venue_id;
    std::int64_t  new_net_qty;  // the adopted position
};

struct BasisAdoptedMsg
{
    MsgHeader     hdr;
    std::uint64_t adjustment_id;
    std::uint64_t recon_id;
    std::uint32_t instrument_id;
    std::uint32_t venue_id;
    std::int64_t  new_avg_entry;  // the adopted cost basis
};

}  // namespace df::msg
