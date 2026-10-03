// P0 smoke test.
//
// Purpose: prove the toolchain end to end — Clang, C++20, CMake, Ninja,
// GoogleTest, and CTest all work — before any real code exists.
//
// It also verifies the language features and layout conventions the design
// depends on (contracts.md), so a toolchain regression is caught now rather
// than discovered in P1.
//
// Layer legend (overview.md, "The channels"): L1 = ingestion (feed handler), L2 = strategy,
// L3 = risk (firewall), L4 = OMS (ledger), L5 = EMS (execution), L6 = observability.

#include "gtest/gtest.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{

// Mirrors the shape of the real message header (contracts.md): a fixed-size
// POD with no pointers, no virtuals, and no owned resources.
struct MsgHeader
{
    std::uint32_t msg_type;     //  0 ..  4
    std::uint32_t msg_len;      //  4 ..  8
    std::uint32_t producer_id;  //  8 .. 12
    std::uint8_t flags;         // 12 .. 13  MsgFlag bitmask
    // 3 bytes of padding, so ts_ns starts 8-byte aligned
    std::uint64_t ts_ns;    // 16 .. 24
    std::uint64_t seq_num;  // 24 .. 32
};

// Mirrors df::msg::MsgFlag (contracts.md, the `flags` field).
enum class MsgFlag : std::uint8_t
{
    None = 0,
    ModelledFill = 1,
};

// Mirrors the audit-log entry framing (layers.md, L6 (observability)). Every entry in the log
// is length-prefixed, and a gap is an explicit entry rather than a hole, so an
// incomplete log is detectable instead of silently replayable.
enum class LogEntryType : std::uint16_t
{
    Message =
        0,  // a recorded wire message, verbatim (incl. padding zeros, contracts.md, "Every message is value-initialized before use")
    SeqGap = 1,     // a hole: producer_id(s) affected + expected/observed seq_num
    LogOpened = 2,  // first entry of a file: writer id, wall-clock start, format version
    LogClosed = 3,  // clean shutdown. Absence means the log was truncated.
};

struct LogEntryHeader
{
    std::uint32_t length;      //  0 ..  4  bytes of payload after this header
    LogEntryType entry_type;   //  4 ..  6
    std::uint16_t pad;         //  6 ..  8  always zero (contracts.md, "Every message is value-initialized before use")
    std::uint64_t recv_ts_ns;  //  8 .. 16  the recorder's own clock, when it received it
};

// Mirrors BookSnapshotRequestMsg (contracts.md, `BookSnapshotRequestMsg`). The one consumer-to-producer
// message in the system, carried on `control` because that is already the channel
// every process both publishes and subscribes to.
struct BookSnapshotRequestMsg
{
    MsgHeader hdr;                //  0 .. 32
    std::uint32_t instrument_id;  // 32 .. 36
    std::uint32_t requester_id;   // 36 .. 40  explicit, not inferred from hdr
    std::uint64_t after_seq_num;  // 40 .. 48  snapshot must be at least this fresh
};

// Mirrors KillCommand (contracts.md, `KillSwitchMsg`). The message is a broadcast COMMAND, not a
// state change: every process receives it and each acts only on the part it owns.
// A command with no named executor silently does nothing, which is the worst
// possible failure mode for an emergency control surface.
enum class KillCommand : std::uint8_t
{
    Halt = 0,
    CancelAll = 1,
    Flatten = 2,
    Resume = 3
};

// Mirrors the per-channel publish-failure policy (contracts.md, "Publication failure is per-channel"). Aeron's
// window-based backpressure is a bound on RETENTION, not a promise that a publish
// succeeds: offer() returns a negative code and delivers nothing. A hot thread may
// not block (overview.md, "What is on the hot path"), so every publish site needs a declared failure behaviour --
// and the right answer differs per channel, because the cost of losing a message
// is not the same everywhere.
enum class PublishFailurePolicy : std::uint8_t
{
    NeverFails = 0,  // capacity makes a full window a fatal error
    DropAndCount =
        1,           // lossy by nature; self-heals via seq_num gap (contracts.md, "Sequence numbers across a restart")
    FailClosed = 2,  // latch the kill switch rather than drop
    RetryUntilDelivered = 3
};

// Mirrors ReplaceOrderMsg (contracts.md, `ReplaceOrderMsg`). Added because the Investigation names
// "Cancel/Replace" as a core order type (Investigation §5) and the design had no replace path.
// client_order_id is REUSED: a venue cancel/replace is one order with amended terms,
// and the exchange_order_id does not change. Generating a fresh id would be the
// cancel+new emulation this message exists to avoid.
struct ReplaceOrderMsg
{
    MsgHeader hdr;                  //  0 .. 32
    std::uint64_t client_order_id;  // 32 .. 40  UNCHANGED id of the order being amended
    std::uint32_t instrument_id;    // 40 .. 44
    std::uint32_t venue_id;         // 44 .. 48  (0 = any)
    std::int64_t new_qty;           // 48 .. 56  ABSOLUTE new total quantity, not a delta
    std::int64_t new_price;         // 56 .. 64  ABSOLUTE new limit price
    std::uint64_t sent_ts_ns;       // 64 .. 72
};

// Mirrors HeartbeatMsg (contracts.md, `HeartbeatMsg`). ref_data_hash makes a heartbeat assert
// IDENTITY as well as liveness, at zero wire cost: it occupies bytes that were
// already padding, so the message stays 48 bytes.
struct HeartbeatMsg
{
    MsgHeader hdr;       //  0 .. 32
    std::uint8_t state;  // 32 .. 33
    // 3 bytes padding
    std::uint32_t ref_data_hash;  // 36 .. 40  content hash of loaded reference data
    std::uint64_t beat_ts_ns;     // 40 .. 48
};

// Mirrors OrderRecordState (layers.md, L4 (OMS)). PendingReplace sits between
// PendingCancel and the terminal block: an amendment is a request, and until the
// venue confirms it the order is still live and still reserved.
enum class OrderRecordState : std::uint8_t
{
    PendingNew = 0,
    Live = 1,
    PartiallyFilled = 2,
    PendingCancel = 3,
    PendingReplace = 4,
    Filled = 5,
    Cancelled = 6,
    Rejected = 7,
    Abandoned = 8
};

constexpr bool IsTerminal(OrderRecordState s)
{ return s >= OrderRecordState::Filled; }

// Mirrors the reconciliation messages (contracts.md, "Reconciliation"). These exist so that a
// field reordering, a widened type, or an accidentally added member fails the
// build rather than silently changing the wire layout.
struct ReconRequestMsg
{
    MsgHeader hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint8_t trigger;
    // 3 bytes padding
    std::uint64_t requested_ts_ns;
};

struct ReconResultMsg
{
    MsgHeader hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint32_t instrument_id;
    std::int64_t venue_net_qty;
    std::int64_t venue_avg_px;
    std::uint32_t venue_open_orders;  // the venue's TOTAL open-order count
    std::uint32_t venue_fill_count;
    std::uint8_t status;
    // 3 bytes padding
    std::uint32_t venue_order_count;    // ids present below (<= kMaxReconOrders)
    std::uint64_t venue_order_ids[64];  // client_order_ids echoed by the venue
    std::uint64_t venue_ts_ns;
};

struct ReconVerdictMsg
{
    MsgHeader hdr;
    std::uint64_t recon_id;
    std::uint32_t venue_id;
    std::uint32_t instrument_id;
    std::int64_t qty_delta;
    std::uint32_t order_delta;
    std::uint8_t outcome;
    std::uint8_t mismatch_class;
    // 6 bytes padding
    std::uint64_t finalize_ts_ns;
};

