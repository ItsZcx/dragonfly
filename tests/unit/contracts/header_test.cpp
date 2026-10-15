// Unit tests for contracts/include/df/msg/header.hpp.

#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <df/msg/header.hpp>
#include <memory>
#include <type_traits>

namespace
{

using df::msg::MsgHeader;

static_assert(sizeof(MsgHeader) == 32, "MsgHeader is 32 bytes, see docs/contracts.md \"The message header\"");
static_assert(0 == offsetof(MsgHeader, msg_type), "MsgHeader.msg_type is at offset 0");
static_assert(4 == offsetof(MsgHeader, msg_len), "MsgHeader.msg_len is at offset 4");
static_assert(8 == offsetof(MsgHeader, producer_id), "MsgHeader.producer_id is at offset 8");
static_assert(12 == offsetof(MsgHeader, flags), "MsgHeader.flags is at offset 12");
static_assert(16 == offsetof(MsgHeader, ts_ns), "MsgHeader.ts_ns is at offset 16");
static_assert(24 == offsetof(MsgHeader, seq_num), "MsgHeader.seq_num is at offset 24");
static_assert(std::is_trivially_copyable_v<MsgHeader>,
              "MsgHeader must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<MsgHeader>,
              "MsgHeader must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<MsgHeader>,
              "keep MsgHeader an aggregate: T{} must zero padding (docs/contracts.md)");

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
