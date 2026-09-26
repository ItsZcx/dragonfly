# Lifecycle: three stories

**Audience:** anyone who wants to see how the layers work together, after reading `overview.md`
and `layers.md`.

This document follows three things through the system: a market tick, an order, and a restart.
The three stories are the fastest way to check your understanding, because each one crosses every
layer and shows where the handoffs are.

## Contents

- [A market tick, end to end](#a-market-tick-end-to-end)
- [One order, from the venue's point of view](#one-order-from-the-venues-point-of-view)
- [Restart and recovery](#restart-and-recovery)

---

## A market tick, end to end

This is the shortest complete description of the system running.

1. A venue sends a trade over its socket.
2. **L1 (ingestion)** parses it, maps the venue's symbol to a canonical instrument id, and records
   two timestamps: the venue's own, and its own monotonic receive time. It publishes a `TradeMsg`
   on `md`.
3. **L2 (strategy)** reads the trade, updates its state, decides, and publishes an
   `AlphaSignalMsg` on `signals`. The signal says something like "buy 0.5 of instrument 1001". It
   names no venue, because no order exists yet and venue selection is an execution decision.
4. **L3 (risk)** reads the signal and runs the fixed sequence of checks. If every check passes, it
   publishes a `NewOrderMsg` on `orders`. If one fails, it publishes a `RiskRejectMsg` on
   `risk_events`, and the signal stops there.
5. Two layers read `orders` at the same time. **L4 (OMS)** records the order and reserves cash
   against it. **L5 (EMS)** takes the same order, snaps its price and quantity to the venue's
   grid, and transmits it. Neither waits for the other.
6. The venue acknowledges and then fills. **L5** translates each outcome into an
   `ExecutionReportMsg` on `fills`.
7. **L4** applies each fill. It moves cash, updates the position, and computes realised P&L.
   **L3** applies the same fill to its own position mirror, so the next check sees the new
   position.
8. **L6 (observability)** displays it.

Each layer adds one thing and hands off. The signal is a proposal, the order is a commitment, and
the execution report is the fact.

Two details in that walkthrough are easy to miss and are worth naming.

The order reaches L4 and L5 in parallel, not in sequence. L4 books what it *intends* to do, and
L5 finds out what *actually* happened. That split is the whole reason the OMS and the EMS are
separate layers.

An `ExecutionReportMsg` is the only message that changes positions or cash. Everything before it
adjusts reservations, and everything after it is derived from it.

---

## One order, from the venue's point of view

This story follows a single order through its lifecycle. It shows why the ledger trusts the venue
over its own request.

1. **L5 (EMS) sends the order.** L4 (OMS) has already recorded it as `pending new`, with cash
   reserved against it.
2. **The venue acknowledges.** It accepts the order and returns its own order id. L5 publishes an
   execution report with `status=new` and the venue's id. L4 records the ack and moves the order
   to `live`. L4 can now map its own id to the venue's.
3. **The venue partially fills.** It reports "0.2 filled at $100,040, fee $2". L5 publishes a
   report with `filled_qty=0.2` and `cumulative_qty=0.2`. L4 applies it, deducts cash, adds the
   fee, updates the position, and moves the order to `partially filled`. L4's record now says
   `filled_qty = 0.2`, and the next report's `cumulative_qty` is compared against that.
4. **A cancel is sent.** Perhaps the strategy's condition changed. L4 sends a `CancelOrderMsg`.
   L4 does **not** release the reservation yet, because a cancel is a request. The order moves to
   `pending cancel`.
5. **The venue confirms.** It reports "cancelled, 0.2 filled in total". L5 publishes a terminal
   report with `status=cancelled`. L4 marks the order `cancelled` and releases the reservation for
   the unfilled 0.3.

If the venue had filled the rest instead, the terminal report would say `filled`, and L4 would
settle it. Either way, **the venue's report resolves the order, and the local send does not.**

Two properties make this robust.

A cancel is a request, so cash stays held until the venue confirms. "I cancelled" and "the venue
says it filled" can never both be true in the ledger, because the venue's report is what resolves
the order.

Every outcome produces a report. An ack, a partial fill, a fill, a cancel, and a reject all arrive
as messages. No outcome leaves L4 waiting for something that will never come.

---

## Restart and recovery

This story connects the recorder, snapshots, reconciliation, and the trading gate.

1. **A process crashes.** Take the OMS as the example. Its in-memory state is gone.
2. **The supervisor restarts it.**
3. **It loads the last snapshot.** L4 writes its full state to disk periodically: positions,
   cash, open orders, and sequence watermarks. It reads the most recent one. The state is
   plausible but may be minutes old.
4. **It replays the log tail.** The audit recorder has written every event to a binary log. L4
   replays the events after the snapshot's point, which rebuilds the state up to the crash. The
   replayed events include ledger adjustments, the recorded changes that have no event behind
   them.
5. **It now believes something.** It has positions, orders, and cash. But things may have happened
   between the crash and now. A fill may have arrived while it was down, the venue may have done
   something, or the log may have a hole. It must not trade on this belief.
6. **It reconciles.** L4 publishes a `ReconRequestMsg` on `recon`. L5 (EMS) asks the venue what it
   holds and replies with a `ReconResultMsg` per instrument. That message carries the venue's
   claim only. L5 does not compare, because comparing is accounting and L5 does not do accounting.
7. **It compares.** L4 checks the venue's claim against its own books and publishes a
   `ReconVerdictMsg` per instrument. Each verdict is matched, or a specific class of disagreement.
8. **The gate decides.** L3 (risk) started with `trading_enabled = false`, so every signal is
   rejected until the pass completes. If **every instrument matched**, L6 clears the gate and
   trading resumes automatically. If **anything mismatched or was inconclusive**, the gate stays
   closed. A human resolves it and then triggers a fresh pass to prove the fix landed.
9. **Trading resumes.** Only now does the next strategy signal get through.

Three ideas hold this story together.

**State is rebuilt from events, not trusted from memory.** That is why the recorder must be
complete, and why the two off-stream mutations are recorded as ledger adjustments. Without the
adjustments, replay would produce a different ledger than what actually happened, and the
difference would be silent.

**The venue is authoritative.** If L4 thinks it holds 0.5 and the venue says 0.3, L4 adopts 0.3
and records the adjustment.

**The system fails closed.** No answer is not the same as no disagreement. An instrument counts as
matched only if an explicit `ok` result arrived and the comparison agreed, so a rate-limited or
unavailable venue leaves the gate closed. The system starts unable to trade, and it proves itself
before it can.

One gap is worth knowing. If a restart is interrupted by an incomplete log, such as a hole or a
truncated file, the honest result is `Inconclusive`, which sends it to a human. The system does
not guess. That is what the recorder's explicit gap markers are for.
