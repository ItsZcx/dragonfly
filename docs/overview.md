# Overview: the runtime picture

Dragonfly runs as a set of independent processes that talk over a message bus. Each process owns
one stage of the path from market data to executed trade. Money and state cross process
boundaries only as fixed-size messages (PODs).

## Contents

- [Layers are roles, not processes](#layers-are-roles-not-processes)
  - [The boundaries are the design](#the-boundaries-are-the-design)
- [The channels](#the-channels)
- [One tick, end to end](#one-tick-end-to-end)
- [What is on the hot path](#what-is-on-the-hot-path)

---

## Layers are roles, not processes

Dragonfly has six layers. A layer is a role, not a process. Some layers run as one process, and
others run as several. Market data ingestion runs one feed handler per venue, execution runs one
gateway per venue, and an isolated strategy runs in its own process. What makes something a layer
is the job it does.

| Layer  | Name          | Job                                                          | Language     |
| ------ | ------------- | ------------------------------------------------------------ | ------------ |
| **L1** | Ingestion     | Turns venue data into normalized messages and keeps the book | C++          |
| **L2** | Strategy      | Turns market observations into trading intentions            | C++ / Python |
| **L3** | Risk          | Approves or rejects each intention                           | C++          |
| **L4** | OMS           | Records positions, cash, orders, and P&L                     | C++          |
| **L5** | EMS           | Sends orders to the venue and reports the outcomes           | C++          |
| **L6** | Observability | Shows what is happening and lets a human stop it             | Python / TBD       |

The core processes are one feed handler per venue, one strategy process, the risk engine, the OMS, one gateway per venue, and the monitor. That strategy process can be one process running multiple strategies or a process per strategy, it depends on configuration.

Four more processes appear as later phases need them. The **telemetry process** summarizes bus
messages for the monitor and arrives with it. The **audit recorder** writes the durable binary log
that recovery replays. The **replay harness** feeds historical data through the same strategy,
risk, OMS, and gateway code as live trading. The **media driver** owns the shared-memory buffers
that every other process publishes into, and starts before them.

Information travels one way:

```
L1 (ingestion) → L2 (strategy) → L3 (risk) → L4 (OMS) / L5 (EMS) → venue
```

Each layer adds one thing and hands off.

These documents write a layer as `L<n>` with its name in parentheses on first use in a section: `L3 (risk)`, `L5 (EMS)`.


### The boundaries are the design

Each layer's job is defined as much by what it may not do. These limits are what let a second
venue be added without changing the core.

- **L1 (ingestion)** never decides anything, and never passes venue-specific information
  downstream. After L1, no venue symbol string exists anywhere in the system.
- **L2 (strategy)** never manages risk or capital. It proposes. It does not commit.
- **L3 (risk)** never touches a venue. It checks limits on the canonical instrument only, because
  a signal carries no venue to look up.
- **L4 (OMS)** never makes trading decisions and never contacts a venue. It records what
  happened.
- **L5 (EMS)** never does accounting and never chooses which venue to trade. It translates and
  reports.
- **L6 (observability)** never sits on the critical path. It may be slow, and it may die.

## The channels

Processes communicate only over named message streams, called channels. A channel is a transport,
not an owner: several processes may publish to one channel, and `producer_id` in the message
header tells the reader which one sent it.

| Channel       | Direction                     | Carries                      | Read by                   |
| ------------- | ----------------------------- | ---------------------------- | ------------------------- |
| `md`          | L1 → L2, L3, L6               | market data                  | strategies, risk, monitor |
| `signals`     | L2 → L3                       | trading intentions           | risk                      |
| `orders`      | L3 and L4 → L4, L5            | new orders, cancels, amends  | OMS, gateways             |
| `fills`       | L5 → L4, L3, L6               | execution outcomes           | OMS, risk, monitor        |
| `risk_events` | L3 → L2, L6                   | rejections                   | strategies, monitor       |
| `control`     | every process ↔ every process | heartbeats, kill switch      | every process             |
| `recon`       | L4, L6 ⇄ L5 and L4 → L6       | reconciliation               | OMS, gateways, monitor    |
| `ledger`      | L4 → recorder, L6             | ledger changes with no event | recorder, monitor         |

Four properties of this set matter.

Data flows downstream, with two exceptions. `control` and `recon` are the only channels that carry
messages backwards or in both directions.

Inbound market data is multi-producer. Outbound orders are single-destination. Several feed
handlers publish to `md`, and `producer_id` distinguishes them, which is what lets one strategy
compare two venues. A single order, by contrast, must reach exactly one venue, so the order itself
carries a venue id rather than relying on the channel. With the wildcard id, that holds only while
one gateway subscribes to `orders`, and the two-gateway case is open in `open-questions.md`.

Every process both publishes and subscribes to `control`. It carries liveness, the kill switch,
and the one request that travels from a consumer back to a producer.

Each channel has a defined behaviour when its buffer is full. The rule that matters most: **no
channel that changes the ledger may drop a message.** `contracts.md` gives the per-channel
behaviour.

## One tick, end to end

This is the shortest complete description of the system running.

1. A venue sends a trade over its socket.
2. **L1 (ingestion)** parses it, maps the venue's symbol to a canonical instrument id, records
   both the venue's timestamp and its own monotonic receive time, and publishes a `TradeMsg` on
   `md`.
3. **L2 (strategy)** reads the trade, updates its state, decides, and publishes an `AlphaSignalMsg`
   on `signals`, for example "buy 0.5 of instrument 1001".
4. **L3 (risk)** reads the signal and runs a fixed sequence of checks. If all pass, it publishes a
   `NewOrderMsg` on `orders`. If one fails, it publishes a `RiskRejectMsg` on `risk_events`, and
   the signal stops there.
5. **L4 (OMS)** records the order and reserves cash against it. **L5 (EMS)** takes the same order,
   snaps its price and quantity to the venue's grid, and sends it.
6. The venue acknowledges and fills. L5 translates each outcome into an `ExecutionReportMsg` on
   `fills`.
7. L4 applies each fill: it moves cash, updates the position, and computes realised P&L. L3
   applies the same fill to its own position mirror so the next check is accurate.
8. **L6 (observability)** displays it.

Reconciliation, snapshots, replay, and the kill switch all address what happens when one of these
steps fails.

## What is on the hot path

Each process has one hot thread: the pinned, single-threaded loop that handles ticks and orders.
On that thread, four things are forbidden:

- allocation,
- disk I/O,
- any blocking call,
- the system wall clock.

Everything that would break those rules runs on an off-path thread or in a separate process: log
writing, snapshots, the reconciliation comparison, the timeout that abandons stale orders, and the
entire monitor.