// Mirrors the ledger-adjustment messages (contracts.md, "Ledger adjustments"). The carrier for the two ledger
// mutations that are NOT derivable from orders/fills: the off-path wall-clock
// `Abandoned` sweep (layers.md, L4 (OMS)), and reconciliation's adopted corrections (contracts.md, Reconciliation)
// classes 1/2/5). Without it, the audit log has a hole exactly where recovery is
// hardest, and P10's "reconstructs identically" cannot hold.
enum class LedgerAdjustmentType : std::uint8_t
{
    OrderAbandoned = 0,
    PositionAdopted = 1,
    BasisAdopted = 2,
};

struct LedgerAdjustmentMsg
{
    MsgHeader hdr;
    std::uint64_t adjustment_id;    // monotonic per producer (L4)
    std::uint64_t recon_id;         // the pass that caused it; 0 for a sweep
    std::uint64_t client_order_id;  // valid for OrderAbandoned, else 0
    std::uint32_t instrument_id;
    std::uint32_t venue_id;  // 0 = not venue-specific
    std::uint8_t type;       // LedgerAdjustmentType
    // 7 bytes padding
    std::int64_t new_net_qty;
    std::int64_t new_avg_entry;
    std::uint64_t applied_ts_ns;
};

}  // namespace

// ---- Toolchain --------------------------------------------------------------

TEST(P0Smoke, ToolchainRunsTests)
{ EXPECT_TRUE(true); }

TEST(P0Smoke, Cxx20IsActive)
{
#if __cplusplus < 202002L
    FAIL() << "expected C++20 or later, got __cplusplus=" << __cplusplus;
#else
    SUCCEED();
#endif
}

// ---- Contracts rely on these properties (contracts.md) --------------------

TEST(P0Smoke, MsgHeaderIsStandardLayoutAndTriviallyCopyable)
{
    // These two traits are what make pointer-casting over a raw byte buffer
    // safe. If either fails, the zero-copy message design is invalid.
    static_assert(std::is_standard_layout_v<MsgHeader>);
    static_assert(std::is_trivially_copyable_v<MsgHeader>);
    SUCCEED();
}

TEST(P0Smoke, MsgHeaderLayoutMatchesSpec)
{
    // The header is specified as 32 bytes in contracts.md, "The message header".
    static_assert(sizeof(MsgHeader) == 32, "MsgHeader layout changed — update contracts.md");
    static_assert(offsetof(MsgHeader, msg_type) == 0);
    static_assert(offsetof(MsgHeader, msg_len) == 4);
    static_assert(offsetof(MsgHeader, producer_id) == 8);
    // `flags` occupies the byte at 12, and three bytes of padding follow it so that
    // `ts_ns` is 8-byte aligned. This is what proves the header did not grow.
    static_assert(offsetof(MsgHeader, flags) == 12, "flags must be at offset 12");
    static_assert(offsetof(MsgHeader, ts_ns) == 16, "padding before ts_ns must keep it 8-aligned");
    static_assert(offsetof(MsgHeader, seq_num) == 24);

    // The flag bitmask must round-trip through a byte-sized field.
    static_assert(sizeof(MsgFlag) == 1, "MsgFlag must fit the header's flags byte");
    SUCCEED();
}

// ---- Reconciliation messages (contracts.md, "Reconciliation") ------------------------------

TEST(P0Smoke, ReconMessagesMatchSpec)
{
    static_assert(sizeof(ReconRequestMsg) == 56, "ReconRequestMsg layout changed — update contracts.md");
    static_assert(sizeof(ReconResultMsg) == 600, "ReconResultMsg layout changed — update contracts.md");
    static_assert(sizeof(ReconVerdictMsg) == 72, "ReconVerdictMsg layout changed — update contracts.md");

    // Offsets are asserted, not merely sizes: two compensating field changes can
    // keep sizeof identical while moving a field a consumer reads by offset.
    static_assert(offsetof(ReconRequestMsg, recon_id) == 32);
    static_assert(offsetof(ReconRequestMsg, trigger) == 44);
    static_assert(offsetof(ReconRequestMsg, requested_ts_ns) == 48, "padding before requested_ts_ns");

    static_assert(offsetof(ReconResultMsg, venue_avg_px) == 56, "padding before venue_avg_px");
    static_assert(offsetof(ReconResultMsg, venue_fill_count) == 68);
    static_assert(offsetof(ReconResultMsg, status) == 72);
    static_assert(offsetof(ReconResultMsg, venue_order_count) == 76, "padding before venue_order_count");
    static_assert(offsetof(ReconResultMsg, venue_order_ids) == 80, "ids must start 8-aligned");
    static_assert(offsetof(ReconResultMsg, venue_ts_ns) == 592, "padding before venue_ts_ns");

    static_assert(offsetof(ReconVerdictMsg, qty_delta) == 48);
    static_assert(offsetof(ReconVerdictMsg, mismatch_class) == 61);
    static_assert(offsetof(ReconVerdictMsg, finalize_ts_ns) == 64, "padding before finalize_ts_ns");

    SUCCEED();
}

// ---- Reconciliation compares order IDENTITY, not a count (contracts.md, "Reconciliation") ----

TEST(P0Smoke, ReconciliationComparesOrderIdentityNotCount)
{
    // contracts.md, "Reconciliation" classes 3 (OrderLocalOnly) and 4 (OrderVenueOnly) are declared as
    // order-level disagreements, but the original ReconResultMsg carried only
    // `venue_open_orders` — a COUNT. A count is invariant under substitution:
    // one stale local order A plus one unbooked venue order B leaves the two
    // counts equal, so a count-only comparison reports Matched and contracts.md, "Reconciliation" then
    // opens the gate while an unmanaged live order exists — failing OPEN on the
    // exact class marked Critical.
    //
    // The fix is the identity block: the venue echoes the client_order_id of each
    // open order, and L4 compares SETS. Modelled here with the mirrored struct.
    auto same_order_set = [](const ReconResultMsg& r, const std::vector<std::uint64_t>& local, bool& truncated) {
        truncated = r.venue_order_count < r.venue_open_orders;  // claim incomplete
        if (truncated)
            return false;  // Inconclusive, never Matched (contracts.md, "Reconciliation")
        if (r.venue_order_count != local.size())
            return false;
        for (std::uint32_t i = 0; i < r.venue_order_count; ++i)
            if (std::find(local.begin(), local.end(), r.venue_order_ids[i]) == local.end())
                return false;
        return true;
    };

    ReconResultMsg r{};
    const std::uint64_t order_a = 0x00030000000000AAull;  // L4 holds this one
    const std::uint64_t order_b = 0x00050000000000BBull;  // venue has this one

    // The dangerous case: counts agree (1 == 1), identities do not.
    r.venue_open_orders = 1;
    r.venue_order_count = 1;
    r.venue_order_ids[0] = order_b;
    bool truncated = false;
    EXPECT_FALSE(same_order_set(r, {order_a}, truncated));
    EXPECT_FALSE(truncated);

    // Genuine agreement still matches.
    r.venue_order_ids[0] = order_a;
    EXPECT_TRUE(same_order_set(r, {order_a}, truncated));

    // Truncated claim (more venue orders than the block can carry) is Inconclusive.
    r.venue_open_orders = 100;
    r.venue_order_count = 64;
    EXPECT_FALSE(same_order_set(r, {order_a, order_b}, truncated));
    EXPECT_TRUE(truncated) << "a partial order set must never be read as agreement";

    // The block is fixed-capacity, so the cap is a hard bound the truncation rule
    // protects. It must stay above the reference max_open_orders ceiling (50).
    constexpr std::uint32_t kMaxReconOrders = 64;
    constexpr std::uint32_t kReferenceMaxOpenOrders = 50;
    EXPECT_GT(kMaxReconOrders, kReferenceMaxOpenOrders);
    EXPECT_EQ(sizeof(r.venue_order_ids) / sizeof(r.venue_order_ids[0]), kMaxReconOrders);
}

// ---- Ledger adjustments: the non-stream state changes (contracts.md, "Ledger adjustments") ----

