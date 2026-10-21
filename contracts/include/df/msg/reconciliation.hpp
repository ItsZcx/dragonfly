#pragma once

#include "df/msg/header.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace df::msg
{

inline constexpr std::size_t kVenueOrderClaimMax = 64;

struct ReconRequestMsg
{
    MsgHeader     hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint8_t  trigger;

    // 3 bytes of padding
};

struct ReconResultMsg
{
    MsgHeader     hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint32_t instrument_id;
    std::int64_t  venue_net_qty;
    std::int64_t  venue_avg_entry;
    std::uint32_t venue_open_orders;
    std::uint32_t venue_fill_count;
    std::uint8_t  status;

    // 3 bytes of padding

    std::uint32_t                                  venue_order_ids_present;
    std::array<std::uint64_t, kVenueOrderClaimMax> venue_order_ids;
    std::uint64_t                                  venue_ts_ns;
};

struct ReconVerdictMsg
{
    MsgHeader     hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint32_t instrument_id;
    std::int64_t  qty_delta;
    std::int32_t  order_delta;
    std::uint8_t  outcome;
    std::uint8_t  mismatch_class;

    // 2 bytes of padding
};

}  // namespace df::msg
