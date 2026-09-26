# Gateway configuration

One file per gateway **process**. Each file describes exactly one venue and is
loaded by a single Layer 5 (EMS) instance via `--config`.

Layer legend (overview.md, "The channels"): L1 = ingestion (feed handler), L2 = strategy,
L3 = risk (firewall), L4 = OMS (ledger), L5 = EMS (execution), L6 = observability.

```
config/gateways/
  binance.yaml      # venue: binance, producer_id: 5, core: 5
  README.md
  # coinbase.yaml   # venue: coinbase, producer_id: 256, core: 8  (when the adapter exists)
```

## Why one file, not one registry

A single `gateways.yaml` holding every venue would be a registry, and that
implies one process loads it and dispatches per venue. We deliberately do not
do that. Reasons, in order of consequence:

1. **Fault isolation.** If one process multiplexes Binance and Coinbase on one
   event loop, a hung Coinbase socket, a slow TLS handshake, or a FIX session
   timeout blocks the loop and therefore blocks Binance order flow too. Separate
   processes make the failure domains separate: Coinbase can be down while
   Binance keeps trading.

2. **Core pinning.** Each gateway is pinned to its own isolated core
   (`core:` in the file, `CPUAffinity=` in its systemd unit). Multiplexing two
   venues on one core means two independent protocol state machines, two
   heartbeat schedules and two rate limiters sharing one core's cache.

3. **Independent rate limits and reconnection.** Venues have different order
   rate limits, heartbeat intervals and reconnect policies. Per-venue files let
   each drift without disturbing the other.

4. **One process, one config** is the same rule already used by
   `deploy/systemd/` (one unit per process). This directory keeps the two
   consistent.

## Adding a venue

1. Add the venue's listing to the canonical instrument in
   `config/instruments.yaml` under `venues:<name>`. That is where the tick size,
   step size and min notional live — never here, because they are properties of
   the venue listing rather than the asset (operations.md, "Reference data").

2. Copy `binance.yaml` to `<venue>.yaml` and change:
   - `venue` and `kind`
   - `venue_id` — the venue's numeric identity on the wire (contracts.md, "Venue identity").
     Write it down here; this file is the authority for the number, and the same
     value must appear in `config/feeds/<venue>.yaml`. `0` is the wildcard and
     `1..99` are reserved, so start at `100`.
   - `rest.base_url`, `websocket.market_data_url`, `websocket.order_url`
   - `credentials.api_key_env` / `api_secret_env` — the *names* of environment
     variables, never the values
   - `producer_id` — must be unique per process (contracts.md, "The message header"). Binance EMS
     is 5; a second venue's **gateway** takes a value from the 256–299 range
     (feed handlers use 200–255), or sequence-gap detection becomes ambiguous.
     The ranges are separate because L1 (ingestion) and L5 (EMS) are different crash domains.
   - `core` — must be unique across running processes and present in the
     kernel's `isolcpus` list. Cores 1-6 are reserved for the hot path in the
     reference layout (overview.md, "More than six processes"); additional venues go to 8+.
   - `venue_rate_limit` — set to the new venue's published limits, which set a
     floor under Layer 3's internal limit in `risk_limits.yaml`.

3. Add a matching systemd unit under `deploy/systemd/`.

## What "no C++ changes" does and does not mean

Be precise here, because the loose version of this claim is false and it matters:

- **Adding a venue needs new C++ in L1 (ingestion) and L5 (EMS).** A venue is reached through a
  protocol — REST/WebSocket payloads, FIX tags, signing, heartbeat schedule — and
  that code is venue-specific by design (operations.md, "Reference data"). There is no config
  switch that teaches a process a new exchange protocol. So the honest statement
  is *not* "zero C++", and the steps above assume the `<venue>` adapter already
  exists.
- **Adding a venue must add no new message types, and must require no changes to
  the venue-agnostic layers — L2 strategy, L3 risk, L4 OMS.** This is the claim
  that holds and the one worth defending: those layers move canonical instruments
  and opaque venue ids, so a new venue appears to them as new data, never as new
  code. A strategy, the risk engine, and the ledger all handle a venue that did
  not exist when they were compiled — which is why the venue's numeric id is
  declared in config rather than in a C++ enum (contracts.md, "Venue identity").

If you ever find yourself editing `core/` or `contracts/` to add a venue, the
boundary has leaked: the change belongs in L1/L5 or in `config/`.

## What the venue boundary does and does not mean

Worth being precise, because it is easy to over-read:

- **Venue-agnostic** describes the *strategy algorithm and the risk engine*. A
  strategy emits "buy 0.5 of canonical 1001" and never names a venue. Venue
  selection is an execution decision.
- It does **not** mean the system is blind to price differences. Venues quote
  genuinely different prices for the same asset at the same instant, and that
  spread is the basis of cross-venue arbitrage.

There are two distinct strategy families, and both are supported:

### Single-venue strategies

The common case. The strategy process subscribes to one venue's feed and emits
orders for canonical ids it has been configured with. Which venue that resolves
to is fixed by deployment (this directory), not by the strategy's code. Pointing
the process at a different gateway file is the only change required to run the
same compiled strategy against a different exchange.

### Multi-venue strategies (arbitrage, smart routing)

A cross-venue strategy is explicitly **not** venue-agnostic, by design:

- It subscribes to *multiple* feed handlers at once — e.g. market data published
  by both the Binance and Coinbase feed handlers. All feed handlers publish to the
  same `md` channel and `MsgHeader.producer_id` identifies which venue a message
  came from, so no per-venue channel naming is needed (overview.md, "The channels").
- It maintains a separate book per venue, because it must compare them:
  arbitrage *is* the relationship between two specific venues.
- It emits orders that explicitly name the destination venue, so the correct
  gateway picks each one up. The mechanism is the `venue_id` field on
  `NewOrderMsg` (contracts.md, `NewOrderMsg`): each gateway drops orders addressed
  elsewhere, so several can share the `orders` channel with no coordination.

The reason the design does not force venue-awareness into the strategy interface
is that most strategies do not need it, and those that do can subscribe to
whatever feeds they want. Note the two directions are handled differently, and
deliberately so: **ingress is multi-producer** (many feed handlers share `md`,
disambiguated by `producer_id`) while **egress is single-destination** (one order
goes to exactly one venue, named by `venue_id`).

The Instrument Master makes this work: a canonical id with two venue listings is
exactly what tells a multi-venue strategy "this asset is reachable on two venues,
here are both order books' worth of feeds to subscribe to."

## Credentials

Never place secrets in these files. Each file names the environment variables
that hold them (`api_key_env`), and the values are injected at runtime by
systemd's `EnvironmentFile=` or a secret manager (Investigation §16). This keeps
config versioned while keeping secrets out of git.

## Modelled fees (simulated venues only)

A live venue reports the fee it charged on every execution report, and that
reported value is authoritative (contracts.md, `ExecutionReportMsg`) — nothing in this directory
is read for a live fill.

A **simulated** venue has no exchange to report a fee, so its gateway models one.
That schedule belongs here, in the simulated venue's own file, rather than in a
shared `config/fees/` table:

- A central fee table would be readable by Layer 4, and Layer 4 must be unable to
  derive a fee. A modelled fee is internal state that cannot reconcile against an
  exchange statement, so the design deliberately makes it awkward to reach.
- It mirrors the existing rule that venue micro-structure lives in the venue
  listing, never on the canonical instrument (operations.md, "Reference data").

A fill whose fee came from a model is marked with `MsgFlag::ModelledFill`
(contracts.md, the `flags` field), so a backtest's costs can never be mistaken for venue truth
downstream.