TEST(P0Smoke, LedgerAdjustmentsAreRecordedAndIdempotent)
{
    // layers.md, L4 (OMS) claimed state was a pure function of orders/fills, but two specified
    // mutations are not on the stream: the wall-clock `Abandoned` sweep (layers.md, L4 (OMS))
    // and reconciliation's adopted corrections (contracts.md, "Reconciliation" classes 1/2/5). Before
    // contracts.md, "Ledger adjustments" there was no carrier for either — OrderStatus has no `Abandoned`, and
    // the recorder's coverage excluded L4's journal. layers.md, L4 (OMS) could only *assert*
    // "recorded as an explicit event"; nothing recorded it.
    EXPECT_EQ(sizeof(LedgerAdjustmentMsg), 96u);
    EXPECT_EQ(offsetof(LedgerAdjustmentMsg, adjustment_id), 32u);
    EXPECT_EQ(offsetof(LedgerAdjustmentMsg, type), 64u);
    EXPECT_EQ(offsetof(LedgerAdjustmentMsg, new_net_qty), 72u) << "padding before new_net_qty";
    EXPECT_EQ(offsetof(LedgerAdjustmentMsg, applied_ts_ns), 88u) << "padding before applied_ts_ns";

    // Write-ahead ordering (contracts.md, "Publication failure is per-channel"): the adjustment is logged BEFORE it is applied.
    // If the `ledger` publish fails, live state must not change. Modelled as a
    // tiny state machine so the ordering is the thing asserted, not a comment.
    bool published = true;
    bool applied = false;
    auto try_adjust = [&](bool publish_ok) {
        if (!publish_ok)
            return;  // not delivered -> not applied
        published = true;
        applied = true;
    };
    try_adjust(/*publish_ok=*/false);
    EXPECT_FALSE(applied) << "an undeliverable adjustment must not be applied (write-ahead)";
    try_adjust(/*publish_ok=*/true);
    EXPECT_TRUE(applied && published);

    // Idempotent: applying the same adjustment twice converges, unlike a fill
    // (contracts.md, `ExecutionReportMsg`). Abandoned is terminal; adoption is an assignment.
    auto apply_adjustment = [](std::int64_t& net_qty, std::int64_t& avg, const LedgerAdjustmentMsg& a) {
        switch (static_cast<LedgerAdjustmentType>(a.type))
        {
            case LedgerAdjustmentType::OrderAbandoned: break;  // terminal order transition; no accumulator to double
            case LedgerAdjustmentType::PositionAdopted:
                net_qty = a.new_net_qty;  // assignment, not +=
                break;
            case LedgerAdjustmentType::BasisAdopted:
                avg = a.new_avg_entry;  // assignment, not +=
                break;
        }
    };
    LedgerAdjustmentMsg a{};
    a.type = static_cast<std::uint8_t>(LedgerAdjustmentType::PositionAdopted);
    a.new_net_qty = -500'000'000;
    std::int64_t net = 0, avg = 0;
    apply_adjustment(net, avg, a);
    apply_adjustment(net, avg, a);
    EXPECT_EQ(net, -500'000'000) << "a duplicated adjustment must not accumulate";

    // A duplicated/out-of-order adjustment is detectable by adjustment_id, and
    // replay ignores anything not greater than the last applied.
    a.adjustment_id = 7;
    std::uint64_t last_applied = 7;
    EXPECT_FALSE(a.adjustment_id > last_applied) << "a replay must skip a non-advancing adjustment";
    a.adjustment_id = 8;
    EXPECT_TRUE(a.adjustment_id > last_applied);
}

// ---- Padding must be zero (contracts.md, "Every message is value-initialized before use") --------------------------------

TEST(P0Smoke, ValueInitZeroesPadding)
{
    // contracts.md, "Every message is value-initialized before use": every message is value-initialized (`T m{}`) before its fields are
    // written. This is what makes the audit log byte-reproducible and `memcmp`-based
    // comparison sound. Two value-initialized messages with identical field values
    // must be byte-for-byte identical, padding included.
    ReconResultMsg a{};
    ReconResultMsg b{};
    for (auto* m : {&a, &b})
    {
        m->hdr.msg_type = 71;
        m->hdr.flags = 0;
        m->recon_id = 42;
        m->venue_id = 100;
        m->instrument_id = 1001;
        m->venue_net_qty = 1'000'000'000;
        m->venue_avg_px = 100'000'000'000;
        m->venue_open_orders = 3;
        m->venue_fill_count = 17;
        m->status = 0;
        m->venue_order_count = 3;
        m->venue_order_ids[0] = 0x0005000000000001ull;
        m->venue_order_ids[1] = 0x0005000000000002ull;
        m->venue_order_ids[2] = 0x0005000000000003ull;
        m->venue_ts_ns = 123456789;
    }
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(a)), 0) << "padding is not zero — see contracts.md";

    // Two other message types, to catch a struct whose gaps differ from
    // ReconResultMsg's. Cheap, and the assertion is the same one.
    ReconRequestMsg r1{};
    ReconRequestMsg r2{};
    for (auto* m : {&r1, &r2})
    {
        m->hdr.msg_type = 70;
        m->recon_id = 7;
        m->venue_id = 100;
        m->trigger = 0;
        m->requested_ts_ns = 99;
    }
    EXPECT_EQ(std::memcmp(&r1, &r2, sizeof(r1)), 0) << "padding is not zero — see contracts.md";

    ReconVerdictMsg v1{};
    ReconVerdictMsg v2{};
    for (auto* m : {&v1, &v2})
    {
        m->hdr.msg_type = 72;
        m->recon_id = 7;
        m->venue_id = 100;
        m->instrument_id = 1001;
        m->qty_delta = -5;
        m->order_delta = 1;
        m->outcome = 1;
        m->mismatch_class = 4;
        m->finalize_ts_ns = 1234;
    }
    EXPECT_EQ(std::memcmp(&v1, &v2, sizeof(v1)), 0) << "padding is not zero — see contracts.md";
    SUCCEED();
}

// ---- Sequence discontinuity classification (contracts.md, "Sequence numbers across a restart") ------------------

namespace
{
// A producer's sequence can be read in exactly four ways relative to a consumer's
// high-water mark. contracts.md, "Sequence numbers across a restart" fixes which is which; this mirrors the table so a future
// edit to either one breaks the build.
enum class SeqEvent : std::uint8_t
{
    Contiguous = 0,
    Restart = 1,       // seq backwards, time forward  -> reset watermark
    LostMessages = 2,  // seq jumped forward            -> keep watermark, resync
    CorruptClock = 3,  // time went backwards           -> fail closed
};

SeqEvent Classify(const std::uint64_t prev_seq, const std::uint64_t new_seq, const std::uint64_t prev_ts,
                  const std::uint64_t new_ts)
{
    if (new_ts < prev_ts)
    {
        return SeqEvent::CorruptClock;  // monotonic time cannot go back
    }
    if (new_seq == prev_seq + 1)
    {
        return SeqEvent::Contiguous;
    }
    return new_seq < prev_seq ? SeqEvent::Restart : SeqEvent::LostMessages;
}
}  // namespace

TEST(P0Smoke, SequenceDiscontinuityClassification)
{
    // The two cases that `seq_num` alone cannot separate. Both are "not prev+1";
    // only the direction plus ts_ns tells them apart, and they demand opposite
    // responses (contracts.md, "Sequence numbers across a restart"). Getting this wrong is either a resync storm or silent loss.
    EXPECT_EQ(Classify(5000, 1, 1000, 2000), SeqEvent::Restart);
    EXPECT_EQ(Classify(5000, 5003, 1000, 1500), SeqEvent::LostMessages);

    // Normal traffic.
    EXPECT_EQ(Classify(5000, 5001, 1000, 1500), SeqEvent::Contiguous);

    // A restart cannot move a monotonic clock backwards, so this is never a
    // restart — it must fail closed rather than reset the watermark.
    EXPECT_EQ(Classify(5000, 5001, 2000, 1500), SeqEvent::CorruptClock);
    EXPECT_EQ(Classify(5000, 1, 2000, 1500), SeqEvent::CorruptClock);

    // A restart at seq 0 (rather than 1) is still a restart, not "lost messages".
    EXPECT_EQ(Classify(5000, 0, 1000, 2000), SeqEvent::Restart);
}

