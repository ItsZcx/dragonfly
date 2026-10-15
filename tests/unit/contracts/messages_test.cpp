// Runtime coverage for every message type in contracts/include/df/msg/.

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <df/msg/control.hpp>
#include <df/msg/execution.hpp>
#include <df/msg/header.hpp>
#include <df/msg/ledger.hpp>
#include <df/msg/market_data.hpp>
#include <df/msg/orders.hpp>
#include <df/msg/reconciliation.hpp>
#include <df/msg/signals.hpp>
#include <memory>
#include <type_traits>

namespace
{

using df::msg::AlphaSignalMsg;
using df::msg::BasisAdoptedMsg;
using df::msg::BookDeltaMsg;
using df::msg::BookSnapshotMsg;
using df::msg::BookSnapshotRequestMsg;
using df::msg::CancelOrderMsg;
using df::msg::ExecutionReportMsg;
using df::msg::HeartbeatMsg;
using df::msg::KillSwitchMsg;
using df::msg::MsgType;
using df::msg::NewOrderMsg;
using df::msg::OrderAbandonedMsg;
using df::msg::PositionAdoptedMsg;
using df::msg::QuoteMsg;
using df::msg::ReconRequestMsg;
using df::msg::ReconResultMsg;
using df::msg::ReconVerdictMsg;
using df::msg::ReplaceOrderMsg;
using df::msg::RiskRejectMsg;
using df::msg::TradeMsg;

template <typename T> void ExpectIdenticalFieldsAreByteIdentical()
{
    // The two instances are compared bytewise, so the type must be trivially
    // copyable. The per-header contracts tests own the full trait set for every
    // message; these two helpers assert only the trait their check relies on.
    static_assert(std::is_trivially_copyable_v<T>, "a bytewise comparison needs a trivially copyable type");

    T a{};
    T b{};
    for (T* m : {&a, &b})
    {
        m->hdr.msg_type    = 1;
        m->hdr.msg_len     = static_cast<std::uint32_t>(sizeof(T));
        m->hdr.producer_id = 7;
        m->hdr.flags       = 0;
        m->hdr.ts_ns       = 1234;
        m->hdr.seq_num     = 99;
    }
    const auto* pa = reinterpret_cast<const std::byte*>(&a);
    const auto* pb = reinterpret_cast<const std::byte*>(&b);
    EXPECT_TRUE(std::equal(pa, pa + sizeof(T), pb));
}

// Stronger: value-initialization zeroes every byte, padding included. Poison
// the storage first, so a byte that value-initialization fails to zero stays
// 0xAA. This follows tests/unit/contracts/header_test.cpp, which does the same
// for MsgHeader.
template <typename T> void ExpectValueInitZeroesEveryByte()
{
    // Value-initialization zeroes padding only when the default constructor is
    // trivial. A user-provided constructor runs instead and leaves the poisoned
    // bytes alone, so the trait is what makes this check meaningful.
    static_assert(std::is_trivially_default_constructible_v<T>,
                  "value-initialization zeroes padding only for a trivially default constructible type");

    // alginas needed because its a template func
    alignas(T) std::array<std::byte, sizeof(T)> storage{};
    storage.fill(std::byte{0xAA});

    std::construct_at(reinterpret_cast<T*>(storage.data()));

    for (std::byte b : storage)
    {
        EXPECT_EQ(b, std::byte{0});
    }
}

// Every message type gets both checks. Dispatch on MsgType, with a case for
// every enumerator and no default: -Wall includes -Wswitch, so a new MsgType
// enumerator is a build error until a case is added here.
void CheckMessageType(std::uint32_t code)
{
    switch (static_cast<MsgType>(code))
    {
        case MsgType::None: return;  // not a message
        case MsgType::Trade:
            ExpectIdenticalFieldsAreByteIdentical<TradeMsg>();
            ExpectValueInitZeroesEveryByte<TradeMsg>();
            return;
        case MsgType::Quote:
            ExpectIdenticalFieldsAreByteIdentical<QuoteMsg>();
            ExpectValueInitZeroesEveryByte<QuoteMsg>();
            return;
        case MsgType::BookDelta:
            ExpectIdenticalFieldsAreByteIdentical<BookDeltaMsg>();
            ExpectValueInitZeroesEveryByte<BookDeltaMsg>();
            return;
        case MsgType::BookSnapshot:
            ExpectIdenticalFieldsAreByteIdentical<BookSnapshotMsg>();
            ExpectValueInitZeroesEveryByte<BookSnapshotMsg>();
            return;
        case MsgType::AlphaSignal:
            ExpectIdenticalFieldsAreByteIdentical<AlphaSignalMsg>();
            ExpectValueInitZeroesEveryByte<AlphaSignalMsg>();
            return;
        case MsgType::RiskReject:
            ExpectIdenticalFieldsAreByteIdentical<RiskRejectMsg>();
            ExpectValueInitZeroesEveryByte<RiskRejectMsg>();
            return;
        case MsgType::NewOrder:
            ExpectIdenticalFieldsAreByteIdentical<NewOrderMsg>();
            ExpectValueInitZeroesEveryByte<NewOrderMsg>();
            return;
        case MsgType::CancelOrder:
            ExpectIdenticalFieldsAreByteIdentical<CancelOrderMsg>();
            ExpectValueInitZeroesEveryByte<CancelOrderMsg>();
            return;
        case MsgType::ReplaceOrder:
            ExpectIdenticalFieldsAreByteIdentical<ReplaceOrderMsg>();
            ExpectValueInitZeroesEveryByte<ReplaceOrderMsg>();
            return;
        case MsgType::ExecutionReport:
            ExpectIdenticalFieldsAreByteIdentical<ExecutionReportMsg>();
            ExpectValueInitZeroesEveryByte<ExecutionReportMsg>();
            return;
        case MsgType::Heartbeat:
            ExpectIdenticalFieldsAreByteIdentical<HeartbeatMsg>();
            ExpectValueInitZeroesEveryByte<HeartbeatMsg>();
            return;
        case MsgType::KillSwitch:
            ExpectIdenticalFieldsAreByteIdentical<KillSwitchMsg>();
            ExpectValueInitZeroesEveryByte<KillSwitchMsg>();
            return;
        case MsgType::BookSnapshotRequest:
            ExpectIdenticalFieldsAreByteIdentical<BookSnapshotRequestMsg>();
            ExpectValueInitZeroesEveryByte<BookSnapshotRequestMsg>();
            return;
        case MsgType::ReconRequest:
            ExpectIdenticalFieldsAreByteIdentical<ReconRequestMsg>();
            ExpectValueInitZeroesEveryByte<ReconRequestMsg>();
            return;
        case MsgType::ReconResult:
            ExpectIdenticalFieldsAreByteIdentical<ReconResultMsg>();
            ExpectValueInitZeroesEveryByte<ReconResultMsg>();
            return;
        case MsgType::ReconVerdict:
            ExpectIdenticalFieldsAreByteIdentical<ReconVerdictMsg>();
            ExpectValueInitZeroesEveryByte<ReconVerdictMsg>();
            return;
        case MsgType::OrderAbandoned:
            ExpectIdenticalFieldsAreByteIdentical<OrderAbandonedMsg>();
            ExpectValueInitZeroesEveryByte<OrderAbandonedMsg>();
            return;
        case MsgType::PositionAdopted:
            ExpectIdenticalFieldsAreByteIdentical<PositionAdoptedMsg>();
            ExpectValueInitZeroesEveryByte<PositionAdoptedMsg>();
            return;
        case MsgType::BasisAdopted:
            ExpectIdenticalFieldsAreByteIdentical<BasisAdoptedMsg>();
            ExpectValueInitZeroesEveryByte<BasisAdoptedMsg>();
            return;
    }
}

TEST(Contracts, EveryMessageValueInitializesToZeroBytes)
{
    // The codes are permanent and all below 100. Scan a wide range so every
    // real code reaches its case. -Wswitch still fails the build if a case is
    // missing.
    for (std::uint32_t code = 0; code < 1024; ++code)
    {
        CheckMessageType(code);
    }
}

}  // namespace
