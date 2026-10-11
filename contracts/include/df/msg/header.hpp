#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace df::msg
{

// More info in docs/contracts.md
enum class MsgType : std::uint32_t
{
    None = 0,

    // md channel
    Trade        = 11,
    Quote        = 12,
    BookDelta    = 13,
    BookSnapshot = 14,

    // signals channel
    AlphaSignal = 21,

    // risk_events channel
    RiskReject = 31,

    // orders channel
    NewOrder     = 41,
    CancelOrder  = 42,
    ReplaceOrder = 43,

    // fills channel
    ExecutionReport = 51,

    // control channel
    Heartbeat           = 61,
    KillSwitch          = 62,
    BookSnapshotRequest = 63,

    // recon channel
    ReconRequest = 71,
    ReconResult  = 72,
    ReconVerdict = 73,

    // ledger channel
    OrderAbandoned  = 81,
    PositionAdopted = 82,
    BasisAdopted    = 83,
};

inline constexpr std::uint8_t kMsgFlagModelledFill = 1 << 0;

// No constructor, deliberately, for any POD, refere to issue's 3 comments.
struct MsgHeader
{
    std::uint32_t msg_type;
    // Related to the process knowing what type of message it is first before decoding verything and wasting time in a possible message not directed at them.
    std::uint32_t msg_len;      // size of msg in bytes
    std::uint32_t producer_id;  // process who emitted it
    std::uint8_t  flags;
    // prevenance bits. bitmask of facts. eg: bit 0 -> ModelledFill, meaning the fee on this execution is not real and doesn't come from ours -> necessary for backtesting realistically with modelled fees.

    // 3 bites offest so ts_ns starts at 16

    std::uint64_t ts_ns;    // producer's MONOTONIC timestamp (our clock)
    std::uint64_t seq_num;  // per producer msg counter so we can check if we missed msgs and resync
};

static_assert(sizeof(MsgHeader) == 32, "MsgHeader is 32 bytes (docs/contracts.md)");
static_assert(0 == offsetof(MsgHeader, msg_type));
static_assert(4 == offsetof(MsgHeader, msg_len));
static_assert(8 == offsetof(MsgHeader, producer_id));
static_assert(12 == offsetof(MsgHeader, flags));
static_assert(16 == offsetof(MsgHeader, ts_ns));
static_assert(24 == offsetof(MsgHeader, seq_num));
static_assert(std::is_trivially_copyable_v<MsgHeader>);
static_assert(std::is_standard_layout_v<MsgHeader>);

}  // namespace df::msg
