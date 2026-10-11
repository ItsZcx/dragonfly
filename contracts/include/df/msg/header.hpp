#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
// `MsgHeader m{}` value-initializes the object and zeroes the padding. See docs/contracts.md, "Every message.
struct MsgHeader
{
    std::uint32_t msg_type;
    std::uint32_t msg_len;
    std::uint32_t producer_id;
    std::uint8_t  flags;
    std::uint64_t ts_ns;
    std::uint64_t seq_num;
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
static_assert(std::is_trivially_default_constructible_v<MsgHeader>,
              "keep MsgHeader an aggregate: T{} must zero padding (docs/contracts.md)");

}  // namespace df::msg
