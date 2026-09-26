# Layers

**Audience:** anyone implementing or debugging a layer.

This document describes the six layers. Every layer uses the same subsection order, so the
sections can be compared: what it is, what it consumes and produces, how it works, what it must
not do, how it fails, and what is core versus added.

Message layouts are in `contracts.md`. The end-to-end flows are in `lifecycle.md`. A reminder on
naming: `L1` to `L6` are roles, not processes. `overview.md` describes how many processes
implement them.

<details>
<summary>Contents</summary>

- [L1 (ingestion)](#l1-ingestion)
- [L2 (strategy)](#l2-strategy)
- [L3 (risk)](#l3-risk)
- [L4 (OMS) — the ledger](#l4-oms--the-ledger)
- [L5 (EMS) — execution](#l5-ems--execution)
- [L6 (observability)](#l6-observability)

</details>

---

## L1 (ingestion)

### What it is

L1 is the boundary between the venue's world and ours. Everything downstream sees canonical
messages. L1 is the only layer that speaks a venue's language.

It turns venue-specific data into canonical messages and maintains a local order book.

### Consumes and produces

- **Consumes:** the venue's data feed, which is a socket in production and a historical log in
  backtest. It also consumes `control` for snapshot requests.
- **Produces:** `TradeMsg`, `QuoteMsg`, `BookDeltaMsg`, and `BookSnapshotMsg` on `md`.

L1 has two ports. The outbound port publishes market data. The inbound port receives the resync
request on `control`. The inbound port is easy to miss, because L1 is the only hot-path process
that consumes rather than publishes. It exists for one reason: a consumer that detects a gap must
be able to ask L1 for a fresh book.

### How it works

For each incoming event, L1 does four things.

1. **Parse** the venue's representation.
2. **Resolve the symbol to a canonical id.** The venue's string becomes an integer, once, here.
   Everything downstream carries only the integer.
3. **Timestamp it twice.** The venue's own timestamp is preserved verbatim, and the monotonic
   receive time goes into the header. Neither overwrites the other.
4. **Publish** a fixed-size canonical message.

Alongside that, L1 maintains a **local order book** per instrument.

#### The book: snapshot, deltas, sequence, resync

This is one mechanism, not four features, and it is the most important thing in L1.

A full book cannot be sent on every update, so a consumer is seeded with a full snapshot and then
applies incremental deltas. That works only if no delta is missed. A single lost delta leaves the
rebuilt book wrong at one price level, with no error, and the consumer then trades on a book that
does not exist.

The mechanism has four parts.

- **Deltas** carry the changes.
- **A snapshot** seeds a consumer's book.
- **Sequence numbers** make a miss detectable. A jump means a delta was lost.
- **Resync** is the response: discard the local book and request a fresh snapshot, rather than
  guessing at the missing update.

The snapshot's own sequence number anchors the join. Deltas older than the snapshot are stale and
ignored. That anchoring makes the rebuild deterministic even if a snapshot arrives twice or a
stream is replayed.

**The request path is the one backwards edge in the system.** A consumer asks L1 for a snapshot on
`control`. L1 answers by publishing an ordinary `BookSnapshotMsg` on `md`, which every consumer
sees. The request is serviced on an **off-path thread**, not the ingest thread. Taking a lock or
touching the shared book on the ingest thread is forbidden, and building a snapshot is real work.
Requests are coalesced, so one publish satisfies every outstanding request for that instrument. A
gap storm therefore cannot become a snapshot storm.

### What it must not do

- L1 never decides anything. It publishes observations, not opinions.
- L1 never carries venue-specific information downstream. If a venue symbol string leaked past L1,
  the venue-agnostic guarantee would be broken.
- L1 never applies tick sizes or lot sizes. Those are venue micro-structure and belong to L5
  (EMS), which is where the venue is known.
- L1 never blocks the ingest thread on the request path.

### How it fails

| Failure                                 | Response                                                                                                         |
| --------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Socket drops                            | reconnect in-process with backoff. Not a process restart, which would discard state that the reconnect preserves |
| Process dies                            | supervised restart. The book is resynced from a fresh snapshot                                                   |
| A delta is missed                       | sequence-gap detection triggers a request for a fresh snapshot                                                   |
| Credentials rejected                    | abort at boot instead of retrying forever                                                                        |
| Config edited and one process restarted | the config hash on heartbeats shows the skew, and the gate closes                                                |

Almost all of L1's complexity is about failure. The happy path is to parse and publish.

### Core versus added

**Core:** connect, parse, resolve the symbol, timestamp, publish, maintain the local book, and
the snapshot, delta, sequence, and resync mechanism.

**Added:** the request path and `BookSnapshotRequestMsg`. The text once said a gap "triggers a
resync" without saying how a consumer's request reached the handler that must answer it. The
mechanism did not exist, and L1 had no inbound port at all. Both were specification gaps rather
than design choices.

---

## L2 (strategy)

### What it is

L2 is the layer that wants something. Every other layer transports, judges, records, or executes.
L2 is the only one that decides.

It turns market observations into trading intentions.

### Consumes and produces

- **Consumes:** market data on `md`, and risk rejections on `risk_events` for information only.
- **Produces:** `AlphaSignalMsg` on `signals`.

L2 does not consume its own fills in the decision loop. A strategy whose correctness depends on
its own fill status is usually doing risk management, which belongs in L3 (risk) or L4 (OMS).

### How it works

L2 reads market data, updates pre-allocated state such as rolling windows, indicators, or model
state, decides, and publishes a signal.

A signal is a proposal. It names a canonical instrument, a side, a quantity, and a price. Nothing
has been approved. It carries no venue and no order id, because no order exists yet.

#### The two strategy families

The distinction is which feeds a strategy subscribes to.

**Single-venue strategies are the common case.** The process is wired by configuration to one
venue's feed and one venue's gateway. To that strategy, its venue is the whole universe. It emits
"buy 0.5 of 1001" and never names a venue. The same compiled strategy runs against a different
venue by changing which configuration it is started with.

**Cross-venue strategies are arbitrage and smart routing.** These are explicitly not
venue-agnostic, and that is correct, because the trade *is* the relationship between two venues.
Such a strategy subscribes to several feed handlers at once and keeps one book per venue, because
it must compare them. It emits one order per leg, and each leg names its destination explicitly.
All feed handlers publish on the same `md` channel, and `producer_id` distinguishes them, so no
per-venue channel naming is needed.

Venue-agnosticism describes the algorithm: no venue name, no exchange format, no tick size in its
math. It does not claim that all venues are interchangeable. Venues quote genuinely different
prices for the same asset at the same instant, and that spread is what a cross-venue strategy
trades.

#### How a strategy is registered and configured

- **A compiled-in factory keyed by a string.** A configuration entry's `impl` field must match a
  registered implementation. There is no dynamic loading. A strategy is a hot-path process peer,
  and loading a shared object allocates and runs arbitrary initializers mid-process, which is
  forbidden on a hot thread and makes "which code produced this log" unanswerable.
- **Validation happens at startup, and failure is loud.** An `impl` that is not registered is a
  startup failure, not a warning. Skipping it would produce a deployment that heartbeats, passes
  reconciliation, and silently trades nothing, which is the hardest failure to diagnose.
- **Parameters are validated against the implementation's own schema before construction.** This
  turns a mistyped parameter from a silently defaulted value into a startup error. A strategy
  running on defaults produces plausible signals that were never backtested, which is worse than
  failing.
- **`enabled` is separate from `impl`.** `enabled` is a deployment switch that can be flipped
  without removing the entry, and therefore without losing its attribution history. `impl` is a
  build-time binding. A disabled strategy is still validated, so configuration errors surface at
  deploy time rather than when the flag is eventually flipped.

**Two identifiers, deliberately independent.** `strategy_id` identifies the logical strategy and
is the risk-attribution key. Per-strategy limits and P&L hang off it. `producer_id` identifies the
emitting process. One process can host several strategies under its `producer_id`, and a strategy
can move between a shared process and its own isolated process as a pure configuration change,
with no change to how its signals are attributed.

#### Where Python runs

A Python strategy runs in its **own OS process** and talks over the bus. The interpreter is never
embedded in a hot-path process.

The reason is not packaging. Embedding an interpreter would put the global interpreter lock,
interpreter initialisation and finalisation, and garbage collection inside the address space of a
process that must never block. It would also make a Python exception or fault a failure of the
strategy loop. As a separate process, Python gets an independent crash domain, a measurable
latency cost of one bus hop, and no effect on the C++ process at all.

What crosses the boundary: the Python process is an ordinary bus peer. It subscribes to `md`,
computes, and publishes `AlphaSignalMsg` on `signals`. It uses a binding library generated from
the contracts as its codec, so the layouts have a single source of truth. The cost is one bus hop
on the signal path, not the market-data path, so it does not enter the ingestion-to-risk latency
budget.

A Python strategy is therefore not special at the bus level. It is a peer with its own
`producer_id`, listed in configuration like any other.

### What it must not do

- L2 never manages risk or capital. It proposes, and L3 disposes. A strategy may be wrong about
  opportunity. It may never be wrong about state.
- L2 never names a venue, except the cross-venue family, which accepts venue-awareness explicitly.
- L2 never allocates, blocks, or touches disk on its hot thread.

### How it fails

| Failure                     | Response                                                                                                                               |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Produces invalid signals    | L3 rejects them. That is what the firewall is for                                                                                      |
| `signals` buffer full       | drop and count, surfaced as a diagnostic. Losing a signal costs a trade. Blocking the signal thread costs every trade, including exits |
| Process crashes             | supervised restart. There is no durable state to recover, which is deliberate                                                          |
| Falls behind on market data | sequence detection tells it, and it resyncs its book. Its signals simply arrive later                                                  |
| Bad configuration           | startup failure                                                                                                                        |

A strategy has no write-ahead log. That is deliberate. Its state is reconstructible from the
market-data stream, so durable storage would add machinery for a case that does not need it. A
restart is normal traffic.

### Core versus added

**Core:** the strategy interface, the signal message, the read-decide-publish loop, and the
configuration model.

**Added:** the registration mechanism, because a configuration entry once pointed at nothing and
no section described how it became an object, and the separate-process decision for Python,
because earlier text described both a plugin and a separate process, which cannot both be true.

**Explicitly absent:** the alpha. What makes a strategy profitable is not specified anywhere. The
system provides the machinery, and the strategy provides the idea.

---

## L3 (risk)

### What it is

L3 is the firewall, and the only layer that can say no.

It turns signals into approved orders, or into rejections with a reason.

### Consumes and produces

- **Consumes:** `signals`, market data on `md` because the price collar needs a reference price,
  and `fills` to keep its position mirror current.
- **Produces:** `NewOrderMsg` on `orders` for approved signals, and `RiskRejectMsg` on
  `risk_events` for rejected ones.

### How it works

#### The check pipeline

Every signal runs the same fixed sequence. **First failure wins, and no check is skipped.**

| #   | Check                          | Source of truth                                  | Reason code                 |
| --- | ------------------------------ | ------------------------------------------------ | --------------------------- |
| 0   | Kill switch tripped?           | an in-memory latch                               | `KillSwitchActive` (1)      |
| 1   | Rate limit exceeded?           | a token bucket refilled from the monotonic clock | `RateLimitExceeded` (2)     |
| 2   | Instrument suspended?          | a canonical suspension set                       | `InstrumentHalted` (3)      |
| 3   | Market orders permitted here?  | the limit table                                  | `MarketOrderNotAllowed` (4) |
| 4   | Order size within limit?       | the limit table                                  | `OrderSizeTooLarge` (5)     |
| 5   | Price within the collar?       | the last quote, through `OnQuote`                | `PriceCollarViolation` (6)  |
| 6   | Open-order count within limit? | the state mirror                                 | `MaxOpenOrdersExceeded` (7) |
| 7   | Position within limit?         | the state mirror, by canonical id                | `PositionLimitExceeded` (8) |
| 8   | Daily loss breached?           | realised plus unrealised P&L, net of fees        | `DailyLossExceeded` (9)     |

The reason code is offset by one from the step number, because 0 is reserved as a "no reason"
sentinel. A zero-initialized rejection can therefore never be read as a real verdict.

**Why this order.** The sequence is a deliberate sort from cheapest and most decisive to most
expensive and least decisive.

- Check 0 is one flag load. When the breaker is tripped, every later check is wasted work.
- Checks 1 to 4 read fixed memory, such as a bucket or a limit table, with no dependent loads.
  They reject the malformed and the abusive before the engine touches its own mutable state.
- Check 5 depends on market data having arrived. It is placed after size deliberately, so a
  badly sized order is reported as a size error, which is the caller's own bug, rather than as a
  collar violation, which is the market's condition.
- Checks 6 to 8 read the state mirror and are the expensive part. The position limit needs a
  post-trade position, and the daily-loss check needs P&L marked to the last mid.

The ordering is also a **security property**, not only a latency one. Because the state-touching
checks run last, a flood of malformed signals is rejected by the stateless checks and never
reaches the expensive ones.

**What the ordering must not depend on.** No check may read a value the sender supplied that has
not been validated. In particular, the collar compares against the mid recorded from L3's own
market-data subscription, never against the limit price in the signal under test.

#### Canonical-only

L3 checks canonical quantities and nothing else: order size against a canonical limit, position
against a canonical cap aggregated across venues, the collar against the mid, rate, suspension,
and P&L.

L3 does **not** check tick size, lot size, or minimum notional. Those are properties of a venue
listing, and a signal carries no venue, so in a multi-venue deployment a check against "the
venue's tick size" would have nothing to look up. L5 (EMS) enforces them, because L5 knows its
venue. This is what keeps the promise that adding a venue does not change the risk engine.

#### The state mirror

L3 needs the current position, the open order count, and the daily P&L. That state belongs to L4
(OMS). L3 keeps its own **mirror**, updated from the same `fills` stream that updates the ledger,
rather than querying L4. A synchronous cross-layer call would be a latency problem and a
coupling.

The trade-off is real. Two components derive positions and P&L from the same stream, so the
mirror can drift from the authoritative ledger. That is accepted, because reconciliation catches
real divergence at restart, and in normal operation both see the same fills. It is a performance
choice, not an accident.

#### Limits and tiers

Limits come from configuration and live at three tiers: `global`, `per_instrument`, and
`per_strategy`.

- Where two tiers apply, **both must pass**. A per-strategy cap cannot override a tighter
  instrument cap, and the reverse is also true. Each is an independent constraint.
- **An absent key is not a limit of zero.** Absent means "no constraint at this tier" and falls
  through. Zero means "disabled" at the tier where it is written.
- **Zero means disabled**, except for the kill switch, which is a latch rather than a numeric
  limit.
- The **daily-loss breaker is per-strategy**, because `strategy_id` is the attribution key and two
  strategies sharing an id would merge their P&L into one figure.

#### The reconciliation gate

L3 owns a `trading_enabled` flag. After a restart it starts **false**, and every signal is rejected
as `KillSwitchActive` until a reconciliation pass is fully matched. This is how "do not trade on
unverified state" is enforced mechanically rather than advised. It reuses the kill-switch
rejection path, so there is exactly one stop-trading state.

### What it must not do

- L3 never touches a venue and never reads venue micro-structure.
- L3 never performs accounting. It holds a mirror, but L4 owns the ledger, and L3 never decides
  what a fill means.
- L3 never blocks, allocates, or touches disk on the firewall thread.
- L3 never derives a reference price from the order under test.

### How it fails

| Failure                       | Response                                                                        |
| ----------------------------- | ------------------------------------------------------------------------------- |
| Malformed or absurd signal    | rejected by the cheap checks                                                    |
| Runaway strategy              | per-strategy rate limit, then per-strategy daily loss                           |
| Position blowout              | position limit, aggregated, and the daily-loss breaker                          |
| Mirror drifts from the ledger | accepted in normal operation. Reconciliation catches real divergence at restart |
| L3 itself down                | nothing is approved. The failure is closed by construction                      |
| Bad limit configuration       | startup failure                                                                 |

### Core versus added

**Core:** the pipeline, its ordering as a contract, the reason codes, limits as configuration,
the state mirror, and rejection reporting.

**Added:** the pipeline's explicit ordering and the mapping from each check to a configuration
tier, both previously unstated. The canonical-only boundary, because L3 previously read
venue-listing fields, which made it venue-aware and unanswerable in a multi-venue deployment. The
reconciliation gate. And the daily-loss definition as net of fees.

**Open:** the mechanism by which L4 communicates dynamic position or capital budgets to L3 is not
designed. The architecture describes L4 computing budgets and L3 enforcing them, but no message
carries one.

---

## L4 (OMS) — the ledger

### What it is

L4 is the accountant. It decides nothing, trades nothing, and contacts nothing. It records what
happened and computes what it means.

It maintains the definitive record of positions, cash, orders, and P&L.

### Consumes and produces

- **Consumes:** `orders` to open reservations, `fills` to settle, `recon` for the venue's claim
  during reconciliation, and `control` for kill-switch commands.
- **Produces:** `ReconRequestMsg` and `ReconVerdictMsg` on `recon`, ledger-adjustment messages on
  `ledger`, and, off-path, snapshots and its journal.

L4 produces nothing on the hot path.

### How it works

#### What it holds

- **Positions**, per canonical instrument: signed net quantity, where positive is long and
  negative is short, plus average entry price, realised P&L, fees paid, and the last mark price.
  Positions are keyed by canonical id and never by venue listing, so exposure on any venue counts
  toward one limit and cannot be evaded by splitting across venues. The average entry price is
  stored as an **unsigned magnitude**, because direction lives only in the net quantity. Signing
  both would multiply two negatives in the weighted-average calculation and silently corrupt the
  basis on every short.
- **Cash**, per settlement currency: `settled` and `reserved`. Available is always *derived* as
  `settled − reserved`, never stored, because storing it would create a second place for the same
  fact to become stale.
- **Order records**, including a state that distinguishes "live at the venue" from "open from our
  point of view". Those differ exactly when a cancel is in flight.
- **A snapshot and a journal**, its own durable record for recovery.

#### The central rule

State is derived from the event stream. The same events in the same order produce the same state.
That is what makes replay, recovery, and backtest parity possible, and it is why the ledger's
equations contain no wall-clock reads, no disk reads, and no randomness.

The rule has an enumerated exception, described below.

#### Reservations, and why a cancel is a request

When an order is booked, L4 reserves against it. For a buy limit, that is quantity times price.
For a sell, it reserves nothing, because the position is the collateral.

When a **cancel** arrives, L4 does **not** release the reservation. It moves the order to a
pending-cancel state and keeps the money held. Only a terminal report from the venue, which is
filled, cancelled, or rejected, releases it. A cancel is a request, and the venue's report is the
fact. Releasing on the request would free cash for an order the venue had already filled. A
**replace** follows the same rule, for the same reason.

This puts an obligation on L5 (EMS). Every cancel it transmits must produce an outcome report,
including a cancel the venue rejects.

#### How money moves

`OnFill` is the only operation that moves money. Everything else adjusts reservations.

For a fill, L4 computes the notional with widening, computes the signed quantity, applies the
cash change, accumulates fees, and then updates the position and realised P&L. Note that a sell
credits cash and a buy debits it.

The position update has three cases.

1. **Opening or increasing.** Nothing is realised. The average entry price is recomputed as a
   weighted average of magnitudes.
2. **Reducing.** The part that closes realises P&L, computed as `closing_qty × (fill_price −
   avg_entry)`. The basis is untouched.
3. **Flipping through zero.** The old position is closed *and* a new one is opened on the other
   side at the fill price. The basis must reset to the fill price and must not blend with the old
   one.

When the position reaches zero, the basis resets to zero. Carrying a stale basis across a flat
point is how a round trip reports phantom P&L.

**Fees are kept separate from realised P&L**, so that cost remains attributable. The daily-loss
breaker uses realised minus fees, which is net without destroying the breakdown.

**A duplicate fill must not be applied twice.** Applying a fill accumulates cash, fees, position,
and P&L, so a second application fabricates money. Duplicates are not hypothetical. A venue may
re-send on reconnect, a gateway restart may replay recent fills, and a re-read after a log gap may
deliver one twice. L4 detects them with the report's `cumulative_qty` against the record's
already-applied total. If the cumulative total has not advanced, L4 applies nothing and counts it.
`contracts.md` explains why that field, rather than the report's own increment.

#### The exception to "state from events"

Two specified mutations do not come from the event stream.

- The off-path sweep that abandons an order whose cancel report never arrived. It is triggered by
  a wall-clock timeout.
- The corrections reconciliation applies, such as adopting the venue's position or basis.

Neither can be re-derived on replay, so both are recorded as **ledger adjustments**. L4 publishes
the adjustment before applying it, so a change cannot happen without the log knowing about it. The
ordering is the guarantee. See `contracts.md`.

#### Reconciliation

At startup, after loading a snapshot and replaying the log tail, L4 publishes a reconciliation
request. L5 returns the venue's claim per instrument, and **L4 performs the comparison**, because
L4 owns the state being compared. L5 never receives the expected numbers. Matched instruments
count toward reopening the gate. A mismatch, an inconclusive result, or a truncated claim keeps
it closed for a human.

### What it must not do

- L4 never makes trading decisions.
- L4 never contacts a venue.
- L4 never produces output on the hot path.
- L4 never assumes a cancel or a replace has taken effect.
- L4 never lets its state be changed by anything that is not an event or a recorded adjustment.

### How it fails

| Failure                                        | Response                                                                                                                                 |
| ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Process crashes                                | supervised restart. Load the snapshot, replay the log tail, reconcile against the venue, and keep the gate closed until matched          |
| Duplicate fill                                 | the `cumulative_qty` comparison applies it as a no-op and counts it                                                                      |
| Cancel report never arrives                    | the order stays pending-cancel with its reservation held. The timeout sweep or reconciliation resolves it, and both record an adjustment |
| Ledger drifts from the venue                   | reconciliation catches it at the next restart or operator request                                                                        |
| The venue has an order the ledger never booked | the gate blocks trading for a human to resolve                                                                                           |

**A gap worth naming.** Reconciliation runs only at restart or on operator request. Between
restarts, if the ledger drifts in a way the duplicate check does not catch, nothing notices until
the next restart. That is a deliberate trade-off, because continuous reconciliation would mean
constant venue queries, which are rate-limited and expensive. It does mean the ledger can be
quietly wrong for a while.

### Core versus added

**Core:** positions, cash, reservations, the event-derived rule, the P&L update rules, `OnFill` as
the only money-mover, and L4's order state machine.

**Added:** duplicate-fill detection, the pending-cancel and pending-replace semantics because
cancels once released reservations immediately, the ledger-adjustment messages, the
widened-arithmetic rule for a genuine overflow bug, and net-of-fees P&L for the breaker.

**Open:** the budget message L4 is said to send L3, described above. The exact shape of the
per-venue position split needed for flattening, described under L5. And whether ledger adjustments
need to be replayed or only recorded, which depends on how snapshots and replay interleave.

---

## L5 (EMS) — execution

### What it is

L5 is the hands. L4 says what we own, and L5 puts orders on the wire and reports back what
happened.

It turns internal orders into venue orders, manages the order lifecycle, and reports outcomes.

### Consumes and produces

- **Consumes:** `orders`, filtered to its own venue, `control` for kill-switch commands and
  flattens, and `recon` for reconciliation requests.
- **Produces:** `ExecutionReportMsg` on `fills`, and `ReconResultMsg` on `recon`.

One L5 instance runs per venue.

### How it works

#### Quantization, L5's distinctive job

A strategy asks to buy 0.5 at $100,050. A venue does not accept arbitrary numbers. Prices must be
multiples of its tick size, quantities must be multiples of its lot step, and the total must meet
its minimum notional. **L5 snaps the order to that grid immediately before transmitting it**, and
this is the only place in the system where that happens.

It cannot happen in L3, because tick size and lot step are properties of a venue listing and a
signal carries no venue. In a multi-venue deployment L3 would have nothing to look up. L5 knows
its venue, so L5 applies its grid.

An order that does not fit the grid is rejected or snapped locally, and the outcome is reported as
an execution report, not as a risk rejection, because the order already passed risk. This is a
venue-fit problem, not a risk problem.

#### The order lifecycle

From the venue's point of view, an order moves through sent, acknowledged, and possibly partially
filled, then reaches a terminal state of filled, cancelled, or rejected. L5 translates each of
those events into an `ExecutionReportMsg` on `fills`, which is what L4 uses to move money.

**L5 must report every outcome.** That includes the ack, partial fills, terminal fills, cancels,
and rejects. It also includes a cancel that the venue rejects because the order is already gone.
The obligation is not politeness. Because a cancel does not release L4's reservation until a
terminal report arrives, a dropped outcome leaves cash reserved forever. This is the tightest
coupling between L5 and L4.

#### Fees

L5 attaches the **venue's own reported fee** to each fill. It never substitutes a locally computed
fee, because real fees drift from published schedules through volume tiers, promotions, and
discounts. A modelled fee is internal state that can never be reconciled against a venue
statement. A simulated venue has no exchange to ask, so its L5 models the fee from configuration
and marks the message as modelled, so that downstream can tell a guess from a fact.

#### Rate limits

Two independent limits apply, and the lower one binds.

- **L3's limit** protects the system from a runaway strategy. It rejects the signal before an
  order exists, and the rejection is visible to the strategy.
- **The venue's limit** protects the account from the venue's own ban. Exceeding it is not a
  rejection but a restriction that can outlast the session.

An order throttled by the venue limit is **not** a risk rejection. It already passed risk and is
in the ledger. L5 therefore backs off and retries rather than discarding it. Discarding would
leave an order the ledger believes is live but which was never transmitted.

#### Venue routing

Each instance knows its own venue id and ignores orders addressed elsewhere, so any number of
gateways can subscribe to the same `orders` channel without coordination. An order with a wildcard
venue id is claimed by whichever gateway receives it. An order naming a venue is acted on only by
that venue.

### What it must not do

- L5 never makes risk decisions.
- L5 never performs accounting. It does not know positions or P&L, and it must not be told the
  expected numbers during reconciliation.
- L5 never tells upstream layers which venue to use.
- L5 never reinterprets the lifecycle. It reports what the venue said.

### How it fails

| Failure                             | Response                                                                                                |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Socket drops                        | reconnect in-process with backoff                                                                       |
| Process dies                        | supervised restart. Reservations survive in L4, and reconciliation checks open orders against the venue |
| Venue rejects an order              | publish a rejected execution report, and L4 releases the reservation                                    |
| Cancel rejected as already gone     | still publish a terminal report                                                                         |
| Order does not fit the venue's grid | reject or snap locally, and report as an execution report                                               |
| Venue rate limit hit                | back off and retry. Never discard. A sustained breach is an operator alert                              |
| The venue reports an unexpected fee | use the venue's number, which is authoritative                                                          |

### Core versus added

**Core:** the order lifecycle, quantization as the single boundary, one process per venue, and the
"report every outcome" obligation.

**Added:** the venue id on orders and the routing check, the replace operation in the lifecycle,
the fee and liquidity flag on the wire because fees were previously absent from the design, the
distinction between L3's rate limit and the venue's, and the rule that a throttled order is
retried rather than discarded.

**Open:** how a flatten is addressed when a position is held on more than one venue. Positions are
canonical, so "bring this position to zero" does not on its own say which venue each order should
go to. The resolution is for the ledger to retain a per-venue split, described under L4, so that
L5 receives explicit per-venue legs.

### The generalizable ideas

Venues do not accept arbitrary numbers, so an order must be snapped to a grid. That snapping
belongs at the edge, where the venue is known, and happens once. Orders are a state machine. Fees
are received facts, not computed ones. A rate limit is a resource you spend.

---

## L6 (observability)

### What it is

L6 is everything that watches the system without participating in trading: dashboards, health,
alerts, and the emergency controls.

It shows you what is happening, and it lets you stop it.

### Consumes and produces

- **Consumes:** the telemetry summary, heartbeats, `fills` passively, and reconciliation verdicts.
- **Produces:** `KillSwitchMsg` and reconciliation requests on `control` and `recon`.

L6 never parses raw hot-path messages.

### How it works

L6 is deliberately unlike every other layer. **It may be slow, and it may die.** L1 to L5 are on
the critical path. L6 is not. If the dashboard freezes or the Python process crashes, trading is
unaffected. That property lets observability be developed, rewritten, and experimented on without
risk to the trading system, and it is worth protecting.

#### Its data source

Hot-path messages are fixed-size binary structs, which are cheap for C++ and awkward for Python,
and Python should not parse every tick in any case. So a small C++ **telemetry process**
subscribes to the data channels, computes a low-frequency summary such as P&L, positions, and
latency percentiles, and republishes it as JSON. L6 reads that.

The telemetry process is off the hot path and droppable. Understand it as how L6 gets its data,
not as an independent component to reason about.

#### The kill switch

L6 is where a human presses the button. It broadcasts a kill-switch command, and each process acts
on the part it owns. Which layer executes which command is normative, because a command with no
named executor silently does nothing, which is the worst property an emergency control can have.
The commands and their executers are in `contracts.md`.

The reconciliation gate is related but distinct. L6 aggregates reconciliation verdicts and, when a
pass is fully matched, reopens trading. Anything else keeps it closed.

#### Who watches the watcher

L6 is the process that declares others dead when their heartbeats stop. If L6 itself is dead,
nobody notices the noticer. In production, supervision outside the system, such as process
supervision and external alerting, closes that gap. L6 does not watch itself.

### What it must not do

- L6 never gates trading directly. It publishes commands and verdicts, and enforcement happens in
  L3 and L4.
- L6 never sits on the critical path.
- L6 never parses hot-path messages.
- L6 never holds authoritative state. Its view is derived and may lag.

### How it fails

| Failure                                | Response                                                                                      |
| -------------------------------------- | --------------------------------------------------------------------------------------------- |
| Process crashes                        | nothing downstream cares. Restart it                                                          |
| Dashboard slow                         | telemetry drops the oldest. Trading is unaffected                                             |
| Telemetry process dies                 | L6 goes blind, and trading continues                                                          |
| Heartbeats missed                      | L6 declares the process dead and alerts, though a missed heartbeat can also mean L6 is broken |
| Config skew observed across heartbeats | L6 raises it, and the trading gate closes                                                     |

### Core versus added

**Core:** passive observation, the kill switch, and the droppable-by-design property.

**Added:** the config-hash comparison on heartbeats, the reconciliation verdict aggregation and
the rule that a clean pass reopens automatically while anything else blocks, and the explicit
kill-command ownership table.

**Removed:** the audit recorder. The log was originally assigned to monitoring, which is exactly
backwards. The log is durable and the monitor is disposable, so a dashboard restart or a slow
write would have silently truncated the artifact recovery depends on. Durability got its own
process, and the monitor stayed disposable. The general rule: do not let the thing you might
restart own the thing you cannot lose.
