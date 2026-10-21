// Unit tests for contracts/include/df/msg/control.hpp.

#include <cstddef>
#include <df/msg/common.hpp>
#include <df/msg/control.hpp>
#include <type_traits>

namespace
{

using df::msg::HeartbeatMsg;
using df::msg::KillCommand;
using df::msg::KillSwitchMsg;
using df::msg::ProcessState;

static_assert(sizeof(HeartbeatMsg) == 40, "HeartbeatMsg is 40 bytes, see docs/contracts.md \"Control\"");
static_assert(0 == offsetof(HeartbeatMsg, hdr), "HeartbeatMsg.hdr is at offset 0");
static_assert(32 == offsetof(HeartbeatMsg, state), "HeartbeatMsg.state is at offset 32");
static_assert(36 == offsetof(HeartbeatMsg, ref_data_hash), "HeartbeatMsg.ref_data_hash is at offset 36");
static_assert(std::is_trivially_copyable_v<HeartbeatMsg>,
              "HeartbeatMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<HeartbeatMsg>,
              "HeartbeatMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<HeartbeatMsg>,
              "keep HeartbeatMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(KillSwitchMsg) == 40, "KillSwitchMsg is 40 bytes, see docs/contracts.md \"Control\"");
static_assert(0 == offsetof(KillSwitchMsg, hdr), "KillSwitchMsg.hdr is at offset 0");
static_assert(32 == offsetof(KillSwitchMsg, command), "KillSwitchMsg.command is at offset 32");
static_assert(std::is_trivially_copyable_v<KillSwitchMsg>,
              "KillSwitchMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<KillSwitchMsg>,
              "KillSwitchMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<KillSwitchMsg>,
              "keep KillSwitchMsg an aggregate: T{} must zero padding (docs/contracts.md)");

// Each enum must fit the field that carries it. The header stores the value in
// a fixed-width integer, so a widened enum would silently truncate on the wire.
static_assert(sizeof(ProcessState) == 1,
              "ProcessState must fit HeartbeatMsg.state (docs/contracts.md \"Shared vocabulary\")");
static_assert(sizeof(KillCommand) == 1,
              "KillCommand must fit KillSwitchMsg.command (docs/contracts.md \"Shared vocabulary\")");

}  // namespace