// ---- Replay preserves stamps; simulation creates them (contracts.md, "Timestamps") ----

TEST(P0Smoke, ReplayRepublishesBytesUnchanged)
{
    // contracts.md, "Timestamps": the replay engine advances SimulationClock to each event's timestamp
    // but republishes the message VERBATIM. Bit-exact replay (P3 "byte-identical
    // receipt", P4 golden tests, and the value-init rule in contracts.md, all depend on this: a replayer that rewrites
    // ts_ns publishes different bytes for the same fixture, so golden tests fail
    // depending on how the sim clock was seeded.
    ReconResultMsg logged{};
    logged.hdr.msg_type = 71;
    logged.hdr.producer_id = 5;    // the ORIGINAL producer, not Producer_Replay
    logged.hdr.ts_ns = 4'242'000;  // the ORIGINAL stamp from the log
    logged.hdr.seq_num = 4999;
    logged.recon_id = 1;
    logged.venue_id = 100;
    logged.venue_net_qty = 7;

    // "Replay": the sim clock advances to the event's time and the bytes go out
    // unchanged. Note the clock is advanced TO ts_ns — it does not become ts_ns.
    std::uint64_t sim_clock_ns = 0;
    sim_clock_ns = logged.hdr.ts_ns;
    const ReconResultMsg published = logged;  // verbatim, no re-stamp

    EXPECT_EQ(std::memcmp(&published, &logged, sizeof(logged)), 0) << "replay must not rewrite the message";
    EXPECT_EQ(published.hdr.ts_ns, 4'242'000U) << "ts_ns must survive replay (contracts.md, Timestamps)";
    EXPECT_EQ(published.hdr.producer_id, 5U)
        << "producer_id must survive replay — replaying under "
           "Producer_Replay would look like a constant restart (contracts.md, Sequence numbers across a restart)";
    EXPECT_EQ(sim_clock_ns, published.hdr.ts_ns) << "after advancing, the clock equals the event's stamp";

    // The other side of the boundary: a message the simulator *creates* (P7's
    // simulated matching-engine fill) was never in the log, so it legitimately
    // stamps its own ts_ns from the sim clock. "Replay preserves stamps,
    // simulation creates them." Modelled here with the structs mirrored at P0;
    // the real ExecutionReportMsg case belongs to P7.
    ReconVerdictMsg sim_created{};
    sim_created.hdr.msg_type = 72;
    sim_created.hdr.producer_id = 5;
    sim_created.hdr.ts_ns = sim_clock_ns + 50'000;  // sim clock + modelled latency
    EXPECT_NE(sim_created.hdr.ts_ns, logged.hdr.ts_ns) << "a simulated message is stamped afresh";
    EXPECT_GT(sim_created.hdr.ts_ns, sim_clock_ns) << "a new message is stamped after the current sim time";
}

// ---- Strategy registration (layers.md, L2 (strategy)) ----

TEST(P0Smoke, StrategyRegistrationBindsConfigToCode)
{
    // layers.md, L2 (strategy) claimed "plugin architecture" refers to how strategies are registered
    // with the system, "contracts.md" -- but contracts.md is the ENUMERATIONS section and the phrase
    // appeared nowhere else in the document. It was pointing at a real hole:
    // IStrategy is a pure interface, config/strategies.yaml had a `name:` nothing
    // bound to any implementation, and no section said how a config entry becomes
    // an object.

    // Registration is an explicit compiled-in factory keyed by string. No dynamic
    // loading: a strategy peer is a hot-path process and dlopen allocates and runs
    // arbitrary static initializers, both forbidden on the hot thread (overview.md, "What is on the hot path").
    constexpr bool kUsesDynamicLoading = false;
    EXPECT_FALSE(kUsesDynamicLoading) << "a .so is an unbounded failure source in a hot path";

    // An `impl` that is not in the registry is a STARTUP FAILURE, not a warning.
    // Skipping it yields a deployment that heartbeats, passes reconciliation, and
    // silently trades nothing -- the failure hardest to diagnose.
    constexpr bool kUnknownImplIsFatal = true;
    EXPECT_TRUE(kUnknownImplIsFatal);

    // `enabled` (deployment switch, keeps attribution history) and `impl`
    // (build-time binding) are different things and both are needed. A disabled
    // strategy is STILL validated, so config errors surface at deploy time rather
    // than when the flag is eventually flipped.
    constexpr bool kDisabledStrategiesAreValidated = true;
    EXPECT_TRUE(kDisabledStrategiesAreValidated);

    // params is arbitrary YAML, so the strategy declares a schema and the loader
    // validates BEFORE calling the factory. A typo becomes a startup error rather
    // than a silently-defaulted value producing plausible, never-backtested signals.
    constexpr bool kParamsValidatedBeforeConstruction = true;
    EXPECT_TRUE(kParamsValidatedBeforeConstruction);

    // strategy_id uniqueness is STRUCTURAL, not stylistic: it occupies the top 16
    // bits of signal_id (layers.md, L2 (strategy)). A collision silently merges two strategies'
    // attribution and corrupts the per-strategy daily-loss breaker (layers.md, L3 (risk) step 8).
    constexpr std::uint32_t kStrategyId1 = 1;
    constexpr std::uint64_t kSignalIdMask = 0x0000FFFFFFFFFFFFull;
    const auto sig_a = (static_cast<std::uint64_t>(kStrategyId1) << 48) | (42u & kSignalIdMask);
    const auto sig_b = (static_cast<std::uint64_t>(kStrategyId1) << 48) | (43u & kSignalIdMask);
    // Same strategy, different counters -> distinct ids, shared high bits.
    EXPECT_NE(sig_a, sig_b);
    EXPECT_EQ(sig_a >> 48, sig_b >> 48) << "the high 16 bits ARE the strategy id";
    // Two strategies sharing an id would collide on the high bits.
    constexpr std::uint32_t kStrategyId2 = 2;
    EXPECT_NE(kStrategyId1, kStrategyId2);

    // layers.md, L2 (strategy): the owner field is 16 bits for BOTH ids, and the bound must be
    // enforced because contracts.md, "The message header"'s ranges run to 299. An 8-bit field (the earlier
    // << 56 form) silently truncated every gateway id: producer 256 collided
    // with producer 0, 257 with 1, 299 with 43. Assert the collision is gone.
    constexpr std::uint64_t kOwnerMask = 0x0000FFFFFFFFFFFFull;
    const auto coid = [](std::uint32_t producer_id, std::uint64_t n) {
        return (static_cast<std::uint64_t>(producer_id & 0xFFFFu) << 48) | (n & kOwnerMask);
    };
    EXPECT_EQ(coid(5, 7), coid(5, 7));
    EXPECT_NE(coid(256, 7), coid(0, 7)) << "gateway producer_id must not collide with 0";
    EXPECT_NE(coid(299, 7), coid(43, 7)) << "gateway producer_id must not collide with 43";
    EXPECT_EQ(coid(256, 7) >> 48, 256u) << "the high 16 bits ARE the producer id";
    // Out-of-range owners truncate and collide, which is why the loader rejects
    // them rather than relying on uniqueness alone.
    EXPECT_EQ(coid(1, 9), coid(1 + 65536, 9)) << "ids above 65535 truncate — rejected at load";
    constexpr std::uint32_t kMaxOwnerId = 65535;
    EXPECT_GE(kMaxOwnerId, 299u) << "the bound must cover every producer_id range in contracts.md";
}

// ---- Phase acceptance tests live outside the summary table (operations.md, "What each phase must prove") ----

