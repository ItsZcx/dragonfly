#pragma once

#include <cstdint>

namespace df::msg
{

enum class Side : std::uint8_t
{
    Buy  = 0,
    Sell = 1,
};

enum class OrderType : std::uint8_t
{
    Limit  = 0,
    Market = 1,
};

enum class OrderStatus : std::uint8_t
{
    New       = 0,
    Partial   = 1,
    Filled    = 2,
    Cancelled = 3,
    Rejected  = 4,
};

enum class TimeInForce : std::uint8_t
{
    Unknown = 0,
    GTC     = 1,
    IOC     = 2,
    FOK     = 3,
};

enum class ProcessState : std::uint8_t
{
    Boot      = 0,
    Ready     = 1,
    Operating = 2,
    Halted    = 3,
    Error     = 4,
};

enum class KillCommand : std::uint8_t
{
    Halt      = 0,
    CancelAll = 1,
    Flatten   = 2,
    Resume    = 3,
};

// Reconciliation
enum class ReconOutcome : std::uint8_t
{
    Unknown      = 0,
    Matched      = 1,
    Mismatch     = 2,
    Inconclusive = 3,
};

enum class ReconMismatch : std::uint8_t
{
    None              = 0,
    PositionLocalOnly = 1,
    PositionVenueOnly = 2,
    OrderLocalOnly    = 3,
    OrderVenueOnly    = 4,
    BasisDelta        = 5,
    FillCountDelta    = 6,
};

enum class ReconStatus : std::uint8_t
{
    Unknown          = 0,
    Ok               = 1,
    RateLimited      = 2,
    VenueUnavailable = 3,
};

enum class ReconTrigger : std::uint8_t
{
    Startup  = 0,
    Operator = 1,
};

enum class RiskRejectReason : std::uint16_t
{
    None = 0,

    KillSwitchActive      = 1,
    TimeInForceUnknown    = 2,
    RateLimitExceeded     = 3,
    InstrumentHalted      = 4,
    MarketOrderNotAllowed = 5,
    OrderSizeTooLarge     = 6,
    PriceCollarViolation  = 7,
    MaxOpenOrdersExceeded = 8,
    PositionLimitExceeded = 9,
    DailyLossExceeded     = 10,
};

}  // namespace df::msg
