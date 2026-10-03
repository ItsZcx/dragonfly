# Contracts: the data

**Audience:** anyone reading, writing, or debugging a message, and anyone implementing a layer.

This is the authoritative description of the wire format. Every field here is pinned by a
`static_assert` in `contracts/`. Where this document and the code disagree, the code wins.

All cross-process communication in Dragonfly is a fixed-size struct. No message contains a
pointer, a `std::string`, a `std::vector`, or a virtual function. A message is bytes, and a
consumer reads it by casting a pointer over those bytes. This document explains what those bytes
mean.

<details>
<summary>Contents</summary>

- [The message header](#the-message-header)
  - [Field by field](#field-by-field)
  - [Sequence numbers across a restart](#sequence-numbers-across-a-restart)
- [Conventions that apply to every message](#conventions-that-apply-to-every-message)
  - [Numbers are fixed-point integers](#numbers-are-fixed-point-integers)
  - [Natural alignment, no packing](#natural-alignment-no-packing)
  - [Messages are fixed-size and own nothing](#messages-are-fixed-size-and-own-nothing)
  - [Every message is value-initialized before use](#every-message-is-value-initialized-before-use)
  - [Timestamps](#timestamps)
  - [The header is on every message](#the-header-is-on-every-message)
- [The message set](#the-message-set)
  - [Market data: L1 produces](#market-data--l1-ingestion-produces)
  - [Signals: L2 to L3](#signals--l2-strategy-to-l3-risk)
  - [Orders: L3 to L4 and L5](#orders--l3-risk-to-l4-oms-and-l5-ems)
  - [Execution: L5 to L4, L3, and L6](#execution--l5-ems-to-l4-oms-l3-risk-and-l6)
  - [Control: every process](#control--every-process)
  - [Reconciliation: L4 and L5, reported to L6](#reconciliation--l4-oms-and-l5-ems-reported-to-l6)
  - [Ledger adjustments: L4 to the recorder and L6](#ledger-adjustments--l4-oms-to-the-recorder-and-l6)
- [Shared vocabulary](#shared-vocabulary)
  - [Reconciliation](#reconciliation)
  - [Venue identity](#venue-identity)
  - [Pre-trade rejection reasons](#pre-trade-rejection-reasons)
- [Publication failure is per-channel](#publication-failure-is-per-channel)

</details>

---

## The message header

Every message begins with the same 32-byte header. It is never optional and never changes shape.

```
uint32_t msg_type;      //  0 ..  4   which kind of message
uint32_t msg_len;       //  4 ..  8   total size of the message, in bytes
uint32_t producer_id;   //  8 .. 12   which process emitted it
uint8_t  flags;         // 12 .. 13   provenance bits
// 3 bytes of padding, so ts_ns starts 8-byte aligned
uint64_t ts_ns;         // 16 .. 24   the producer's monotonic timestamp
uint64_t seq_num;       // 24 .. 32   per-producer message counter
```

A message's total size is always `32 + body`. This is why the sizes below read as 48, 64, 88, and
so on.

### Field by field

**`msg_type`** identifies which message type in this document the message is. The value decides
how the body is read, so it is the first thing a consumer checks.

**`msg_len`** is the total length. A reader that does not recognise a `msg_type` can use it to
skip exactly the right number of bytes. It also lets a raw binary log be replayed without the
bus's own framing, because each message carries its own length.

**`producer_id`** identifies the process that sent the message. It is a stable logical identity,
not a handle for one process instance: a restarted process returns with the same `producer_id`.
That is what makes restart detection work (see "Sequence numbers across a restart"). The field
also tells a consumer whose data it is reading, because a channel can have several producers. All
feed handlers publish market data to one channel, so this field is what lets one strategy compare
two venues without a separate channel per venue.

The allocation is fixed, so that two processes cannot claim the same id. A duplicate makes
sequence-gap detection ambiguous, which is the failure the field exists to prevent.

| Id or range   | Process                                        |
| ------------- | ---------------------------------------------- |
| 1             | feed handler, primary venue                    |
| 2             | strategy, canonical single-strategy process    |
| 3             | risk engine                                    |
| 4             | OMS                                            |
| 5             | EMS, primary venue gateway                     |
| 6             | observability, the monitor                     |
| 7             | telemetry process                              |
| 8             | replay harness                                 |
| 9             | audit recorder                                 |
| 100 to 199    | strategy instances. Instance N takes `100 + N` |
| 200 to 255    | additional feed handlers, one per venue        |
| 256 to 299    | additional gateways, one per venue             |
| 300 and above | reserved                                       |

Feed handlers and gateways take separate ranges because they are different layers with
independent crash domains. Sharing one range would let a venue's feed handler and its gateway
collide, and the collision would stay invisible until sequence-gap detection misfired.

**`flags`** is a bitmask of facts about how the message was produced. One bit is defined.

| Bit | Name           | Meaning                                                                |
| --- | -------------- | ---------------------------------------------------------------------- |
| 0   | `ModelledFill` | the `fee` on this execution report came from a model, not from a venue |

A backtest has no exchange to report a fee, so the simulated venue invents one from a schedule.
That invented number is honest as long as everything downstream knows it is invented. Without the
bit, a modelled fee and a reported fee are indistinguishable, and a backtest's simulated costs can
be mistaken for a venue's confirmed costs. A live venue reports its own fee and leaves the bit
clear.

`flags` is a `uint8_t` rather than a `bool` because it occupies a byte that alignment already
requires, so leaving room costs nothing. Treat it as a one-bit field. A second bit requires a
written justification, because a header that accumulates flags accumulates rules for reading
them.

Three bytes of padding follow `flags`, so that `ts_ns` starts at offset 16. The padding is not a
field. Nothing writes to it and nothing reads it, and value-initialization keeps it at zero.

**`ts_ns`** is the producer's timestamp, read from a **monotonic** clock when the message is
published. It is not the system wall clock, which network time adjustments can move backwards,
and it is not the CPU cycle counter, which is per-core and can appear to go backwards when a
process restarts on a different core. Monotonic time does not reset when a process restarts, and
that property makes the restart detection below decidable.

`ts_ns` differs from `exchange_ts_ns`, which appears in market data and execution messages.
`ts_ns` is our clock: when we saw the event. `exchange_ts_ns` is the venue's timestamp: when the
venue says it happened. Both are kept. The difference between them is our latency, and the
venue's value is what audit and reconciliation read.

**`seq_num`** is a per-producer counter that increases by one for every message its producer
emits. A consumer compares the sequence it receives against the sequence it expects. A jump
forward means messages were dropped, for example because the consumer fell behind and a ring
buffer overwrote them. Detecting the jump is what lets a consumer request a resync instead of
trading on stale state.

### Sequence numbers across a restart

A producer's `seq_num` lives in memory. After a restart it begins again at 1, while a consumer's
high-water mark is still at the old value. Two opposite situations then produce the same
observation, and they need opposite responses.

| Observation                                             | Meaning                                                                      | Response                                                                   |
| ------------------------------------------------------- | ---------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `seq_num` **backwards**, `ts_ns` **forward**            | the producer restarted, so its in-memory state is gone                       | reset the watermark. If the producer owns state, such as a book, resync it |
| `seq_num` **forward**, `ts_ns` **forward**              | messages were genuinely lost                                                 | keep the watermark and request a resync                                    |
| `ts_ns` **backwards**, in either direction of `seq_num` | not a restart and not a gap, because a monotonic clock cannot move backwards | fail closed. Do not resync and do not guess                                |

The direction of `ts_ns` is what tells the first two rows apart. This is why the clock rules are
not pedantry. Handling the two rows the wrong way round produces either a resync storm, where a
restart is misread as a gap, or silent data loss, where a gap is misread as a restart. A restart
is normal traffic. It is logged, the resync follows, and it is not treated as an error.

---

## Conventions that apply to every message

### Numbers are fixed-point integers

Prices and quantities are signed 64-bit integers scaled by `kScale = 1_000_000_000` (1e9).

```
$105.50      →  105_500_000_000
1.25 BTC     →  1_250_000_000
```

Binary floating point cannot represent decimal fractions exactly, so a system that stores money as
`double` cannot reliably add it up. One universal scale is used across every instrument and venue.

**A product of two scaled values must widen to a 128-bit intermediate before dividing.**

```
notional = MulDiv(qty, price)      // (qty × price) / kScale, in __int128, then narrowed
```

Both operands are scaled, so their product is scaled twice. Dividing by `kScale` puts it back.
Passing `1` as the divisor skips that step, and the result overflows `int64_t` for almost every
real trade. The boundary is narrow:

| Quantity | Largest price before a raw `int64_t` product overflows |
| -------- | ------------------------------------------------------ |
| 1.0      | $9.22                                                  |
| 0.01     | $922                                                   |
| 1e-8     | ~$922 million                                          |

So `1.0 BTC × $100,000` is `1e23` against an `int64_t` ceiling of `9.22e18`, about ten thousand
times too large. A wrapped product is still a plausible-looking number. That is why this is a
rule rather than a runtime check. Every site that produces a scaled quantity from a product uses
`MulDiv`: notionals, cash movements, reservations, P&L, and the minimum-notional comparison.

The one case that widens without dividing is a numerator that is itself divided by a scaled
value, which is the weighted-average cost basis. That case uses an explicit widening cast rather
than `MulDiv(..., 1)`, so the two situations cannot be confused.

`MulDiv` does **not** check for overflow. Its precondition is that the 128-bit result fits in
`int64_t`, and that precondition is enforced at the boundary rather than in the arithmetic: L3
bounds quantity at its check 4, and the price collar at its check 5 bounds price, so a notional
large enough to overflow cannot be admitted. `MulDiv` carries an `assert` that fires in a debug
build and is compiled out under `NDEBUG`, which makes it a development tripwire rather than
protection. See `decisions.md`, "Money is fixed-point", for why the check lives at the boundary.

### Natural alignment, no packing

Messages use the compiler's natural alignment. There is no `#pragma pack`. Fields are ordered
within each struct to avoid interior padding where practical.

Two approaches to binary layout exist, and they are not interchangeable.

- **Packed** (`#pragma pack(1)`) means no padding. This is correct when the layout is a wire
  format that must be byte-identical across compilers, machines, or languages.
- **Natural alignment** means the compiler pads as it sees fit. This is correct when the layout is
  only ever read by casting a pointer on one machine.

Dragonfly uses natural alignment because its messages never cross a machine boundary. They travel
through shared memory on one box. Packing would force misaligned 64-bit loads, which costs more
than the bytes it saves. If messages ever do cross machines, the encoding at that seam becomes
explicitly little-endian and packed, behind a codec. That case is out of scope here.

Because the compiler defines the layout rather than a person, every message is verified at compile
time with `sizeof` and `offsetof` assertions. The sizes and offsets in this document exist because
those assertions exist. They are not maintained by hand.

### Messages are fixed-size and own nothing

A message may not contain a pointer, a `std::string`, a `std::vector`, or a virtual function.
Anything variable-length would need allocation or indirection, and the hot path permits neither.
This is why an order book snapshot has a compile-time depth cap: the array must have a size the
compiler knows.

Every message is also *trivially copyable* and *standard layout*. Those are the two properties
that make it valid to cast a pointer over raw bytes.

### Every message is value-initialized before use

Construct messages as `T m{}`, never as `T m;`.

Value-initialization zeroes every byte, including padding. It matters for four reasons.

1. The audit log stores messages verbatim. If padding were indeterminate, the same event recorded
twice would produce different bytes, and byte-identical replay would be impossible.
2. Byte comparison would be unsound. Two messages with identical fields would compare unequal
   because their padding differed. The failure would be intermittent and depend on allocator
   state.
3. Messages are written into reused buffers. An uninitialized padding byte would carry data from
   an unrelated earlier message into the log.
4. Determinism is the property the recovery story rests on, and indeterminate bytes sit directly
   underneath it.

Zeroing each gap by hand was rejected. A field reorder would silently invalidate it while still
compiling. `T m{}` is a property of how a message is built, so it cannot be forgotten at one call
site or broken by a layout change.

A test checks this. It value-initializes each message type twice, writes identical values, and
asserts the bytes are equal. That single test catches indeterminate padding, a forgotten field,
and a field-order regression at once.

### Timestamps

| Kind                | Field            | Clock                      | Rewritten on replay?         |
| ------------------- | ---------------- | -------------------------- | ---------------------------- |
| Internal event time | `hdr.ts_ns`      | monotonic, read at publish | **No**, republished verbatim |
| Venue time          | `exchange_ts_ns` | the venue's own            | No                           |

A component uses the clock in two distinct ways, and they must not be confused.

- **To measure a duration**, it asks "what time is it now?". This drives token-bucket refill,
  heartbeat staleness, and timeout deadlines. In a live run this is a monotonic system clock. In
  replay it is a simulated clock, advanced to each event's timestamp before that event is
  delivered.
- **To read when a message was stamped**, it reads `hdr.ts_ns`. That field is data. Replay
  republishes each logged message with its original timestamp, producer, and sequence number.

A replayer is a subscriber, not a producer, so it never re-stamps a message. Re-stamping would
break byte-identical replay, erase the real inter-arrival timing from the log, and make restart
detection depend on how the simulated clock happened to be seeded.

**Replay preserves stamps. Simulation creates them.** A message produced by a simulated matching
engine was never in the log, so its producer is genuine and stamps its own timestamp.

### The header is on every message

Every message is a `MsgHeader` followed by its body. The offsets in the next section continue from
32.

---

## The message set

Eighteen message types, in six families. Throughout, "core" means the field is part of the
original design and would be designed again. A noted addition explains the failure it closes.

### Market data — L1 (ingestion) produces

These messages describe the market. They are the only thing strategies act on.

#### `TradeMsg` — an executed trade (64 bytes)

```
uint32_t instrument_id;  // 32..36   canonical instrument id
uint8_t  aggressor_side; // 36..37   0 = buyer crossed the spread, 1 = seller
int64_t  price;          // 40..48   fixed-point
int64_t  qty;            // 48..56   fixed-point
uint64_t exchange_ts_ns; // 56..64   the venue's timestamp
```

Three bytes of padding follow `aggressor_side`, so that `price` starts 8-byte aligned.

`instrument_id` is the canonical id that L1 (ingestion) resolved at ingress. `aggressor_side`
identifies the taker: the side that initiated the trade, as opposed to the side that was resting.

#### `QuoteMsg` — best bid and offer (80 bytes)

```
uint32_t instrument_id;  // 32..36
int64_t  bid_px;         // 40..48
int64_t  bid_qty;        // 48..56
int64_t  ask_px;         // 56..64
int64_t  ask_qty;        // 64..72
uint64_t exchange_ts_ns; // 72..80
```

This is the top of the book only. Size accompanies price because a bid without size is
misleading. A price with nothing behind it is not the same opportunity as one with real depth.

#### `BookDeltaMsg` — one price-level change (64 bytes)

```
uint32_t instrument_id;  // 32..36
uint8_t  side;           // 36..37   0 = bid side, 1 = ask side
uint8_t  action;         // 37..38   0 = add, 1 = modify, 2 = delete
int64_t  price_level;    // 40..48
int64_t  qty;            // 48..56   new size at the level (0 on delete)
uint64_t exchange_ts_ns; // 56..64
```

#### `BookSnapshotMsg` — a full book (2096 bytes)

```
uint32_t instrument_id;  //   32..36
uint16_t bid_levels;     //   36..38   how many of the 64 are populated
uint16_t ask_levels;     //   38..40
uint64_t exchange_ts_ns; //   40..48
Level    bids[64];       //   48..1072   { int64_t px; int64_t qty; } best first
Level    asks[64];       // 1072..2096   best first
```

A feed handler sends this on connect and on resync, never per tick. It is the largest message in
the system, and it is off the hot path.

**A snapshot, its deltas, the sequence numbers and the resync are one mechanism, not four
messages.** A full book cannot be sent on every update, so a consumer is seeded with a snapshot
and then applies deltas. That works only if no delta is missed. Every message carries `seq_num`
for exactly this reason. A jump in sequence means a delta was lost, and the response is to discard
the local book and ask for a fresh snapshot rather than to guess. The snapshot's own sequence
number anchors the join: deltas with a lower sequence are stale and ignored. That anchoring makes
reconstruction deterministic even across a duplicate snapshot or a repeated replay.

**The depth cap is a trade-off to state explicitly.** Fixed-size messages need a compile-time
bound, so the snapshot carries exactly 64 levels per side and no consumer's book can be more
accurate than that. The snapshot itself is rare, so its size barely matters. What matters is the
size of each consumer's local book, which every delta touches. Raise the cap in four cases: a
strategy reads depth rather than the touch, the instrument's tick is small enough that 64 levels
cover a narrow price band, an execution algorithm needs a specific depth, or you deliberately
keep less than the venue offers. Do not raise it for near-touch strategies. The cap is a
wire-format constant, so changing it changes this message's size, its assertions, and the byte
layout of any logged snapshots.

#### `BookSnapshotRequestMsg` — request a fresh book (48 bytes)

```
uint32_t instrument_id;  // 32..36
uint64_t after_seq_num;  // 40..48   the snapshot must be at least this fresh
```

Four bytes of padding follow `instrument_id`.

This is the only consumer-to-producer message in the system. A consumer that notices a sequence
gap must ask the producing feed handler for a new snapshot. That request is a backwards edge in an
otherwise downstream-only topology, so it travels on `control`, the one channel every process both
publishes and subscribes to. **The answer is not a reply on `control`.** The handler publishes an
ordinary `BookSnapshotMsg` on `md`, which reaches every consumer at once. That is what makes
coalescing correct: one publish satisfies every outstanding request, so a gap storm cannot become
a snapshot storm. Requests are rate-limited and coalesced for the same reason, and the handler
services them on an off-path thread rather than the ingest thread.

### Signals — L2 (strategy) to L3 (risk)

#### `AlphaSignalMsg` — trading intent (80 bytes)

```
uint64_t signal_id;      // 32..40   unique system-wide
uint32_t strategy_id;    // 40..44
uint32_t instrument_id;  // 44..48   canonical id
uint8_t  side;           // 48..49   0 = buy, 1 = sell
uint8_t  order_type;     // 49..50   0 = limit, 1 = market
uint8_t  tif;            // 50..51   0 = GTC, 1 = IOC, 2 = FOK
int64_t  target_qty;     // 56..64
int64_t  limit_price;    // 64..72   0 = market
uint64_t created_ts_ns;  // 72..80
```

*(5 bytes of padding after `tif`.)*

A signal is a **proposal**, not a commitment. It is what the strategy wants. Nothing has been
approved, no order exists, and no money is reserved.

**A signal carries no venue.** It names a canonical instrument and a quantity, and says nothing
about where it should execute. That absence is deliberate. Venue selection is an execution
decision. A strategy that genuinely needs to choose, such as a cross-venue arbitrage leg,
expresses that on the order, not on the signal.

A signal also carries no order id, for the same reason. No order exists until risk approves.

`signal_id` is the backlink the whole attribution chain hangs off: signal to order to fills to
P&L. Its top 16 bits are the `strategy_id`, so the two fields are redundant by construction.

`limit_price == 0` is the sentinel for a market order.

#### `RiskRejectMsg` — a refused signal (56 bytes)

```
uint64_t signal_id;      // 32..40
uint32_t instrument_id;  // 40..44
uint16_t reason;         // 44..46   RiskRejectReason
uint64_t rejected_ts_ns; // 48..56
```

Two bytes of padding follow `reason`.

L3 (risk) sends this back to the strategy for information and to L6 (observability) for
diagnostics. It references the signal, not an order, because no order was created.

`reason` is a code with ten values: a `None` sentinel at 0, then one value per pipeline check.
The checks are the kill switch, rate limit, instrument suspended, market-order policy, order size,
price collar, open order count, position limit, and daily loss. The code lets rejections be
counted by reason. "Most rejections are rate limits" and "most rejections are collar violations"
are different problems. The code is offset by one from the pipeline step numbers, so reason 1 is
step 0, because 0 is the sentinel. Read the mapping from the table rather than inferring it.

### Orders — L3 (risk) to L4 (OMS) and L5 (EMS)

#### `NewOrderMsg` — an approved order (88 bytes)

```
uint64_t client_order_id;  // 32..40   our id, unique system-wide
uint64_t signal_id;        // 40..48   backlink to the originating signal
uint32_t strategy_id;      // 48..52
uint32_t instrument_id;    // 52..56   canonical id
uint32_t venue_id;         // 56..60   0 = any venue, else a specific venue
uint8_t  side;             // 60..61
uint8_t  order_type;       // 61..62
uint8_t  tif;              // 62..63
int64_t  qty;              // 64..72
int64_t  price;            // 72..80   0 = market
uint64_t sent_ts_ns;       // 80..88
```

One byte of alignment padding follows `tif`, so that `qty` starts at offset 64.

`client_order_id` is our identifier for the order, generated by L3 when the signal is approved.
It is the primary key of the ledger, so order records, fills, and reconciliation all join on it.
The venue's own identifier arrives later, on the execution report, and both are tracked so that
our record and the venue's can be mapped to each other.

`signal_id` is the backlink for attribution. `strategy_id` duplicates information already encoded
in `signal_id`, deliberately. Per-strategy limits and attribution read it directly rather than
shifting an id on the hot path.

`venue_id` is how an order names its destination. `0` means "any venue, whichever gateway is
listening claims it". Any other value targets one venue. A gateway ignores orders addressed
elsewhere, so any number of gateways can subscribe to the same `orders` channel with no
coordination and no extra hop. Most strategies set `0` and never think about it. A cross-venue
strategy sets it explicitly, one value per leg.

`order_type = 1`, a **market order**, is permitted but weakly constrained. A market order has no
price, so it cannot be checked against a minimum notional, and it reserves no cash because any
reservation would be a guess. Its exposure is bounded by `max_order_qty` and by the position limit
at the next fill instead. Its true cash impact is unknown until the venue fills it.

#### `CancelOrderMsg` — a cancel request (56 bytes)

```
uint64_t client_order_id;  // 32..40
uint32_t instrument_id;    // 40..44
uint32_t venue_id;         // 44..48   0 = any
uint64_t sent_ts_ns;       // 48..56
```

**A cancel is a request, not a state change.** Sending it cancels nothing. The order moves to a
pending-cancel state, and any cash reserved against it stays reserved. Only a later execution
report from the venue releases it. The reason is a race. If the reservation were released when the
cancel was sent, while the venue had already filled the order, the ledger would have freed money
that was just spent. The venue's report is the fact. The request is not.

This puts an obligation on L5 (EMS). It must publish an outcome for every cancel it transmits,
including a cancel the venue rejects because the order is already gone.

#### `ReplaceOrderMsg` — an amendment (72 bytes)

```
uint64_t client_order_id;  // 32..40   UNCHANGED, the id of the order being amended
uint32_t instrument_id;    // 40..44
uint32_t venue_id;         // 44..48   0 = any
int64_t  new_qty;          // 48..56   absolute new total quantity, not a delta
int64_t  new_price;        // 56..64   absolute new limit price
uint64_t sent_ts_ns;       // 64..72
```

Only quantity and price are amendable. Side, instrument, and time-in-force are not, because venues
commonly model those as cancel-plus-new with a new id. Supporting them here would put venue
semantics back into the ledger.

`client_order_id` is **reused, not regenerated**. From the venue's point of view, a cancel/replace
is one order with amended terms, and the venue's own identifier does not change. Reusing our id
keeps the ledger and the venue describing the same object. A fresh id would make the ledger hold
two records where the venue holds one, and reconciliation, which compares open orders, would
report a mismatch on a correct system. A safety mechanism that fires on correct behaviour trains
its operator to ignore it.

Like a cancel, a replace is a **request**. Nothing is released until the venue confirms. Because
nothing was released, a rejected amendment needs no compensating action. The record keeps its old
terms and its old reservation.

`new_qty` and `new_price` are absolute rather than deltas, so applying the same replace twice
converges. That is what makes retrying a failed publish safe.

Two consequences are worth knowing. An amendment to a quantity at or below what has already filled
is rejected rather than silently truncated. And an amendment that increases quantity is the one
path by which a replace can raise exposure, so L4 (OMS) checks it against available budget.

### Execution — L5 (EMS) to L4 (OMS), L3 (risk), and L6

#### `ExecutionReportMsg` — what happened at the venue (104 bytes)

```
uint64_t client_order_id;    //  32..40   our order id
uint64_t exchange_order_id;  //  40..48   the venue's order id
uint32_t instrument_id;      //  48..52
uint32_t venue_id;           //  52..56   which venue this came from
uint8_t  side;               //  56..57
uint8_t  status;             //  57..58   0=new, 1=partial, 2=filled, 3=cancelled, 4=rejected
uint8_t  liquidity_flag;     //  58..59   0=maker, 1=taker, 2=unknown
int64_t  filled_qty;         //  64..72   this report's increment
int64_t  cumulative_qty;     //  72..80   total filled so far
int64_t  fill_price;         //  80..88   0 if no fill
int64_t  fee;                //  88..96   signed, NEGATIVE = a rebate to us
uint64_t exchange_ts_ns;     //  96..104
```

Five bytes of padding follow `liquidity_flag`.

**This is the message that moves money.** It is the only input that changes positions or cash in
the ledger. Everything else adjusts reservations.

`status` is the wire vocabulary for the outcome. `filled_qty` is this report's increment, such as
0.2 of a 1.0 order. `cumulative_qty` is the venue's running total for that order.

`cumulative_qty` exists to detect **duplicate fills**. The `fills` channel is at-least-once in
practice. A venue may re-send on reconnect, a gateway restart may replay recent fills, and a log
re-read after a gap may deliver a fill twice. Applying a fill accumulates cash, fees, position,
and P&L, so applying one twice fabricates money. Comparing the report's `cumulative_qty` against
the record's already-applied total makes the check a single comparison: if the cumulative total has
not advanced, apply nothing. Using the report's own `filled_qty` instead would require remembering
every increment already applied, which is unbounded state that would itself have to survive a
restart. A duplicate is not an error, because there is nothing to fix and the ledger is already
correct, but it is counted. A rising count means a venue is re-sending more than expected.

`fee` is signed, and **negative means a credit to us**, which is a maker rebate. It is the venue's
own reported number, never one derived locally, because real fees drift from published schedules
through volume tiers and promotions. A locally modelled fee is internal state that can never be
reconciled against a venue statement. `liquidity_flag` accompanies it because the amount and the
reason are different facts. `fee` says what was paid. `liquidity_flag` says whether we were
providing or taking liquidity, which is what later analysis needs to attribute cost. Whether a fee
was reported or modelled is not here. It is `MsgFlag::ModelledFill` on the header.

### Control — every process

#### `HeartbeatMsg` — liveness and identity (48 bytes)

```
uint8_t  state;          // 32..33   0=boot, 1=ready, 2=trading, 3=halted, 4=error
uint32_t ref_data_hash;  // 36..40   hash of the reference data this process loaded
uint64_t beat_ts_ns;     // 40..48
```

Three bytes of padding follow `state`.

Published periodically by every process. L6 declares a process dead after a small number of
missed beats.

`ref_data_hash` makes a heartbeat assert **identity** as well as liveness. Processes restart
independently, and the reference data (instrument definitions, tick sizes) can be edited between
restarts. If one process restarts with an edited file while the others keep running, the fleet
disagrees about something order-affecting. A gateway quantises to a different grid than everyone
else believes, and nothing errors. Comparing this hash across the fleet makes that disagreement
loud. Two different values in one running system is a config-skew condition, which closes the
trading gate exactly as an inconclusive reconciliation does.

#### `KillSwitchMsg` — an authoritative command (48 bytes)

```
uint8_t  command;        // 32..33   0=halt, 1=cancel all, 2=flatten, 3=resume
uint64_t issued_ts_ns;   // 40..48
```

Seven bytes of padding follow `command`.

This is a broadcast **command**, not a state change. Every process receives it and acts only on
the part it owns, which is why there is no target field. The four commands form a ladder.

| Command    | Effect                                                                                     |
| ---------- | ------------------------------------------------------------------------------------------ |
| Halt       | latch the breaker. No new orders are admitted, and live orders and positions are untouched |
| Cancel all | halt, then cancel every live order at its venue                                            |
| Flatten    | cancel all, then send orders to bring every position to zero                               |
| Resume     | clear the latch, if the safety gate allows                                                 |

`Flatten` is the one command that transmits new orders while the system is halted, so it is the
one control path that can create exposure rather than only reduce it. It requires an explicit
operator command, with no automatic trigger, and it is the documented exception to the closed
gate. The gate exists to stop strategies, not to trap the system in a position it must exit.

A command with no named executor silently does nothing, which is the worst property an emergency
control can have. Which layer executes each command is therefore normative, not implied.

### Reconciliation — L4 (OMS) and L5 (EMS), reported to L6

Reconciliation answers one question at restart: does the ledger agree with the venue? It is a
cold, off-path, rate-limited transaction, and it gates trading.

#### `ReconRequestMsg` — begin a pass (56 bytes)

```
uint64_t recon_id;        // 32..40   correlates this pass's results and verdicts
uint32_t venue_id;        // 40..44   0 = every venue this system trades
uint8_t  trigger;         // 44..45   0=startup, 1=operator
uint64_t requested_ts_ns; // 48..56
```

Three bytes of padding follow `trigger`.

`recon_id` correlates a pass. Results are asynchronous and rate-limited, so a late response to a
superseded pass must be discardable rather than allowed to corrupt a fresh one. Every result and
verdict echoes the id. Both triggers run the same code. The trigger affects reporting, not
behaviour.

#### `ReconResultMsg` — the venue's claim about one instrument (600 bytes)

```
uint64_t recon_id;            //  32..40
uint32_t venue_id;            //  40..44
uint32_t instrument_id;       //  44..48   canonical id
int64_t  venue_net_qty;       //  48..56   the venue's position
int64_t  venue_avg_px;        //  56..64   the venue's average cost
uint32_t venue_open_orders;   //  64..68   the venue's total open-order count
uint32_t venue_fill_count;    //  68..72   cumulative fills on this instrument
uint8_t  status;              //  72..73   0=ok, 1=rate limited, 2=unavailable
uint32_t venue_order_count;   //  76..80   ids present below
uint64_t venue_order_ids[64]; //  80..592  the venue's open-order ids, as we know them
uint64_t venue_ts_ns;         // 592..600
```

Three bytes of padding follow `status`.

**L5 (EMS) reports the venue's claim and never a comparison.** L5 does not receive the ledger's
expected state and does not compute a difference. Comparing would require L5 to understand
positions and reservations, which is accounting in the layer whose job is protocol translation.
L4 (OMS) owns the comparison because L4 owns the state being compared.

Every field is the venue's own answer, obtained however that venue permits. The contract is
venue-independent: adding a venue adds no message type, only an adapter that fills this one in.

- `venue_net_qty` and `venue_avg_px` hold the position and the cost basis. The basis is included
even though a quantity comparison would not need it, because a position can have the right
quantity and the wrong basis, which is a real P&L fault.
- `venue_fill_count` shows why a position differs. With only a quantity, "the ledger is short one
  unit the venue does not have" is ambiguous between a missed fill and a stale position, and those
  need different responses.
- `venue_open_orders` and `venue_order_ids` carry the count and the identities. A count alone
  cannot tell "the same orders" from "a different set of the same size". An order the ledger holds
  and the venue does not, and the reverse, would compare as matching if the counts happened to
  agree, which would open the gate while an unmanaged live order exists. Comparing sets is what
  makes the order-level disagreement classes detectable at all. The ids are the ledger's own
  client order ids, echoed back by the venue, so nothing venue-specific crosses this seam. The
  block has a fixed capacity. If the venue holds more open orders than fit, the claim is
  **truncated** and the instrument is inconclusive, never matched.
- `status` says whether the venue's answer is complete. **"No answer" is not "no disagreement".**
  Rate-limiting and unavailability both make the instrument inconclusive, which blocks trading
  exactly like a mismatch.

#### `ReconVerdictMsg` — the comparison's outcome (72 bytes)

```
uint64_t recon_id;        // 32..40
uint32_t venue_id;        // 40..44
uint32_t instrument_id;   // 44..48
int64_t  qty_delta;       // 48..56   local minus venue
uint32_t order_delta;     // 56..60   local minus venue
uint8_t  outcome;         // 60..61   0=matched, 1=mismatch, 2=inconclusive
uint8_t  mismatch_class;  // 61..62
uint64_t finalize_ts_ns;  // 64..72
```

Six bytes of padding follow `mismatch_class`.

One message per (venue, instrument) per pass. The six disagreement classes are separate because
they have different causes, severities, and resolutions, and the class is the only diagnostic an
operator gets.

| Class               | Meaning                                                            | Severity and resolution                               |
| ------------------- | ------------------------------------------------------------------ | ----------------------------------------------------- |
| Position local only | the ledger holds a position the venue does not                     | serious. Adopt the venue's number                     |
| Position venue only | the venue holds a position the ledger does not                     | critical. Adopt the venue's number                    |
| Order local only    | the ledger shows an open order the venue does not                  | benign. Mark it abandoned and release the reservation |
| Order venue only    | the venue has an order the ledger never booked                     | critical. **Blocks trading.** A human decides         |
| Basis delta         | quantities agree, cost basis does not                              | serious. Adopt the venue's basis                      |
| Fill count delta    | the position differs and the fill count confirms fills were missed | critical. Replay the venue's fills, then re-check     |

The gate: an instrument counts as matched only if an explicit `ok` result arrived and the
comparison agreed. A fully matched pass reopens trading automatically. Anything else, which is a
mismatch, an inconclusive result, a truncated claim, or a config skew, closes the gate and waits
for a human. The human resolves the classes above and then triggers a fresh pass to prove the fix
landed. Clearing the gate on the strength of an edited ledger, without re-asking the venue, would
trust the state that was just shown to be wrong.

### Ledger adjustments — L4 (OMS) to the recorder and L6

Three messages carry the ledger state changes that have **no event behind them**. The ledger's
central rule is that state is derived from the event stream, which is what makes replay rebuild an
identical state. Two specified mutations are not on that stream:

- the off-path sweep that abandons an order whose cancel report never arrived, triggered by a
  wall-clock timeout, and
- the corrections reconciliation applies, such as adopting the venue's position or basis.

Without a record of these, replay would produce a different ledger than what actually happened.
They are published on the `ledger` channel, which the recorder subscribes to.

**The ordering is the guarantee.** L4 publishes the adjustment before applying it, and does not
apply it if the publish fails. Applying first and logging afterwards would leave a window in which
a crash changes state the log never saw, and replay would then diverge silently. That is the one
failure this mechanism exists to prevent. Because the operations are assignments or terminal
transitions rather than accumulations, applying one twice converges, so a replay that sees a
duplicate is harmless. `adjustment_id` is monotonic per producer, so a duplicate or out-of-order
delivery is detectable.

#### `OrderAbandonedMsg` (64 bytes)

```
uint64_t adjustment_id;   // 32..40   monotonic per producer
uint64_t client_order_id; // 40..48   the order being abandoned
uint32_t instrument_id;   // 48..52
uint32_t venue_id;        // 52..56   0 if it never reached a venue
uint64_t applied_ts_ns;   // 56..64
```

L4 emits this when the sweep times out, or when reconciliation finds an order the venue does not
have. It releases the order's reservation.

#### `PositionAdoptedMsg` (72 bytes)

```
uint64_t adjustment_id;   // 32..40
uint64_t recon_id;        // 40..48   the reconciliation pass that caused it
uint32_t instrument_id;   // 48..52
uint32_t venue_id;        // 52..56
int64_t  new_net_qty;     // 56..64   the adopted position
uint64_t applied_ts_ns;   // 64..72
```

#### `BasisAdoptedMsg` (72 bytes)

```
uint64_t adjustment_id;   // 32..40
uint64_t recon_id;        // 40..48
uint32_t instrument_id;   // 48..52
uint32_t venue_id;        // 52..56
int64_t  new_avg_entry;   // 56..64   the adopted cost basis
uint64_t applied_ts_ns;   // 64..72
```

Three messages replace one message with a discriminator so that every field is meaningful in
every message. A single message carrying all three operations would have fields that are only
sometimes live, and a reader would have to consult the discriminator to know which. The two
adoptions carry `recon_id`, so an audit can trace "why did my position change?" to the pass that
said so.

---

## Shared vocabulary

These are the enumerations that appear in message bodies. Each one's underlying type matches the
width of the field that carries it, so the values can be read directly from the bytes.

```cpp
enum class Side : uint8_t        { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t   { Limit = 0, Market = 1 };
enum class TimeInForce : uint8_t { GTC = 0, IOC = 1, FOK = 2 };

enum class OrderStatus : uint8_t {
    New = 0, Partial = 1, Filled = 2, Cancelled = 3, Rejected = 4,
};

enum class ProcessState : uint8_t {
    Boot = 0, Ready = 1, Trading = 2, Halted = 3, Error = 4,
};

enum class KillCommand : uint8_t {
    Halt = 0, CancelAll = 1, Flatten = 2, Resume = 3,
};
```

### Reconciliation

```cpp
// Only Mismatch and Inconclusive keep the trading gate closed.
enum class ReconOutcome : uint8_t { Matched = 0, Mismatch = 1, Inconclusive = 2 };

enum class ReconMismatch : uint8_t {
    None = 0,
    PositionLocalOnly = 1,
    PositionVenueOnly = 2,
    OrderLocalOnly = 3,
    OrderVenueOnly = 4,
    BasisDelta = 5,
    FillCountDelta = 6,
};

enum class ReconStatus : uint8_t { Ok = 0, RateLimited = 1, VenueUnavailable = 2 };

// Which side started a pass. Affects reporting, not behaviour: both triggers run
// the same code path.
enum class ReconTrigger : uint8_t { Startup = 0, Operator = 1 };
```

The six `ReconMismatch` values, their causes, and their resolutions are in the
`ReconVerdictMsg` section above.

A truncated venue claim is not a `ReconStatus` value. The venue reports its total open-order count
in `venue_open_orders`, and if fewer order ids follow, the claim is incomplete. L4 (OMS) treats
that as inconclusive, the same as a rate limit.

### Venue identity

`VenueId` is a `uint32_t`, matching the `venue_id` field width in the order, fill, and
reconciliation messages.

```cpp
enum class VenueId : uint32_t {
    Any = 0,        // no preference: whichever gateway receives the order claims it
    // 1 to 99 are reserved. A stale config or a truncated read must not resolve
    // to a live venue.
    Binance = 100,  // convenience alias. The authority is config/gateways/binance.yaml
};
```

**This enum is not the registry of venues.** A venue's id is declared and validated in its config
file, so that adding a venue does not change the venue-agnostic layers. Only `Any` is
load-bearing, because the routing check needs one value every layer agrees on. The named constants
are conveniences for the adapters that exist, and L2 (strategy), L3 (risk), and L4 (OMS) must
never switch on them. Doing so is the coupling the config-declared id exists to prevent. A venue
whose adapter did not exist at compile time takes the next free number, and no C++ changes.

### Pre-trade rejection reasons

`RiskRejectReason` is a `uint16_t`, matching the `reason` field width in `RiskRejectMsg`.

```cpp
enum class RiskRejectReason : uint16_t {
    // Sentinel. No pipeline check maps to 0, so a zero-initialized or truncated
    // RiskRejectMsg can never be read as a real verdict.
    None = 0,

    KillSwitchActive      = 1,  // check 0
    RateLimitExceeded     = 2,  // check 1
    InstrumentHalted      = 3,  // check 2: the canonical suspension set
    MarketOrderNotAllowed = 4,  // check 3
    OrderSizeTooLarge     = 5,  // check 4
    PriceCollarViolation  = 6,  // check 5
    MaxOpenOrdersExceeded = 7,  // check 6
    PositionLimitExceeded = 8,  // check 7
    DailyLossExceeded     = 9,  // check 8
};
```

The values follow the pipeline order, not the alphabet. A reason code and a pipeline position
describe the same fact, so they must not drift apart. The code is offset by one from the step
number because 0 is the sentinel, so `reason = 5` means the order-size check failed, which is step
4. The checks themselves are in `layers.md`, L3.

`InstrumentHalted` comes from a canonical suspension set, not from a venue listing's `status`.
L3 (risk) has no venue to look up, so a venue halting one listing is an L5 (EMS) matter that
returns as an execution report, and an operator halting the asset fleet-wide is what sets this
reason.

---

## Publication failure is per-channel

The transport is bounded. A full publication buffer, or a channel with no connected subscriber,
makes the publish return an error and deliver nothing. It does not block and does not retry, and
no hot thread may block. Every publish site therefore declares what it does when a publish fails,
and the right answer differs per channel, because the cost of losing a message is not the same
everywhere.

| Channel       | On a full buffer                                              | Why                                                                                                                                                        |
| ------------- | ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `md`          | never blocks. A persistently full window is a fatal condition | market data is high-volume and self-healing, because a missed delta triggers a resync. Retrying a stale quote would publish a price that is no longer true |
| `signals`     | drop, count, and surface                                      | a signal is a lost opportunity, not lost state. Losing it costs a trade. Blocking the signal thread costs every trade, including exits                     |
| `orders`      | bounded retry, then **latch the kill switch**                 | a dropped order is a position the ledger never learns about. An order is a commitment                                                                      |
| `fills`       | never drops. A full window is a fatal process error           | a dropped fill silently corrupts positions and P&L                                                                                                         |
| `risk_events` | drop and count                                                | informational. Losing a rejection notice cannot change state                                                                                               |
| `control`     | retry until delivered                                         | liveness is the one signal whose absence triggers action. A dropped heartbeat makes a healthy process look dead                                            |
| `recon`       | handled by the reconciliation state machine                   | not a hot-path channel. "No answer" already has a designed outcome                                                                                         |
| `ledger`      | publish before apply, retry until delivered                   | an adjustment that is applied but not logged is state replay cannot reproduce                                                                              |

Two invariants make this more than a list of guesses.

1. **No droppable channel mutates the ledger.** `orders`, `fills`, and `ledger` change state, and
   none is best-effort. Everything droppable carries self-healing data (`md`), a proposal rather
   than a commitment (`signals`), or a derived view (`risk_events`, `telemetry`). This is why
   signals and orders are separate channels at all: **intent can be lost, and a commitment
   cannot.**
2. **Every drop is counted and observable.** A silent drop is indistinguishable from an absence of
   events, which is the same ambiguity the sequence-number machinery exists to eliminate.

A *full buffer* and a *disconnected subscriber* are different failures with different responses:
the first is backpressure, the second is a reconnection or supervision event. Likewise, an order
throttled by a *venue's* own rate limit is not a rejection. It already passed risk and is in the
ledger, so it is retried rather than discarded.
