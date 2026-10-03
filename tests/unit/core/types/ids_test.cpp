// Unit tests for core/types/include/df/types/ids.hpp.

// Proves the rule in docs/contracts.md, "The message header" and the message
// bodies: a strategy_id is a uint32_t, a signal_id is a uint64_t, and every
// identity is its own type. A type that shares a width with another is not
// enough, because two uint32_t fields in the same struct can be swapped and the
// swap still compiles.

// Obligation: issue #2, "core/types: add ids.hpp with the strong ID types".

// The file checks four separate things.

//   1. Every identity is a distinct type. StrategyId and InstrumentId are both
//      uint32_t on the wire, so only a type check separates them.
//   2. No identity converts to or from a bare integer. An implicit conversion
//      would make the swap above legal again, one step removed.
//   3. The widths match docs/contracts.md field for field.
//   4. A StrategyId of 65536 or more is rejected. It occupies the top 16 bits of
//      signal_id, so a larger value truncates and collides with another
//      strategy's ids.

#include "gtest/gtest.h"
#include <cstdint>
#include <df/types/ids.hpp>
#include <limits>
#include <optional>
#include <type_traits>

namespace
{

// ---------------------------------------------------------------------------
// 1. Every identity is its own type.
//
// This is the reason the file exists. All the 32-bit ids below are
// interchangeable byte-for-byte, so nothing but the type system distinguishes
// them. is_same is the whole check; there is no runtime behaviour to observe.
// ---------------------------------------------------------------------------

TEST(CoreTypesIds, NoneOfTheIdsIsTheSameTypeAsAnother)
{
    EXPECT_FALSE((std::is_same_v<df::ProducerId, df::InstrumentId>));
    EXPECT_FALSE((std::is_same_v<df::ProducerId, df::StrategyId>));
    EXPECT_FALSE((std::is_same_v<df::InstrumentId, df::StrategyId>));
    EXPECT_FALSE((std::is_same_v<df::SignalId, df::StrategyId>));

    // The two order ids are the pair most easily confused. docs/contracts.md,
    // ExecutionReportMsg carries both, and they mean different things: one is
    // ours, one is the venue's.
    EXPECT_FALSE((std::is_same_v<df::ClientOrderId, df::ExchangeOrderId>));
    EXPECT_FALSE((std::is_same_v<df::ReconId, df::ClientOrderId>));
    EXPECT_FALSE((std::is_same_v<df::AdjustmentId, df::ReconId>));
}

// A 32-bit id is not a 64-bit id, even though both are integers. This is what
// stops a StrategyId reaching a signal_id field, where it would occupy the top
// half and leave the bottom half zero.
TEST(CoreTypesIds, TheNarrowIdsAreNotTheWideOnes)
{
    EXPECT_FALSE((std::is_same_v<df::StrategyId, df::SignalId>));
    EXPECT_FALSE((std::is_same_v<df::InstrumentId, df::ReconId>));
    EXPECT_FALSE((std::is_same_v<df::ProducerId, df::AdjustmentId>));
}

// ---------------------------------------------------------------------------
// 2. No implicit conversion, in either direction.
//
// docs/contracts.md gives several fields the same width on purpose. Without
// this property, `msg.strategy_id = msg.instrument_id` compiles, and so does a
// call with two same-typed arguments swapped. Both are silent wire bugs.
// ---------------------------------------------------------------------------

TEST(CoreTypesIds, NoIdConvertsToAnotherId)
{
    EXPECT_FALSE((std::is_convertible_v<df::StrategyId, df::InstrumentId>));
    EXPECT_FALSE((std::is_convertible_v<df::InstrumentId, df::StrategyId>));
    EXPECT_FALSE((std::is_convertible_v<df::StrategyId, df::ProducerId>));
    EXPECT_FALSE((std::is_convertible_v<df::StrategyId, df::SignalId>));
    EXPECT_FALSE((std::is_convertible_v<df::SignalId, df::StrategyId>));
    EXPECT_FALSE((std::is_convertible_v<df::ClientOrderId, df::ExchangeOrderId>));
    EXPECT_FALSE((std::is_convertible_v<df::ExchangeOrderId, df::ClientOrderId>));
    EXPECT_FALSE((std::is_convertible_v<df::ReconId, df::AdjustmentId>));
}

TEST(CoreTypesIds, NoIdConvertsToOrFromABareInteger)
{
    EXPECT_FALSE((std::is_convertible_v<df::StrategyId, std::uint32_t>));
    EXPECT_FALSE((std::is_convertible_v<std::uint32_t, df::StrategyId>));
    EXPECT_FALSE((std::is_convertible_v<df::InstrumentId, std::uint32_t>));
    EXPECT_FALSE((std::is_convertible_v<std::uint32_t, df::InstrumentId>));
    EXPECT_FALSE((std::is_convertible_v<df::SignalId, std::uint64_t>));
    EXPECT_FALSE((std::is_convertible_v<std::uint64_t, df::SignalId>));
    EXPECT_FALSE((std::is_convertible_v<df::ProducerId, int>));
    EXPECT_FALSE((std::is_convertible_v<int, df::ProducerId>));

    // The enum is the exception, and deliberately so: it is the shared
    // vocabulary, not an opaque handle. Its underlying type is what crosses the
    // wire, and docs/contracts.md, "Venue identity" fixes it at uint32_t.
    EXPECT_TRUE((std::is_same_v<std::underlying_type_t<df::VenueId>, std::uint32_t>));
}

// ---------------------------------------------------------------------------
// 3. The widths match docs/contracts.md, field for field.
//
// Each assertion names the message the field appears on, so a failure points at
// the layout that disagrees rather than at the id type alone.
// ---------------------------------------------------------------------------

TEST(CoreTypesIds, WidthsMatchTheDocumentedWireFields)
{
    // MsgHeader::producer_id      // 8..12
    EXPECT_EQ(sizeof(df::ProducerId), 4);
    // NewOrderMsg::strategy_id    // 48..52
    EXPECT_EQ(sizeof(df::StrategyId), 4);
    // NewOrderMsg::instrument_id  // 52..56
    EXPECT_EQ(sizeof(df::InstrumentId), 4);
    // AlphaSignalMsg::signal_id   // 32..40
    EXPECT_EQ(sizeof(df::SignalId), 8);
    // NewOrderMsg::client_order_id // 32..40
    EXPECT_EQ(sizeof(df::ClientOrderId), 8);
    // ExecutionReportMsg::exchange_order_id // 40..48
    EXPECT_EQ(sizeof(df::ExchangeOrderId), 8);
    // ReconRequestMsg::recon_id   // 32..40
    EXPECT_EQ(sizeof(df::ReconId), 8);
    // OrderAbandonedMsg::adjustment_id // 32..40
    EXPECT_EQ(sizeof(df::AdjustmentId), 8);
    // VenueId // the underlying type of the enum
    EXPECT_EQ(sizeof(df::VenueId), 4);
}

// The names that are load-bearing are asserted, and nothing more. Only Any has
// a value every layer must agree on, because the routing check needs one. The
// convenience aliases are deliberately not enumerated here: this enum is not
// the registry of venues, so a venue is added by config and taking the next
// free number, without touching this header or this test. Asserting a specific
// alias would make it look like part of the contract and invite a layer to
// switch on it, which is the coupling the config-declared id exists to prevent.
TEST(CoreTypesIds, OnlyAnyIsLoadBearing)
{ EXPECT_EQ(static_cast<std::uint32_t>(df::VenueId::Any), 0); }

// The widths are the wire widths, so the ids must be usable as plain bytes.
// docs/contracts.md requires every message to be trivially copyable and
// standard layout, and a message cannot be either if its fields are not.
TEST(CoreTypesIds, EveryIdIsTriviallyCopyableAndStandardLayout)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<df::ProducerId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::InstrumentId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::StrategyId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::SignalId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::ClientOrderId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::ExchangeOrderId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::ReconId>);
    EXPECT_TRUE(std::is_trivially_copyable_v<df::AdjustmentId>);

    EXPECT_TRUE(std::is_standard_layout_v<df::ProducerId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::InstrumentId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::StrategyId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::SignalId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::ClientOrderId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::ExchangeOrderId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::ReconId>);
    EXPECT_TRUE(std::is_standard_layout_v<df::AdjustmentId>);
}

