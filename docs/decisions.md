# Decisions

**Audience:** anyone asking "why is it built this way?".

Each entry records one decision, the problem it answered, and what it costs. The entries are
short on purpose. They explain the choice, not the mechanism. For the mechanism, follow the
cross-reference.

A decision is listed here when the alternative was plausible and the reason for rejecting it is
worth keeping. Routine choices are not listed.

<details>
<summary>Contents</summary>

- [The message bus is bounded, and each channel declares what it drops](#the-message-bus-is-bounded-and-each-channel-declares-what-it-drops)
- [Money is fixed-point, not floating point](#money-is-fixed-point-not-floating-point)
- [The core is venue-agnostic, and venue code lives at the edges](#the-core-is-venue-agnostic-and-venue-code-lives-at-the-edges)
- [Messages use natural alignment, not packing](#messages-use-natural-alignment-not-packing)
- [Every message is value-initialized](#every-message-is-value-initialized)
- [Timestamps are monotonic, never the CPU cycle counter](#timestamps-are-monotonic-never-the-cpu-cycle-counter)
- [A cancel is a request, not a state change](#a-cancel-is-a-request-not-a-state-change)
- [A replace reuses the order id](#a-replace-reuses-the-order-id)
- [Duplicate fills are detected with a cumulative counter](#duplicate-fills-are-detected-with-a-cumulative-counter)
- [Fees travel on the wire, and a simulation marks its own](#fees-travel-on-the-wire-and-a-simulation-marks-its-own)
- [Python runs in its own process](#python-runs-in-its-own-process)
- [Strategies are registered at build time, not loaded at runtime](#strategies-are-registered-at-build-time-not-loaded-at-runtime)
- [Signals and orders are separate channels](#signals-and-orders-are-separate-channels)
- [L3 checks canonical limits only](#l3-checks-canonical-limits-only)
- [The venue is authoritative, and trading fails closed](#the-venue-is-authoritative-and-trading-fails-closed)
- [The audit log is written by its own process](#the-audit-log-is-written-by-its-own-process)
- [Ledger adjustments are recorded before they are applied](#ledger-adjustments-are-recorded-before-they-are-applied)
- [The ledger keeps a per-venue position split for flattening](#the-ledger-keeps-a-per-venue-position-split-for-flattening)
- [The system runs on one box](#the-system-runs-on-one-box)

</details>

---

## The message bus is bounded, and each channel declares what it drops

**Problem.** Processes need to exchange messages without blocking each other. The naive answer,
an unbounded queue, converts a slow consumer into a memory exhaustion failure. The other naive
answer, blocking on a full queue, lets one slow subscriber stall the whole system.

**Decision.** Use a bounded message bus with a per-channel failure policy. A full buffer returns
an error. No hot thread may block, so each channel declares what it does instead: `md` and `fills`
treat a full buffer as fatal, `signals` and `risk_events` drop and count, `orders` retries briefly
and then latches the kill switch, and `control` retries until delivered.

**Consequence.** Every publish site must handle a failure, which is more code than assuming
success. In return, the system degrades in a defined way under load instead of corrupting
silently. The rule that makes it safe is that no channel which changes the ledger may drop a
message.

See `contracts.md`, "Publication failure is per-channel".

## Money is fixed-point, not floating point

**Problem.** Binary floating point cannot represent decimal fractions exactly. `0.1 + 0.2` is not
`0.3`, and a system that cannot add up money cannot be trusted.

**Decision.** Store prices and quantities as signed 64-bit integers scaled by 1e9. Any product of
two scaled values widens to a 128-bit intermediate before dividing.

**Consequence.** Arithmetic is exact and deterministic, and it is the same in live trading and in
backtest. The cost is that every multiplication must be widened. A bare `qty × price` overflows
`int64_t` above $9.22 per unit, and the wrapped result is still a plausible-looking number, so the
rule has to be followed rather than checked.

See `contracts.md`, "Numbers are fixed-point integers".

## The core is venue-agnostic, and venue code lives at the edges

**Problem.** A trading system built around one exchange's API inherits that exchange's
assumptions everywhere. Adding a second venue then means changing strategy, risk, and ledger code.

**Decision.** L1 (ingestion) and L5 (EMS) are the only layers that contain venue-specific code.
L2, L3, and L4 move canonical instrument ids and opaque venue ids. Adding a venue adds no message
type and requires no change to the three middle layers.

**Consequence.** A signal carries no venue, so risk cannot check venue micro-structure such as
tick size or lot step. That check moves to L5, which knows its venue. The benefit is that a
strategy written against one venue runs against another by changing configuration.

See `layers.md`, L3 and L5.

## Messages use natural alignment, not packing

**Problem.** Two binary layout approaches exist. Packed layout removes all padding and is required
when bytes must be identical across compilers or machines. Natural alignment lets the compiler
pad, and is valid when the bytes are only ever read on one machine.

**Decision.** Use natural alignment. Messages travel through shared memory on one box, so they
never cross a machine boundary.

**Consequence.** Some bytes are wasted on padding. In return, every 64-bit field is naturally
aligned, which is faster than the misaligned loads that packing would force. If messages ever
cross machines, the encoding at that seam becomes explicitly little-endian and packed, behind a
codec.

See `contracts.md`, "Natural alignment, no packing".

## Every message is value-initialized

**Problem.** Struct padding is not zeroed by default. If padding is indeterminate, the same event
recorded twice produces different bytes, byte comparison between two equal messages fails
intermittently, and reused buffers leak data from earlier messages into the log.

**Decision.** Construct every message as `T m{}`. Value-initialization zeroes every byte,
including padding.

**Consequence.** A reproducible audit log and sound byte comparison. Zeroing each gap by hand was
rejected because a field reorder would silently invalidate it while still compiling.

See `contracts.md`, "Every message is value-initialized before use".

## Timestamps are monotonic, never the CPU cycle counter

**Problem.** Two clocks are available for a message timestamp. The wall clock can jump backwards
when network time adjusts it. The CPU cycle counter is per-core, so a process that restarts on a
different core can appear to go backwards too.

**Decision.** Read `CLOCK_MONOTONIC` at publish. The cycle counter is allowed only for internal
latency measurement.

**Consequence.** Restart detection becomes decidable, because monotonic time does not reset when a
process restarts. If a process restarts, its sequence counter goes backwards while its timestamp
goes forward, and that combination is unambiguous. The cycle counter is cheaper, but it makes a
genuine restart look like a corrupt clock.

See `contracts.md`, "Sequence numbers across a restart".

## A cancel is a request, not a state change

**Problem.** A cancel could release the reserved cash immediately, or wait for the venue.
Releasing immediately is wrong whenever the venue had already filled the order: the ledger would
free money that was just spent.

**Decision.** When a cancel arrives, move the order to a pending-cancel state and release nothing.
Only a terminal report from the venue releases the reservation. This applies to replace as well.

**Consequence.** L4 holds reservations longer, and L5 takes on an obligation: every cancel it
transmits must produce an outcome report, including a cancel the venue rejects. Without that
report, cash stays reserved forever.

See `contracts.md`, `CancelOrderMsg`, and `layers.md`, L4.

## A replace reuses the order id

**Problem.** A replace could be implemented as cancel plus new order, which is simpler. It is also
wrong, for three reasons. The ledger would hold two records where the venue holds one, so
reconciliation would report a mismatch on a correct system. The replacement leg would be live but
unreserved. And the intermediate state, where the order is live at the venue but superseded
locally, would have no representation.

**Decision.** Reuse `client_order_id` across a replace, because from the venue's point of view it
is one order with amended terms. Quantity and price are absolute values, so a retry converges.

**Consequence.** The ledger and the venue keep describing the same object, and reconciliation
does not fire on correct behaviour. A safety mechanism that fires on correct behaviour trains its
operator to ignore it.

See `contracts.md`, `ReplaceOrderMsg`.

## Duplicate fills are detected with a cumulative counter

**Problem.** The `fills` channel is at-least-once. A venue may re-send on reconnect, a gateway
restart may replay recent fills, and a re-read after a log gap may deliver one twice. Applying a
fill accumulates cash, fees, position, and P&L, so a second application fabricates money.

**Decision.** The execution report carries `cumulative_qty`, the venue's running total for the
order. The ledger compares it against the total it has already applied. If it has not advanced,
the ledger applies nothing and counts it.

**Consequence.** Duplicate detection is one comparison against a counter the ledger already holds,
and it survives a restart because the counter is rebuilt from events. Using the report's own
increment instead would require remembering every increment already applied, which is unbounded
state. A duplicate is not an error, but it is counted, because a rising count means a venue is
re-sending more than expected.

See `contracts.md`, `ExecutionReportMsg`.

## Fees travel on the wire, and a simulation marks its own

**Problem.** A fee can be reported by the venue or modelled locally from a published schedule. The
two must not be confused, because a modelled fee can never be reconciled against a venue statement.

**Decision.** Attach the venue's own reported fee to each fill. A simulated venue, which has no
exchange to ask, models the fee from configuration and sets the `ModelledFill` flag on the header.

**Consequence.** P&L reflects what was actually charged, and a backtest's modelled costs are
labelled as modelled. The flag is what keeps a guess from being mistaken for a fact downstream.

See `contracts.md`, `ExecutionReportMsg`, and `layers.md`, L5.

## Python runs in its own process

**Problem.** Research models are easier to write in Python. The obvious way to run one is to embed
the interpreter in the strategy process.

**Decision.** Run the Python strategy in its own OS process, talking over the bus. The interpreter
is never loaded into a hot-path process.

**Consequence.** The global interpreter lock, interpreter startup and shutdown, and garbage
collection stay out of the address space of a process that must never block, and a Python fault
cannot take down the strategy loop. The cost is one bus hop on the signal path. Being careful with
an embedded interpreter is not a substitute, because the problem is having a second execution
environment inside the process at all.

See `layers.md`, L2.

## Strategies are registered at build time, not loaded at runtime

**Problem.** "Plugin architecture" suggests loading a strategy as a shared object. A strategy is a
hot-path process peer, and loading a shared object allocates and runs arbitrary initializers
mid-process. It also makes "which code produced this log" unanswerable.

**Decision.** Register strategies in a compiled-in factory keyed by a string. A configuration
entry's `impl` field must match a registered implementation.

**Consequence.** An unknown `impl` is a startup failure, not a warning, so a deployment cannot
silently trade nothing. The strategy set is fixed by the build, which keeps replay reproducible.

See `layers.md`, L2.

## Signals and orders are separate channels

**Problem.** Intent and commitment could share one channel.

**Decision.** Keep them separate, and give them different failure policies. A dropped signal is
tolerable and is counted. A dropped order is not, and latches the kill switch.

**Consequence.** The system can lose a trading opportunity under load without losing correctness.
That is the reason the two are distinct, not a preference: intent can be lost, and a commitment
cannot.

See `contracts.md`, "Publication failure is per-channel".

## L3 checks canonical limits only

**Problem.** Tick size, lot step, and minimum notional are properties of a venue listing. A signal
carries a canonical instrument id and no venue, so in a multi-venue deployment a check against
"the venue's tick size" has nothing to look up.

**Decision.** L3 (risk) checks canonical quantities: order size, position, collar, rate,
suspension, and P&L. L5 (EMS) checks venue micro-structure.

**Consequence.** Adding a venue does not change the risk engine. The cost is that L3 cannot catch
an order that will fail the venue's grid, so that rejection happens at L5 and returns as an
execution report rather than a risk rejection.

See `layers.md`, L3.

## The venue is authoritative, and trading fails closed

**Problem.** After a restart, the ledger holds a belief about positions and orders. That belief
can be wrong. Trading on it is how a system takes a position it does not know it has.

**Decision.** On restart, compare the ledger against the venue. Where they disagree, adopt the
venue's number and record the change. An instrument counts as matched only if an explicit `ok`
result arrived and the comparison agreed. Anything else, including no answer at all, closes the
trading gate.

**Consequence.** The system starts unable to trade, and proves itself before it can. "No answer"
is not "no disagreement", so a rate-limited or unavailable venue blocks trading rather than being
read as agreement. A clean pass reopens trading automatically, because requiring a human to approve
a clean start would make the gate meaningless through reflex.

See `lifecycle.md`, "Restart and recovery".

## The audit log is written by its own process

**Problem.** Three parts of the design claimed to own the log, and none wrote it. Assigning it to
the monitor is exactly backwards: the log is durable, and the monitor is disposable.

**Decision.** A dedicated recorder process subscribes to the data channels and writes the
append-only binary log. It is not the monitor, and it is not the ledger.

**Consequence.** A dashboard restart, a slow write, or a reconnecting monitor cannot truncate the
artifact recovery depends on. The recorder never applies backpressure to a producer, because
durability yields to the hot path. If it falls behind, it records a gap marker rather than
stalling a publisher.

See `layers.md`, L6, and `overview.md`, "More than six processes".

## Ledger adjustments are recorded before they are applied

**Problem.** Two ledger mutations have no event behind them: the timeout sweep that abandons an
order whose cancel report never arrived, and the corrections reconciliation applies. Without a
record, replay would produce a different ledger than what actually happened.

**Decision.** Record each one as a ledger adjustment, published before it is applied. If the
publish fails, the change is not applied.

**Consequence.** Any change that happened is in the log, because it could not happen until it was
logged. The ordering is the guarantee, not the message. Because the operations are assignments or
terminal transitions rather than accumulations, applying one twice converges, so replay seeing a
duplicate is harmless.

See `contracts.md`, "Ledger adjustments", and `layers.md`, L4.

## The ledger keeps a per-venue position split for flattening

**Problem.** Positions aggregate on the canonical instrument, so a position has no venue. A flatten
command must send orders, and an order must name a venue, so "bring this position to zero" does
not say where each order should go. The default, a wildcard venue, is claimed by one gateway,
which under-flattens one venue and can over-flatten another.

**Decision.** The ledger keeps a per-venue split alongside the canonical aggregate, fed from the
venue id already present on every execution report. The canonical quantity stays the risk
aggregate. A flatten emits one order per non-zero venue leg, each naming its venue.

**Consequence.** Flatten is correct in a multi-venue deployment, and its retry rule applies per
leg rather than per canonical position. The cost is extra ledger state, which must reconcile
against the canonical aggregate so it does not become a second source of truth for exposure.

See `layers.md`, L4 and L5.

## The system runs on one box

**Problem.** A design that supports multiple machines must define a wire encoding, versioning, and
cross-machine failure handling.

**Decision.** Assume one box. Messages use native byte order and natural alignment, and the bus is
shared memory.

**Consequence.** The design is simpler and faster, and it cannot be distributed without an
explicit encoding at that seam. That case is out of scope, and the constraint is recorded rather
than hidden.