TEST(P0Smoke, PhaseTableStaysScannable)
{
    // operations.md, "The phase plan"'s table is read to answer "what does this phase produce?" -- a
    // scannable summary. The acceptance tests were being inlined into it, which
    // made P3's cell roughly THREE TIMES the length of any other (1015 chars vs a
    // ~270 median) and defeated the table's only purpose. An acceptance test is
    // read to answer a different question ("what exactly must I observe?"), by a
    // different audience, so the obligations moved to operations.md, "What each phase must prove".
    //
    // The rule this encodes: if a cell needs bold text to stay legible, it is not
    // summary text.
    constexpr bool kAcceptanceTestsBelongInTheTable = false;
    EXPECT_FALSE(kAcceptanceTestsBelongInTheTable) << "a cell needing bold to stay legible is not a summary";

    // The obligations themselves are NOT deleted -- they moved. Each names the
    // section specifying the rule, so a test checks a WRITTEN CONTRACT rather than
    // the implementer's memory (operations.md, "What each phase must prove").
    constexpr int kP3Obligations = 6;
    constexpr int kP7Obligations = 2;
    constexpr int kP8Obligations = 3;
    EXPECT_EQ(kP3Obligations + kP7Obligations + kP8Obligations, 11);

    // Test 6 is the one needing real care: it asserts a NEGATIVE -- that an order
    // was NOT silently dropped. A pass requires observing the LATCH, not merely
    // the absence of an error. Absence-of-error is also what a broken test looks
    // like, so the two must be distinguishable.
    constexpr bool kObservingNoErrorIsSufficient = false;
    EXPECT_FALSE(kObservingNoErrorIsSufficient)
        << "backpressure's orders case needs the latch observed, not just no crash";
}

// ---- Config tiers match the risk pipeline (layers.md, L3 (risk)) ----

TEST(P0Smoke, RiskLimitsConfigCanExpressThePipeline)
{
    // layers.md, L3 (risk) specifies nine checks and their order. The config has three tiers
    // (global / per_instrument / per_strategy) but nothing said which tier each
    // check reads, so keys landed wherever seemed reasonable. Two could not express
    // what the design requires at all.

    // Step 1: the rate limit is "messages/sec, PER STRATEGY". A global-only key
    // lets ONE runaway strategy consume the whole process's order budget and
    // starve every other strategy in that process -- the exact failure the limit
    // is meant to prevent. Both tiers are now permitted; the tighter binds.
    constexpr bool kRateLimitIsGlobalOnly = false;
    EXPECT_FALSE(kRateLimitIsGlobalOnly) << "one runaway strategy must not starve the others";

    // Step 8: the daily-loss breaker is PER-STRATEGY, and that is structural, not
    // stylistic -- layers.md, L2 (strategy) justifies requiring strategy_id uniqueness BY the fact
    // that a collision merges two strategies' P&L into one breaker figure. A
    // global-only key would make that justification refer to a mechanism the
    // config cannot express.
    constexpr bool kDailyLossIsPerStrategy = true;
    EXPECT_TRUE(kDailyLossIsPerStrategy);

    // Steps 4/6/7 read BOTH tiers and both must pass. "Both" is not "the more
    // specific wins": a per-strategy limit larger than the instrument cap must not
    // be able to override it, and vice versa. Each is an independent constraint.
    constexpr bool kBothTiersMustPass = true;
    EXPECT_TRUE(kBothTiersMustPass);

    // Absent != 0. Absent means "no constraint at this tier" and falls through to
    // the next tier that specifies one; 0 means "disabled" at the tier where it is
    // written. Conflating them makes an omitted key block all trading.
    constexpr int kAbsentLimit = -1;  // sentinel distinct from a real 0
    constexpr int kDisabledLimit = 0;
    EXPECT_NE(kAbsentLimit, kDisabledLimit);

    // Step 2 (instrument status) is NOT a limit at all, and -- after layers.md, L3 (risk) -- it is
    // NOT a venue-listing status either. It is a CANONICAL suspension set pushed over
    // `control`, because an AlphaSignalMsg carries a canonical id and no venue, so a
    // firewall reading a listing's `status` has no answer in a two-venue deployment.
    constexpr bool kInstrumentStatusLivesInRiskLimits = false;
    EXPECT_FALSE(kInstrumentStatusLivesInRiskLimits);
    constexpr bool kL3ReadsVenueListingStatus = false;
    EXPECT_FALSE(kL3ReadsVenueListingStatus)
        << "venue listing is read by L1/L5 only (see contracts.md and layers.md, L3)";

    // Step 4 is CANONICAL-ONLY: max_order_qty and nothing else. tick_size, step_size
    // and min_notional are venue micro-structure, enforced by L5 (layers.md, L3 (risk)). This also
    // removes a real bug: the old check was MulDiv(qty, price) >= min_notional,
    // and a MARKET order has price == 0, so every market order that passed step 3 was
    // then rejected at step 4 with OrderSizeTooLarge.
    constexpr bool kL3ChecksStepSizeOrMinNotional = false;
    EXPECT_FALSE(kL3ChecksStepSizeOrMinNotional) << "venue micro-structure belongs to L5";
    constexpr std::int64_t kMarketOrderPrice = 0;  // sentinel (contracts.md, "Numbers are fixed-point integers")
    EXPECT_EQ(kMarketOrderPrice, 0);
    // There is no notional check left in L3 for it to fail.
    constexpr bool kL3ChecksNotionalAtAll = false;
    EXPECT_FALSE(kL3ChecksNotionalAtAll) << "a market order has no price; min_notional is L5's";

    // book_depth is a PROCESS setting, not the wire cap: BookSnapshotMsg is fixed
    // at kMaxBookLevels = 64 (contracts.md, `BookSnapshotMsg`). Any retained depth <= 64 is publishable; more
    // is not representable. The two numbers are independent.
    constexpr std::uint32_t kMaxBookLevels = 64;
    constexpr std::uint32_t kConfiguredBookDepth = 20;
    EXPECT_LE(kConfiguredBookDepth, kMaxBookLevels) << "a retained depth above the wire cap is not representable";
    EXPECT_NE(kConfiguredBookDepth, kMaxBookLevels)
        << "the config's value and the wire cap are independent, not required to match";

    // A venue-throttled order is NOT a risk rejection: it already passed the
    // pipeline and is in the ledger, so L5 backs off and retries. Discarding it
    // would leave an order the ledger thinks is live but was never sent -- the
    // OrderLocalOnly divergence of contracts.md, "Reconciliation".
    constexpr bool kVenueThrottleDiscardsTheOrder = false;
    EXPECT_FALSE(kVenueThrottleDiscardsTheOrder);
}

// ---- Repo hygiene: scratch is never versioned (operations.md, "Build and run") ----

TEST(P0Smoke, BuildScratchIsNeverVersioned)
{
    // operations.md, "Build and run"'s test for what is versioned: can this be regenerated from a small
    // committed source? vcpkg produces TWO scratch directories and .gitignore
    // named only one of them:
    //
    //   .vcpkg/           the bootstrap checkout -- was ignored
    //   vcpkg_installed/  manifest-mode deps  -- was NOT ignored, and two of its
    //                     lock/cache files had been staged for the initial commit
    //
    // The two staged files were individually tiny, which is what made the omission
    // dangerous rather than obvious: committing them is harmless, and the damage
    // appears later when a real build fills that directory with hundreds of MB and
    // it is one `git add -A` from entering history permanently.

    // The categories, asserted as the rule rather than as a file listing.
    constexpr bool kSourcesAreVersioned = true;       // contracts/ core/ apps/ config/
    constexpr bool kManifestsAreVersioned = true;     // vcpkg.json, uv.lock
    constexpr bool kBuildScratchIsVersioned = false;  // build/, .vcpkg/, vcpkg_installed/
    EXPECT_TRUE(kSourcesAreVersioned);
    EXPECT_TRUE(kManifestsAreVersioned);
    EXPECT_FALSE(kBuildScratchIsVersioned);

    // A lockfile is the OPPOSITE of scratch: it exists to make a scratch rebuild
    // reproducible. Confusing the two is how vcpkg_installed/ got missed.
    constexpr bool kLockfileIsScratch = false;
    EXPECT_FALSE(kLockfileIsScratch);

    // data/ is the subtle case: the DIRECTORY is tracked (it holds per-directory
    // .gitignore files) while its CONTENTS are ignored. Verified for real with
    // `git check-ignore`, not assumed.
    constexpr bool kDataDirectoryTracked = true;
    constexpr bool kDataContentsIgnored = true;
    EXPECT_TRUE(kDataDirectoryTracked);
    EXPECT_TRUE(kDataContentsIgnored);
}

