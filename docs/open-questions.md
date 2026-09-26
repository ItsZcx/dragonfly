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

**Resolved by:** designing the snapshot and replay procedure, which is P10.

## The `recon` channel is not recorded, so `recon_id` points nowhere in the log

The audit recorder covers `md`, `signals`, `orders`, `fills`, `risk_events`, and `ledger`. It does
not cover `recon`. An adoption message carries a `recon_id`, but the log has no pass with that id
and no record of what the venue claimed.

The audit can say "reconciliation caused this". It cannot say what the venue said.

**Resolved by:** the same P10 work as the entry above. If replay is correctness-critical for
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

## The per-venue position split is decided in principle, not specified

`decisions.md` records the decision that the ledger keeps a per-venue position split, fed from the
venue id on every execution report, so that a flatten can name the venue for each leg. The
requirement is stated in `layers.md`, L4 and L5. The shape is not designed.

What is undecided is how the split is stored and how it reconciles against the canonical position,
which stays the risk aggregate and must remain the single source of truth for exposure.

**Resolved by:** designing the ledger's position storage, which is P7.
