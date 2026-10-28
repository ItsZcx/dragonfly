# Open questions

**Audience:** anyone deciding what to build next.

These are decisions that are not made yet. Each one names what would resolve it. When an entry is
resolved, its decision moves to `decisions.md` and the entry is deleted from this file.

This file is expected to be short. If it grows, the design is drifting.

---

## Do ledger adjustments need to be replayed, or only recorded?

The three ledger-adjustment messages record state changes that have no event behind them: the
abandonment sweep, and the corrections reconciliation applies. The system records them so replay
can reproduce those changes.

Whether replay *needs* them depends on how snapshots and replay interleave. If a snapshot taken
after an adjustment already contains the adjusted state, then replaying the adjustment is
redundant for that case, and the messages matter for auditability rather than for correctness.

This decides whether the three messages are load-bearing for recovery or only for the audit log.

**Resolved by:** designing the snapshot and replay procedure, which is P12.

## The `recon` channel is not recorded, so `recon_id` points nowhere in the log

The audit recorder covers `md`, `signals`, `orders`, `fills`, `risk_events`, and `ledger`. It does
not cover `recon`. An adoption message carries a `recon_id`, but the log has no pass with that id
and no record of what the venue claimed.

The audit can say "reconciliation caused this". It cannot say what the venue said.

**Resolved by:** the same P12 work as the entry above. If replay is correctness-critical for
adjustments, the log likely needs the venue's claim as well. Recording `recon` is one way. Folding
the venue's numbers into the adoption message is another.

## The budget message from L4 to L3 does not exist

`layers.md` describes L4 (OMS) computing position and capital budgets that L3 (risk) enforces. No
message carries one.

The risk pipeline in `layers.md`, L3 checks canonical limits from configuration and its own state
mirror. That is what exists today. The dynamic budgets the architecture describes are a separate
mechanism that was never designed.

**Resolved by:** deciding whether dynamic budgets are needed at all. If the configured limits and
the mirror are sufficient, this entry is deleted rather than implemented.

## Execution reports do not echo the time in force

`TimeInForce` travels on `AlphaSignalMsg` and `NewOrderMsg`, not on `ExecutionReportMsg`. FIX puts
its equivalent, tag 59, on all three, so a consumer can see the lifetime the venue applied.

The three values we have leave nothing for a venue to reinterpret, so the field would go unused
today. It becomes load-bearing as soon as a venue expires an order, treats a missing value as its
own default, or substitutes a value of its own. The ledger would then learn the change only from a
status update, and a mismatch could not distinguish an order that expired as instructed from one the
venue cancelled.

`ExecutionReportMsg` has five bytes of padding after `liquidity`, so the field would fit without
changing its size.

**Resolved by:** the L5 (EMS) venue-capability work. If a venue with a richer time-in-force
vocabulary is added, the report needs the field. If the three values are permanent, this entry is
deleted and the contract keeps its assumption that the venue applies our instruction verbatim.

## Two gateways both claim an order with a wildcard venue id

`NewOrderMsg.venue_id` of `0` means any venue, and the gateway that receives the order claims it.
That is unambiguous while exactly one gateway subscribes to `orders`. With two, both receive the
same message and both would transmit it, and nothing says which one wins.

Nothing sets the venue id on an order today: a signal carries no venue, and L3 has none to copy.
So a two-venue deployment puts every order in this case.

**Resolved by:** deciding where venue selection lives. A routing policy in L4 or L5, choosing by
price or from configuration, keeps the signal venue-free. A venue field on the signal would make L3
venue-aware, which the layer boundaries exist to prevent.

## Link-time optimization is off, and nothing records whether it should be

No preset enables link-time optimization. That is not a decision anyone made: no file mentions it,
so a reader cannot tell a considered "no" from an omission.

Without it the compiler cannot see one translation unit from another, which is what makes a body in
a .cpp cost something on a hot path. The clock is the first case: `SimulationClock::ClockAdvanceTo`
lives in `core/clock/src/clock.cpp` and cannot be inlined into the replay loop it advances. The same
will hold for L3, L4, and L5.

What it would buy is a few percent on a program with a measured hot path, and nothing measurable
today, because there is no hot path and the live clock's own cost is a system call. What it costs is
link time on every build, harder stepping in the debug build, a poor fit with the two presets that
run the memory and thread checkers, and the risk that a duplicate definition, harmless when each
translation unit keeps its own copy, silently becomes one wrong choice.

**Resolved by:** the P15 latency measurement, which is where a hot path and a recorded baseline
exist to compare against. If it is enabled, it goes in the `release` preset only, on targets with
hot paths, behind `check_ipo_supported`. If it is not, the reasoning is written in `decisions.md`
and this entry is deleted, so the absence reads as a choice rather than an oversight.