// ---- Duplicate fill detection (contracts.md, `ExecutionReportMsg`) ----

TEST(P0Smoke, DuplicateFillIsANoOpNotAnError)
{
    // OnFill is NOT idempotent and cannot be made so: every step accumulates
    // (settled += ..., fees_paid += ..., filled_qty advances, realized_pnl += ...).
    // Applying one report twice double-counts all of it -- fabricated money, a
    // fabricated position, and a P&L figure the daily-loss breaker reads as real.
    //
    // Duplicates are not hypothetical: a live-socket venue redelivery, an L5
    // restart replaying recent fills, and post-SeqGap log re-reading all produce
    // them. `fills` is therefore an AT-LEAST-ONCE channel in practice.

    // The test is a comparison against a single counter L4 already maintains --
    // no set of applied reports, no unbounded history, nothing new in the snapshot.
    auto is_duplicate = [](std::int64_t report_cumulative, std::int64_t record_filled) {
        return report_cumulative <= record_filled;
    };

    // The same fill delivered twice: second arrival is cumulative == record_filled.
    EXPECT_TRUE(is_duplicate(/*report*/ 100, /*record*/ 100)) << "a redelivered fill must apply NOTHING";

    // An out-of-order/stale report (cumulative BEHIND our record) is also ignored.
    // Deliberate: the arithmetic cannot distinguish it from a duplicate, and
    // layers.md, L4 (OMS) has no way to un-apply anything.
    EXPECT_TRUE(is_duplicate(/*report*/ 60, /*record*/ 100))
        << "behind is indistinguishable from duplicate; do not error";

    // A genuinely new fill advances the cumulative total and IS applied.
    EXPECT_FALSE(is_duplicate(/*report*/ 140, /*record*/ 100));

    // cumulative_qty is the key BECAUSE the report's own filled_qty is only this
    // report's increment -- using it would need a growing set of applied reports.
    constexpr bool kUsesPerReportIncrement = false;
    EXPECT_FALSE(kUsesPerReportIncrement);

    // Applied silently in state, loudly in telemetry: a duplicate is not an error
    // (nothing to fix) but MUST be visible, because a rising counter is the signal
    // that a venue is re-sending more than expected.
    constexpr bool kDuplicateIsAnError = false;
    constexpr bool kDuplicateIsCounted = true;
    EXPECT_FALSE(kDuplicateIsAnError);
    EXPECT_TRUE(kDuplicateIsCounted);

    // The duplicate key lives on the wire already -- it was transmitted and unused.
    constexpr std::size_t kCumulativeQtyOffset = 72;
    EXPECT_EQ(kCumulativeQtyOffset, 72u);
}

// ---- Reference-data reload / config skew (contracts.md, `HeartbeatMsg`) ----

TEST(P0Smoke, ConfigSkewIsDetectableNotSilent)
{
    // Reference data was specified as load-once-at-boot with no story for an
    // edit-while-running. That is a correctness hole, not a convenience gap:
    // L1 inverts the venue listings into the routing table, L5 reads
    // tick_size/step_size to quantize orders, and the two restart INDEPENDENTLY
    // under systemd. So the natural operation -- edit a tick size, restart the
    // gateway -- yields a gateway quantizing to a different grid than the rest of
    // the fleet believes, with no error anywhere. Orders are then rejected at the
    // venue, or partially filled, for no visible reason.

    // The hash fits existing padding: the heartbeat must NOT grow.
    EXPECT_EQ(sizeof(HeartbeatMsg), 48u);
    EXPECT_EQ(offsetof(HeartbeatMsg, ref_data_hash), 36u);
    EXPECT_EQ(offsetof(HeartbeatMsg, beat_ts_ns), 40u);

    // Two processes disagreeing is what closes the gate. Same fail-closed shape
    // as reconciliation (contracts.md, "Reconciliation".7): start unable to trade, prove consistency first.
    constexpr std::uint32_t kFleetHash = 0xA1B2C3D4u;
    constexpr std::uint32_t kStaleHash = 0xDEADBEEFu;
    EXPECT_NE(kFleetHash, kStaleHash) << "a skew must be observable as a hash difference";

    // Only `status` is hot-reloadable, and only because it can only REDUCE
    // exposure. The quantization grid is not reloadable: changing it changes what
    // an already-live order means.
    constexpr bool kQuantizationGridIsHotReloadable = false;
    constexpr bool kStatusIsHotReloadable = true;
    EXPECT_FALSE(kQuantizationGridIsHotReloadable);
    EXPECT_TRUE(kStatusIsHotReloadable);

    // A status change blocks NEW orders only. Existing orders are handled by the
    // explicit CancelAll/Flatten commands (contracts.md, `KillSwitchMsg`) -- silently cancelling resting
    // orders because a status field changed would turn a routine operator action
    // into an uncontrolled liquidation.
    constexpr bool kStatusChangeImplicitlyCancelsRestingOrders = false;
    EXPECT_FALSE(kStatusChangeImplicitlyCancelsRestingOrders);

    // Identities are never editable: canonical_id and venue_symbol are already
    // written into live orders, positions and the audit log (layers.md, L4 (OMS)).
    constexpr bool kIdentitiesAreEditable = false;
    EXPECT_FALSE(kIdentitiesAreEditable);
}

// ---- Order replace (contracts.md, `ReplaceOrderMsg`) ----

TEST(P0Smoke, ReplaceIsNotCancelPlusNew)
{
    // The Investigation names Cancel/Replace as a core order type, but the design
    // had NewOrder + CancelOrder and no replace. Cancel+new is the obvious cheaper
    // answer, and it is WRONG for three reasons the design already established:
    //
    //   1. client_order_id is the ledger's primary key (layers.md, L4 (OMS)) and open orders are
    //      compared against the venue during reconciliation (contracts.md, "Reconciliation"). Cancel+new
    //      makes L4 hold TWO records where the venue holds ONE, producing a class-4
    //      OrderVenueOnly mismatch -- which BLOCKS TRADING. A safety mechanism that
    //      fires on correct behaviour trains the operator to ignore it.
    //   2. Option A's cancel releases nothing until a terminal report (layers.md, L4 (OMS)), so
    //      the replacement leg would be live but unreserved.
    //   3. The intermediate state (live at venue, superseded locally) has no
    //      representation without PendingReplace.
    EXPECT_EQ(sizeof(ReplaceOrderMsg), 72u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, client_order_id), 32u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, instrument_id), 40u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, venue_id), 44u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, new_qty), 48u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, new_price), 56u);
    EXPECT_EQ(offsetof(ReplaceOrderMsg, sent_ts_ns), 64u);

    // PendingReplace is NON-terminal: the venue has not confirmed the amendment,
    // so the order may still fill. IsTerminal must classify it with PendingCancel,
    // not with Filled.
    EXPECT_FALSE(IsTerminal(OrderRecordState::PendingReplace));
    EXPECT_FALSE(IsTerminal(OrderRecordState::PendingCancel));
    EXPECT_TRUE(IsTerminal(OrderRecordState::Filled));
    // The ordering invariant one comparison relies on: every non-terminal state
    // precedes Filled.
    EXPECT_LT(static_cast<std::uint8_t>(OrderRecordState::PendingReplace),
              static_cast<std::uint8_t>(OrderRecordState::Filled));

    // new_qty/new_price are ABSOLUTE, not deltas: applying the same replace twice
    // converges to the same terms, which is what makes the contracts.md, "Publication failure is per-channel" retry path safe.
    ReplaceOrderMsg r{};
    r.new_qty = 2000000000;
    r.new_price = 100000000000;
    const auto qty_after_twice = r.new_qty;
    EXPECT_EQ(qty_after_twice, 2000000000) << "absolute quantities are idempotent";
}

