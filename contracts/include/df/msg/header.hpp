#pragma once

#include <cstdint>

namespace df::msg
{

// The message type codes. docs/contracts.md, "Field by field" is the reference.
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

// Every field is specified in docs/contracts.md, "The message header".
// `MsgHeader m{}` value-initializes the object and zeroes the padding. See
// docs/contracts.md, "Every message is value-initialized before use".
struct MsgHeader
{
    std::uint32_t msg_type;
    std::uint32_t msg_len;
    std::uint32_t producer_id;
    std::uint8_t  flags;
    std::uint64_t ts_ns;
    std::uint64_t seq_num;
};

}  // namespace df::msg