// docs/contracts.md, "Every message is value-initialized before use": a message
// is built as `T m{}` and every byte must come out zero, padding included. That
// only holds for the fields if the ids themselves zero on value-initialization.
// This is why the members carry `{}` rather than a bare declaration.
TEST(CoreTypesIds, ValueInitializationZeroesTheId)
{
    const df::StrategyId kStrategy{};
    const df::SignalId kSignal{};
    const df::ClientOrderId kOrder{};

    EXPECT_EQ(kStrategy.value, 0);
    EXPECT_EQ(kSignal.value, 0);
    EXPECT_EQ(kOrder.value, 0);
}

// ---------------------------------------------------------------------------
// 4. The strategy_id ceiling.
//
// docs/operations.md, "Strategies: config/strategies.yaml": strategy_id must be
// unique across the fleet and less than 65536, because it occupies the top 16
// bits of signal_id. A larger value truncates and collides with another
// strategy's ids, so it is refused at construction rather than checked later.
// ---------------------------------------------------------------------------

TEST(CoreTypesIds, TheCeilingIsSixteenBits)
{
    EXPECT_EQ(df::kMaxStrategyId, 65'535);
    EXPECT_EQ(df::kMaxStrategyId, std::numeric_limits<std::uint16_t>::max());
}

TEST(CoreTypesIds, AStrategyIdAtTheCeilingIsAccepted)
{
    const std::optional<df::StrategyId> kLow = df::MakeStrategyId(0);
    const std::optional<df::StrategyId> kHigh = df::MakeStrategyId(df::kMaxStrategyId);

    // A plain if-guard rather than ASSERT_TRUE. clang-tidy's
    // bugprone-unchecked-optional-access follows the guard and accepts the
    // access below; gtest's ASSERT_TRUE expands to code it cannot see through.
    if (!kLow.has_value() || !kHigh.has_value())
    {
        FAIL() << "the factory rejected a value inside the ceiling";
    }

    EXPECT_EQ(kLow->value, 0);
    EXPECT_EQ(kHigh->value, 65'535);
}

// 65536 is the first value that would truncate, so it is the first rejection.
// The boundary is checked from both sides: 65535 is accepted above, 65536 is
// refused here. An off-by-one in the comparison would break exactly one of the
// two.
TEST(CoreTypesIds, AStrategyIdAboveTheCeilingIsRejected)
{ EXPECT_FALSE(df::MakeStrategyId(65'536).has_value()); }

TEST(CoreTypesIds, TheWholeRejectedRangeIsRejected)
{
    EXPECT_FALSE(df::MakeStrategyId(65'537).has_value());
    EXPECT_FALSE(df::MakeStrategyId(100'000).has_value());
    EXPECT_FALSE(df::MakeStrategyId(1'000'000).has_value());
    EXPECT_FALSE(df::MakeStrategyId(std::numeric_limits<std::uint32_t>::max()).has_value());
}

// The point of the truncation the ceiling prevents, stated as an example. A
// value of 65536 has the low 16 bits zero, so packing it into the top half of a
// signal_id would collide with strategy 0's ids from the reader's point of
// view. The factory refuses it for that reason.
TEST(CoreTypesIds, TheRejectedValueWouldHaveTruncatedToZero)
{
    constexpr std::uint32_t kRejected = 65'536;
    constexpr std::uint64_t kPacked = static_cast<std::uint64_t>(kRejected) << 48;

    // The low 16 bits of the packed result are the strategy's slot, and they are
    // indistinguishable from strategy 0.
    EXPECT_EQ(kPacked & 0xFFFF, 0);
    EXPECT_FALSE(df::MakeStrategyId(kRejected).has_value());
}

// The accepted values are the ones that survive the pack. This is the round
// trip the ceiling exists to guarantee: anything MakeStrategyId accepts can be
// placed in the top 16 bits of a signal_id and read back unchanged.
TEST(CoreTypesIds, EveryAcceptedIdSurvivesThePackIntoASignalId)
{
    for (std::uint32_t strategy = 0; strategy <= df::kMaxStrategyId; strategy += 997)
    {
        const std::optional<df::StrategyId> kId = df::MakeStrategyId(strategy);
        if (!kId.has_value())
        {
            FAIL() << "the factory rejected a value inside the ceiling: " << strategy;
        }

        const std::uint64_t kPacked = static_cast<std::uint64_t>(kId->value) << 48;
        const auto kReadBack = static_cast<std::uint32_t>(kPacked >> 48);
        EXPECT_EQ(kReadBack, strategy) << "the accepted id did not survive the pack";
    }

    // The last value inside the ceiling, so the loop's stride cannot skip it.
    const std::optional<df::StrategyId> kLast = df::MakeStrategyId(df::kMaxStrategyId);
    if (!kLast.has_value())
    {
        FAIL() << "the factory rejected the ceiling value";
    }

    const std::uint64_t kPackedLast = static_cast<std::uint64_t>(kLast->value) << 48;
    EXPECT_EQ(static_cast<std::uint32_t>(kPackedLast >> 48), df::kMaxStrategyId);
}

}  // namespace