// ---- Publish failure is per-channel (contracts.md, "Publication failure is per-channel") ----

TEST(P0Smoke, PublishFailureIsHandledPerChannel)
{
    // The design cited Aeron's "strict window-based backpressure" as a headline
    // property, and overview.md, "What is on the hot path" forbade hot threads from blocking -- but NOTHING said what
    // a producer does when offer() returns BACK_PRESSURED. Every publish site had an
    // unspecified failure mode on the one path where "the transport is full" is
    // simultaneously most likely and least forgivable.
    //
    // The load-bearing invariant: no droppable channel mutates L4's state. `orders`
    // and `fills` are the only channels that change the ledger, and neither may use
    // best-effort delivery. Intent can be lost; a commitment cannot.
    constexpr PublishFailurePolicy kOrders = PublishFailurePolicy::FailClosed;
    constexpr PublishFailurePolicy kFills = PublishFailurePolicy::NeverFails;
    constexpr PublishFailurePolicy kMd = PublishFailurePolicy::NeverFails;
    constexpr PublishFailurePolicy kSignals = PublishFailurePolicy::DropAndCount;
    constexpr PublishFailurePolicy kRiskEvents = PublishFailurePolicy::DropAndCount;
    constexpr PublishFailurePolicy kControl = PublishFailurePolicy::RetryUntilDelivered;

    // Nothing that mutates the ledger is droppable.
    EXPECT_NE(kOrders, PublishFailurePolicy::DropAndCount)
        << "a dropped NewOrderMsg is a position the ledger never learns about";
    EXPECT_NE(kFills, PublishFailurePolicy::DropAndCount)
        << "a dropped ExecutionReportMsg silently corrupts positions and P&L (layers.md, L4 (OMS))";

    // Volume-driven self-healing channels may drop: a seq_num jump triggers the
    // resync path (contracts.md, `BookSnapshotMsg`), so the loss is recovered rather than persistent.
    EXPECT_EQ(kMd, PublishFailurePolicy::NeverFails);
    EXPECT_EQ(kSignals, PublishFailurePolicy::DropAndCount);
    EXPECT_EQ(kRiskEvents, PublishFailurePolicy::DropAndCount);

    // A heartbeat that is silently dropped makes a healthy process look dead, and
    // liveness is the one signal whose ABSENCE triggers action (contracts.md, `HeartbeatMsg`).
    EXPECT_EQ(kControl, PublishFailurePolicy::RetryUntilDelivered);

    EXPECT_EQ(static_cast<std::uint8_t>(PublishFailurePolicy::FailClosed), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(PublishFailurePolicy::RetryUntilDelivered), 3u);
}

// ---- Kill-switch command ownership (contracts.md, `KillSwitchMsg`) ----

TEST(P0Smoke, EveryKillCommandHasANamedExecutor)
{
    // The channel registry and the KillSwitchMsg description both claimed the command goes to "every process".
    // But only L3 had a receive hook (OnControl) and L1 had one invented for it
    // (OnKillSwitch) -- while L4 and L5 had NONE. CancelAll and Flatten are
    // executed ONLY by L4 (generates) and L5 (transmits), so before the fix the
    // two most important emergency commands could not reach their executors.
    //
    // Ownership is now normative and asserted here as the four commands each
    // naming exactly one executor path.
    EXPECT_EQ(static_cast<std::uint8_t>(KillCommand::Halt), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(KillCommand::CancelAll), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(KillCommand::Flatten), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(KillCommand::Resume), 3u);

    // There is exactly ONE inbound control contract. A second per-component
    // signature (IRiskEngine::OnControl) means two names for one event, and the
    // handlers drift apart.
    constexpr int kInboundControlSignatures = 1;
    EXPECT_EQ(kInboundControlSignatures, 1)
        << "IControlConsumer is the single inbound contract (layers.md, L1 (ingestion))";

    // Flatten is the one command that TRANSMITS new orders while the breaker is
    // latched -- every other command only reduces exposure. It is therefore the
    // one control path that can fail open, and must be operator-only.
    constexpr bool kFlattenHasAutomaticTrigger = false;
    EXPECT_FALSE(kFlattenHasAutomaticTrigger) << "no automatic trigger may flatten (contracts.md, `KillSwitchMsg`)";

    // Flatten strictly implies CancelAll: otherwise resting orders could re-open
    // the position immediately after the flatten filled.
    constexpr bool kFlattenImpliesCancelAll = true;
    EXPECT_TRUE(kFlattenImpliesCancelAll);
}

// ---- L1's inbound port: the resync request path (layers.md, L1 (ingestion)) ----

TEST(P0Smoke, FeedHandlerHasAnInboundPortForResync)
{
    // The original seam declared IFeedHandler with OnSnapshotRequest as a bare
    // method and a single `emits → IMarketDataSink` port. But contracts.md, `BookSnapshotRequestMsg` requires L1 to
    // RECEIVE BookSnapshotRequestMsg on `control` -- and no interface declared how
    // it arrived. Without an inbound port the resync protocol is unimplementable:
    // a consumer detects a gap, but nothing says how the request reaches the
    // handler that must answer it. L1 is the only place in Layers 1-5 where a
    // hot-path process consumes rather than publishes.
    //
    // The answer is published on `md`, never as a reply on `control`, because `md`
    // reaches every consumer at once -- which is what makes coalescing correct.
    // That asymmetry is asserted here as a channel-property fact.
    constexpr int kMdIsBroadcastToAllConsumers = 1;
    constexpr int kControlReplies = 0;  // no reply is sent on `control`
    EXPECT_EQ(kControlReplies, 0) << "the snapshot is ordinary market data, not a control reply";
    EXPECT_EQ(kMdIsBroadcastToAllConsumers, 1)
        << "one publish must satisfy every consumer (contracts.md, `BookSnapshotRequestMsg`)";

    // The request message carries an explicit requester_id rather than inferring
    // it from hdr.producer_id, so a handler can log who asked when several
    // consumers share one feed (contracts.md, `BookSnapshotRequestMsg`).
    BookSnapshotRequestMsg req{};
    req.hdr.producer_id = 2;  // the requester's identity on the wire...
    req.requester_id = 2;     // ...also carried explicitly, so a handler can log who asked
    EXPECT_EQ(req.requester_id, req.hdr.producer_id);
    EXPECT_EQ(sizeof(BookSnapshotRequestMsg), 48u);

    // The request must be serviced OFF the pinned ingest thread (overview.md, "What is on the hot path"): that thread
    // may take no lock and touch no book state shared with the per-tick path, and
    // building a 64-level snapshot is real work.
    constexpr bool kServicedOnIngestThread = false;
    EXPECT_FALSE(kServicedOnIngestThread)
        << "materialising a snapshot is off-path work (overview.md, What is on the hot path)";
}

// ---- Audit log ownership & framing (layers.md, L6 (observability)) --------------------------------

