// Unit tests for contracts/include/df/msg/control.hpp.

// docs/decisions.md, "Layout and trait checks live in the contracts tests, not
// the headers". The offsets are the wire layout: a wrong value breaks every
// reader and every logged heartbeat or kill command.

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

static_assert(sizeof(HeartbeatMsg) == 48, "HeartbeatMsg is 48 bytes, see docs/contracts.md \"Control\"");
static_assert(0 == offsetof(HeartbeatMsg, hdr), "HeartbeatMsg.hdr is at offset 0");
static_assert(32 == offsetof(HeartbeatMsg, state), "HeartbeatMsg.state is at offset 32");
static_assert(36 == offsetof(HeartbeatMsg, ref_data_hash), "HeartbeatMsg.ref_data_hash is at offset 36");
static_assert(40 == offsetof(HeartbeatMsg, beat_ts_ns), "HeartbeatMsg.beat_ts_ns is at offset 40");
static_assert(std::is_trivially_copyable_v<HeartbeatMsg>,
              "HeartbeatMsg must be trivially copyable: the wire path copies it bytewise");
static_assert(std::is_standard_layout_v<HeartbeatMsg>,
              "HeartbeatMsg must be standard layout: offsetof and the shared wire layout depend on it");
static_assert(std::is_trivially_default_constructible_v<HeartbeatMsg>,
              "keep HeartbeatMsg an aggregate: T{} must zero padding (docs/contracts.md)");

static_assert(sizeof(KillSwitchMsg) == 48, "KillSwitchMsg is 48 bytes, see docs/contracts.md \"Control\"");
static_assert(0 == offsetof(KillSwitchMsg, hdr), "KillSwitchMsg.hdr is at offset 0");
static_assert(32 == offsetof(KillSwitchMsg, command), "KillSwitchMsg.command is at offset 32");
static_assert(40 == offsetof(KillSwitchMsg, issued_ts_ns), "KillSwitchMsg.issued_ts_ns is at offset 40");
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
