#pragma once

#include "df/msg/header.hpp"

#include <cstdint>

namespace df::msg
{

struct HeartbeatMsg
{
    MsgHeader     hdr;
    std::uint8_t  state;  // ProcessState
    std::uint32_t ref_data_hash;
};

struct KillSwitchMsg
{
    MsgHeader    hdr;
    std::uint8_t command;  // KillCommand
};

}  // namespace df::msg