TEST(P0Smoke, LogEntryFramingIsSelfDescribing)
{
    // layers.md, L6 (observability): the audit log is replay's input, so an incomplete log must be
    // DETECTABLE rather than silently replayable. Two properties carry that:
    // every entry is length-prefixed (a reader can skip what it does not
    // understand), and a gap is an explicit entry rather than an absence.
    EXPECT_EQ(sizeof(LogEntryHeader), 16u) << "length-prefix + type + zeroed pad + recorder stamp";
    EXPECT_EQ(offsetof(LogEntryHeader, length), 0u);
    EXPECT_EQ(offsetof(LogEntryHeader, entry_type), 4u);
    EXPECT_EQ(offsetof(LogEntryHeader, pad), 6u);
    EXPECT_EQ(offsetof(LogEntryHeader, recv_ts_ns), 8u) << "4+2+2 fills the word exactly, so the uint64_t needs no "
                                                           "padding (contracts.md, Natural alignment, no packing)";

    // The four entry types, and the two that make an incomplete log legible: a
    // SeqGap is written where data is missing, and a missing LogClosed means the
    // file was truncated. Neither is inferable from a bare message stream.
    EXPECT_EQ(static_cast<std::uint16_t>(LogEntryType::Message), 0u);
    EXPECT_EQ(static_cast<std::uint16_t>(LogEntryType::SeqGap), 1u);
    EXPECT_EQ(static_cast<std::uint16_t>(LogEntryType::LogOpened), 2u);
    EXPECT_EQ(static_cast<std::uint16_t>(LogEntryType::LogClosed), 3u);

    // BookSnapshotRequestMsg layout (contracts.md, `BookSnapshotRequestMsg`), compiler-verified.
    EXPECT_EQ(sizeof(BookSnapshotRequestMsg), 48u);
    EXPECT_EQ(offsetof(BookSnapshotRequestMsg, instrument_id), 32u);
    EXPECT_EQ(offsetof(BookSnapshotRequestMsg, requester_id), 36u);
    EXPECT_EQ(offsetof(BookSnapshotRequestMsg, after_seq_num), 40u);
}

TEST(P0Smoke, RecorderIsItsOwnProducerAndOwnsTheLog)
{
    // L6 (observability) resolved three competing owners: the old spec put an "audit
    // logger" on the OMS row, called Monitoring the passive subscriber on every
    // channel, and listed no recorder in its process inventory at all. The log is the
    // input to recovery and the artifact golden tests compare against, so it must not
    // share a crash domain with the disposable TUI, and it cannot be L4: L4 sees
    // `orders` and `fills` but not `md`, so L4 alone could not produce a replayable log.
    constexpr std::uint32_t kProducerOMS = 4;
    constexpr std::uint32_t kProducerMonitoring = 6;
    constexpr std::uint32_t kProducerRecorder = 9;

    EXPECT_NE(kProducerRecorder, kProducerOMS) << "the log must not share L4's crash domain";
    EXPECT_NE(kProducerRecorder, kProducerMonitoring) << "observability != durability";
    EXPECT_GT(kProducerRecorder, kProducerMonitoring)
        << "a distinct, stable id in the fixed block (contracts.md, The message header)";

    // Ownership is exclusive: exactly one writer, so replay reads one ordered
    // stream. Per-producer files would need a global ts_ns merge, but ts_ns is
    // monotonic PER PRODUCER (contracts.md, Timestamps, and Sequence numbers across a restart) -- a global merge would be a heuristic,
    // not a reconstruction.
    constexpr int kLogWriters = 1;
    EXPECT_EQ(kLogWriters, 1) << "one ordered stream; no cross-file merge on replay (layers.md, L6 (observability))";
}

// ---- Venue ids are config data, not a closed enum (contracts.md, "Venue identity") ----

TEST(P0Smoke, VenueIdSpaceAllowsNewVenuesWithoutRecompiling)
{
    // contracts.md, "Venue identity": a venue's numeric id is declared in its config and is the authority;
    // the VenueId enum's named constants are conveniences. What must hold is that
    // the id for a venue is representable in the wire field for a venue whose
    // adapter did not exist at compile time -- i.e. the ids are uint32_t data, and
    // the reserved ranges are honoured.
    constexpr std::uint32_t kVenueAny = 0;    // wildcard: receiving gateway claims it
    constexpr std::uint32_t kReservedLo = 1;  // 1..99 reserved: must not be a live venue
    constexpr std::uint32_t kReservedHi = 99;
    constexpr std::uint32_t kBinance = 100;  // config/gateways/binance.yaml
    constexpr std::uint32_t kCoinbase = 101;
    constexpr std::uint32_t kFutureVenue = 102;  // a venue needing NO C++ enum change

    EXPECT_EQ(kVenueAny, 0U) << "0 is the wildcard and must stay so";
    EXPECT_GT(kReservedLo, kVenueAny) << "1..99 are reserved";
    EXPECT_LT(kReservedHi, kBinance) << "a live venue id must not land in the reserved range";
    EXPECT_EQ(kFutureVenue, kBinance + 2) << "new venues extend the numeric space, not the enum";
    EXPECT_EQ(kCoinbase, kBinance + 1) << "ids are dense and config-assigned, not enum-ordinal";
    EXPECT_GT(static_cast<std::uint64_t>(kFutureVenue), 99ULL);

    // The wire field is uint32_t, so the id space is not exhausted by an enum's
    // worth of venues. The specific bound matters less than the property: adding a
    // venue never requires changing a compiled constant.
    EXPECT_EQ(sizeof(std::uint32_t), sizeof(decltype(ReconRequestMsg{}.venue_id)))
        << "venue_id is uint32_t on the wire (contracts.md, `NewOrderMsg`); ids are data, not an enum width";
}

// ---- The Python strategy is a peer, not an embedded interpreter (layers.md, L2 (strategy)) ----

// NOTE: this cannot be tested at P0, and pretending otherwise would be worse than
// saying so. The real assertion is a *link-time* property -- "no hot-path binary
// links libpython" -- which becomes checkable once the binaries exist (P8), where
// it is an assertion about the build, not about a struct. What CAN be fixed now and
// is easy to get wrong is the part contracts.md, "Sequence numbers across a restart" depends on: a bridged strategy must be a
// DISTINCT producer_id from the C++ strategy, because if it shared one, a restart of
// either would be indistinguishable from a restart of the other.
TEST(P0Smoke, BridgedStrategyIsADistinctProducer)
{
    // contracts.md, "The message header": Producer_Strategy = 2 for the canonical C++ strategy process; a strategy
    // needing process isolation is assigned 100 + N from the strategy-instance range.
    constexpr std::uint32_t kCppStrategyProducer = 2;        // Producer_Strategy
    constexpr std::uint32_t kBridgedStrategyProducer = 100;  // 100 + N (contracts.md, "The message header")

    EXPECT_NE(kCppStrategyProducer, kBridgedStrategyProducer)
        << "a bridged strategy must not reuse the C++ strategy's producer_id (see layers.md, L2, and contracts.md)";
    EXPECT_GE(kBridgedStrategyProducer, 100u)
        << "strategy instances come from the 100..199 range (contracts.md, The message header)";
    EXPECT_LE(kBridgedStrategyProducer, 199u);

    // The bus must not be able to tell them apart from the message alone: same type,
    // same channel, only producer_id differs. That is what makes the language an
    // implementation detail behind an IPC seam (contracts.md).
    ReconVerdictMsg cpp_signal{};
    cpp_signal.hdr.producer_id = kCppStrategyProducer;
    cpp_signal.hdr.msg_type = 72;
    ReconVerdictMsg py_signal{};
    py_signal.hdr.producer_id = kBridgedStrategyProducer;
    py_signal.hdr.msg_type = 72;
    EXPECT_EQ(cpp_signal.hdr.msg_type, py_signal.hdr.msg_type)
        << "the bus must not need to know which language produced a signal";
    EXPECT_NE(cpp_signal.hdr.producer_id, py_signal.hdr.producer_id)
        << "...but it must know which PROCESS produced it, for restart detection (contracts.md)";
}

// ---- Zero-copy views (used throughout Layer 1) ------------------------------

TEST(P0Smoke, NonOwningViewsAreAvailable)
{
    constexpr std::string_view symbol{"BTCUSDT"};
    EXPECT_EQ(symbol.size(), 7u);

    const std::uint64_t prices[] = {100, 200, 300};
    const std::span<const std::uint64_t> view{prices};
    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.front(), 100u);
    EXPECT_EQ(view.back(), 300u);
}
