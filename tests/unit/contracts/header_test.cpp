// Unit tests for contracts/include/df/msg/header.hpp.

// docs/contracts.md, "Every message is value-initialized before use": a message
// is built as `T m{}`, which zeroes every byte including padding. That is what
// makes the audit log byte-reproducible and byte comparison sound.

// The layout, the field offsets, and the two copy traits are asserted in the
// header itself, so the compiler checks them on every build. This file covers
// the one guarantee an assertion cannot: the padding bytes really are zero
// after value-initialization.

// Obligation: issue #3, "contracts: add header.hpp with MsgHeader and MsgFlag".

#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <df/msg/header.hpp>
#include <memory>

namespace
{

using df::msg::MsgHeader;

TEST(MsgHeader, ValueInitZeroesPadding)
{
    // Poison the storage first, so a padding byte that value-initialization
    // fails to zero stays 0xAA and the test fails instead of passing by luck.
    alignas(MsgHeader) std::array<std::byte, sizeof(MsgHeader)> storage{};
    storage.fill(std::byte{0xAA});

    // Construct in place. The braces are load-bearing: `MsgHeader m;` would
    // leave the bytes indeterminate.
    MsgHeader* hdr = std::construct_at(reinterpret_cast<MsgHeader*>(storage.data()));

    EXPECT_EQ(hdr->msg_type, 0U);

    // Bytes 13, 14, and 15 are the padding between `flags` and `ts_ns`
    // (docs/contracts.md, "Field by field").
    EXPECT_EQ(static_cast<unsigned>(storage.at(13)), 0U);
    EXPECT_EQ(static_cast<unsigned>(storage.at(14)), 0U);
    EXPECT_EQ(static_cast<unsigned>(storage.at(15)), 0U);
}

}  // namespace
