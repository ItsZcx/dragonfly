# Quantitative Development Investigation & Architecture Notes

<details>
<summary>Contents</summary>

- [1. Purpose and how to read this](#1-purpose--how-to-read-this)
- [2. System topology and IPC](#2-system-topology--ipc-zeromq-vs-aeron)
- [3. The 6-layer deep architecture](#3-the-6-layer-deep-architecture-agnostic--polyglot)
- [4. Modern C++ focus areas](#4-modern-c-focus-areas-for-high-performance-systems)
- [5. Market microstructure and asset classes](#5-market-microstructure--asset-classes)
- [6. Data sourcing and storage](#6-data-sourcing--storage-quant-research-infrastructure)
- [7. Infrastructure and deployment](#7-infrastructure--deployment-production-parity--low-latency)
- [8. The quant lifecycle](#8-the-quant-lifecycle-from-alpha-idea-to-live-pl)
- [9. Professional tech stack and toolchain](#9-professional-tech-stack--toolchain)
- [10. Professional dependencies and libraries](#10-professional-dependencies--libraries-institution-standards)
- [11. Low-level implementation realities](#11-low-level-implementation-realities-the-invisible-toolkit)
- [12. Domain 1: the core execution pipeline](#12-domain-1-the-core-execution-pipeline-the-hot-path-anatomy)
- [13. Domain 2: state management and crash recovery](#13-domain-2-state-management--crash-recovery)
- [14. Domain 3: configuration and reference data](#14-domain-3-configuration-instrument-master--reference-data)
- [15. Domain 4: simulation, replay and backtesting](#15-domain-4-simulation-replay--backtesting-harness)
- [16. Domain 5: deployment, CI/CD and operations](#16-domain-5-deployment-cicd--operational-tooling)

</details>

## 1. Purpose & How To Read This

This is the *why* document. It records the domain research behind the architecture:
the landscape, the technology choices and the reasoning that produced them, and the
operational realities each layer has to survive.

- **This document explains rationale.** It records why the system is the way it is: the domain
  research, the technology choices, and the operational realities each layer has to survive. The
  specification itself lives in the documentation set: `overview.md` for the runtime picture,
  `contracts.md` for the message formats, `layers.md` for per-layer behaviour, and `operations.md`
  for configuration and phases. Where this document and those disagree, those win and this
  document is out of date.
- **Sections are cited from elsewhere as `Investigation §N`**, to distinguish them from
  references into the documentation set, which use section names.
- **Design decisions that were settled** are recorded inline near the topic they affect, marked
  *Resolved* / *Decision*, rather than collected in a changelog. Many of those notes cite the old
  numbered specification; treat the name as the pointer, not the number.

Scope: systems engineering for a single-box, low-latency trading system. It is not a
market-prediction text; alpha research is out of scope by design (see Investigation §8, the quant
lifecycle, for where research sits relative to this system).

## 2. System Topology & IPC (ZeroMQ vs. Aeron)
- **ZeroMQ:** Socket abstraction over TCP/domain sockets. Allocates memory dynamically, uses OS networking stacks. Prone to microsecond-scale jitter and unbounded buffering (risk of OOM if subscribers lag).
- **Aeron (Chosen Standard):** Lock-free ring buffers over shared memory (IPC) or UDP. Zero-allocation on the hot path, bounded predictable latency, and strict window-based backpressure.
- **Backpressure Handling:** 
  - ZMQ: Silently buffers until memory exhausts (OOM) or latency explodes.
  - Aeron: Exposes consumer lag explicitly via tracking windows, allowing deterministic decisions (drop stale frames, trigger circuit breakers, or shed load).

## 3. The 6-Layer Deep Architecture (Agnostic & Polyglot)
- **Layer 1: Ingestion & Normalization [C++]:** Zero-copy binary parsing, fixed-size POD (Plain Old Data) structs, **nanosecond monotonic** timestamping (`clock_gettime(CLOCK_MONOTONIC)`; the venue's own timestamp is preserved separately, `contracts.md`, "Timestamps"), publishing over Aeron.
- **Layer 2: Alpha / Strategy Engine [Python / C++ Hybrid]:** Plugin architecture. Python for medium/low-frequency ML/statistical models; C++ for HFT threshold/pricing loops. Communicates via shared memory/Aeron without GIL bottlenecks on the hot path. *(Resolved in layers.md, L2 (strategy): "without GIL bottlenecks" is achieved by **not having an interpreter in the hot-path process** — the Python strategy runs as its own Aeron peer, never embedded via a pybind11 plugin. The pybind11 bridge is a codec **library used by that peer**, not code loaded into the engine. The separate-process decision was correct, and the earlier phrasing that implied an in-process module was wrong. The contradiction is now closed.)*
  - *Two families of strategy, and the distinction is which feeds they subscribe to:*
    - **Single-venue (the common case).** The process is wired by configuration to one venue's feed and one venue's gateway. To that strategy its venue *is* the universe. It emits "buy 0.5 of canonical `1001`" and never names a venue. Because the feed is normalized at L1 (ingestion), the same compiled strategy runs against Binance or Coinbase purely by changing deployment config.
    - **Multi-venue (arbitrage, smart routing).** Explicitly *not* venue-agnostic. The process subscribes to several feed handlers at once and keeps one book per venue, because the trade *is* the relationship between those venues. **The canonical analogy is a sports-betting arbitrageur comparing two books:** DraftKings has Team A at +150, FanDuel at +160, and neither site is an opportunity on its own — the gap between them is. Likewise a crypto arbitrage strategy holds `book_binance` and `book_coinbase` side by side and fires a paired order when `best_bid(coinbase) > best_ask(binance)`.
      - *The deep reason this must be venue-aware:* `best_bid(coinbase) > best_ask(binance)` is a state that **cannot be observed from either venue's feed alone**. It exists only in the relationship. No amount of normalization can express it on a single canonical stream, so the strategy must consume both — which is why "venue-agnostic" describes the *algorithm's* lack of venue-specific code, not a claim that all venues are interchangeable.
      - *Venue-aware by necessity:* this family's second leg names its destination via the `venue_id` field on the order (contracts.md, `NewOrderMsg`), so leg 1 can go to Binance and leg 2 to Coinbase through the same message bus.
  - *Why "venue-agnostic" is not the same as "blind to venue differences":* venues quote genuinely different prices for the same asset at the same instant. Venue-agnosticism means the *algorithm* carries no venue-specific code (no FIX tags, no exchange JSON, no tick size in its math), not that the system hides cross-venue price differences. Most strategies don't need them; the ones that do subscribe to more feeds.
  - *Why this keeps risk limits honest:* because positions and P&L aggregate on the canonical id, one `max_position_qty` covers the asset across every venue. If each venue had its own id, that cap would silently become per-venue and the effective limit would multiply by venue count.
- **Layer 3: Risk & Compliance Engine [C++]:** Pure deterministic bouncer/firewall on the hot path. Enforces mandatory pre-trade risk checks before signals reach OMS/EMS, in the strict pipeline order fixed by **layers.md, L3 (risk)**: global circuit breakers, rate limiting (token bucket), instrument status, market-order policy, order size, price collars (fat-finger protection), open-order count, position/inventory caps, and daily loss. Fails fast, completely decoupled from Layer 2 logic.
  - *Fast-Fail Ordering & Latency Cost:* Ordered strictly from cheapest to most complex to minimize CPU cycles. 
    1. CPU Register flags (Global kill switch: ~1 cycle).
    2. Integer math (Rate limits / token bucket: ~2-5 cycles).
    3. Primitive comparisons (Order size / price collars: ~5-10 cycles).
    4. State lookup (Position limits via flat array index: ~10-30 cycles).
  - *Branch Prediction:* Uses compiler branch hints (`unlikely`) so happy-path execution incurs zero branch penalty. Avoids heap-allocated maps (`std::map`) for position lookups in favor of flat arrays (`std::vector`) indexed by instrument ID for cache locality.
- **Layer 4: Portfolio OMS [C++]:** Internal accounting and state ledger. Tracks cash balances, open positions across instruments, and unrealized/realized P&L. Uses double-entry bookkeeping (every transaction records exact debits/credits) ensuring complete replay determinism from event logs. *(Resolved in contracts.md, `ExecutionReportMsg`: replay determinism needs the fill stream to be **at-least-once**, because a venue redelivery on a live socket, an L5 (EMS) restart replaying recent fills, and post-`SeqGap` log re-reading all deliver the same report twice. `OnFill` accumulates unconditionally, so a duplicate is fabricated money and a fabricated position. L4 (OMS) therefore applies a fill only when the report's `cumulative_qty` advances past its own `filled_qty` — a comparison against one counter it already maintains, needing no applied-report history and surviving restart. A duplicate is a no-op, counted in telemetry rather than raised as an error.)*
  - *State model resolved in layers.md, L4 (OMS):* `Position` (per canonical id: signed `net_qty`, **unsigned-magnitude** `avg_entry_price`, `realized_pnl`, `fees_paid`, `mark_price`), `CashBalance` (per settlement currency: `settled`, `reserved`; available is derived, never stored), and `OrderRecord`. Update rules are in `layers.md`, L4 (OMS).
  - *The one arithmetic rule that bites (`contracts.md`, "Numbers are fixed-point integers"):* a product of two `SCALE`-scaled values overflows `int64_t` — `1.0 BTC × $100k` is `1e23` against a `9.22e18` ceiling. All notional, P&L and reservation products widen to `__int128` before dividing.
  - *Cancel semantics (option A):* a `CancelOrderMsg` is a **request**. L4 moves the order to `PendingCancel` and releases nothing; only a terminal `ExecutionReportMsg` releases the reservation (`layers.md`, L4 (OMS)). This makes a venue-rejected cancel resolve correctly instead of leaking capital, at the cost of requiring L5 to publish an outcome for every cancel it transmits.
  - *OMS vs. EMS Separation:* OMS handles internal state, portfolio accounting, and capital allocation. EMS (Layer 5) handles external exchange connectivity, order lifecycle routing, and network protocol translation.
  - *OMS vs. Layer 3 Risk Boundary:* Layer 4 (OMS) calculates dynamic capital allocations and position budgets in the background. Layer 3 (Risk Firewall) enforces those budgets as fast, binary pre-trade constraints on the hot execution path without performing slow accounting calculations. *(Open: the L4→L3 budget message is still undesigned. It is recorded in `open-questions.md`.)*
- **Layer 5: Execution EMS [C++]:** External exchange connectivity and order lifecycle management. Translates internal standardized orders into venue-specific protocols (FIX Protocol, custom WebSockets, binary gateways). Manages the full order lifecycle (`New`, `Partially Filled`, `Filled`, `Cancelled`, `Rejected`), network timeouts, re-transmissions, rate limits, and parent/child order slicing (VWAP/TWAP/Icebergs). **Also the single quantization boundary:** the requested price and quantity are snapped to the target venue's tick size and lot step immediately before the venue order is emitted, and `min_notional` is enforced there. This is the only place venue micro-structure precision is applied — Layer 3 validates against canonical limits and never needs a venue, which is what keeps signals venue-agnostic.
- **Layer 6: Observability & Monitoring [Python / TUI]:** Passive control room. Subscribes passively to telemetry, fills, and heartbeats over Aeron without slowing down the hot execution path. Monitors real-time P&L, inventory drift, and p99/p99.9 latency telemetry. Hosts the emergency **Kill-Switch** (broadcasting instant cancel-all and position-flattening commands across the bus).

## 4. Modern C++ Focus Areas for High-Performance Systems
- **Event Loops & Concurrency:**
  - *The Anti-Pattern:* Traditional thread pools and `std::mutex` locks. Mutex contention causes OS context switches (1,000–10,000 ns), blowing tight latency budgets and destroying CPU cache lines.
  - *The HFT Standard:* Single-threaded, pinned-core event loops (`pthread_setaffinity_np`) communicating via lock-free ring buffers (Aeron/shared memory). Zero mutex contention, maximum L1/L2 cache locality, absolute FIFO determinism. Non-blocking design is mandatory.
- **Time Management & Determinism:**
  - *Wall-Clock vs. Monotonic:* Never use `std::chrono::system_clock` for intervals (vulnerable to NTP/system clock jumps). Use `clock_gettime(CLOCK_MONOTONIC)`. *(Resolved in contracts.md, "Timestamps": raw `RDTSC` is **not** an equal alternative for any cross-process timestamp. It is per-core, and a restart can land on a different core with a different TSC offset, making the value non-monotonic across the restart boundary — which breaks the restart detection described in `contracts.md`, "Sequence numbers across a restart". TSC is permitted only as an internal latency-measurement optimization.)*
  - *The Clock Provider Pattern:* Abstract time behind an `IClock` interface. `LiveClock` reads monotonic/packet timestamps; `SimulationClock` is driven deterministically by historical data replay. Allows identical strategy code to run in backtest and live production.
- **Deterministic Replay & Backtesting:**
  - *Production-Parity Backtesting:* Avoid disconnected research-only Python/Pandas backtesters for production validation. Instead, use a dual-source ingestion adapter in Layer 1: live sockets vs. historical PCAP/binary log replay.
  - *Event-Driven Fill Simulation:* Backtests must simulate real market friction—queue priority in the limit order book, partial fills, round-trip latency, and cancellation delays—rather than magical instantaneous fills.
  - *Audit Logging & Post-Mortems:* Record every packet, signal, risk decision, and order event to an append-only binary log with nanosecond timestamps for step-through debugging and failure analysis.
- **Zero-Copy & Memory:** `std::string_view`, `std::span`, placement new, avoiding heap allocations (`new`/`delete`) on the hot path.
- **Cache Locality:** Utilizing contiguous memory (`std::vector`, flat pools) instead of pointer-chasing structures (`std::list`, node-based trees) to eliminate CPU cache misses.
- **Concurrency & Lock-Free:** `std::atomic`, memory ordering (`std::memory_order_acquire`/`release`), avoiding OS-level mutex sleeps on critical paths.
- **Modern Features:** Move semantics, smart pointers, compile-time computations (`constexpr`), concepts/templates (C++20).

## 5. Market Microstructure & Asset Classes
- **The Limit Order Book (LOB):** Continuous double auction matching bids and asks. 
- **Matching Precedence (Price-Time Priority):** 
  1. *Price:* Better prices execute first.
  2. *Time:* FIFO queue priority at the same price level (explains why microsecond latency matters).
- **Core Order Types:** Market orders (certainty of execution, pay spread), Limit orders (certainty of price, provide liquidity), and Cancel/Replace.
- **Strategy Archetypes:** 
  - *Market Making:* Passive liquidity provision, capturing the bid-ask spread.
  - *Statistical Arbitrage / Mean Reversion:* Exploiting temporary spreads between correlated assets.
  - *Momentum / Trend Following:* Riding directional breakouts driven by order flow imbalance.
  - *Execution Algos (VWAP/TWAP):* Slicing large parent orders into child orders to minimize market impact.
- **Adverse Selection:** The risk faced by market makers when trading against faster participants with superior information (getting filled right before a directional price move).

## 6. Data Sourcing & Storage (Quant Research Infrastructure)
- **Granularity Levels:**
  - *Trades (Time & Sales):* Executed price, size, timestamp, aggressor side.
  - *Quotes (BBO):* Best bid and ask updates at the top of the book.
  - *Market Depth (L2 / L3):* Aggregated price levels (L2) or individual order-by-order add/modify/cancel streams (L3 ITCH feeds — massive file sizes, highest fidelity).
- **Data Providers:** Databento, Kaiko, Polygon.io, Alpaca, or raw public exchange WebSocket captures.
- **Storage & Query Performance:**
  - *Anti-Pattern:* Relational SQL databases (Postgres/MySQL) or document stores (MongoDB) for tick data (row-based queries take too long for billions of records).
  - *The Quant Standard:* Columnar storage formats (Apache Parquet, Apache Arrow, Zarr). Stores data by column rather than row, enabling massive compression (10x) and ultra-fast disk reads when querying specific fields (e.g., loading only prices).
  - *Binary Audit Logs:* Append-only custom binary log files for zero-copy replay and exact backtest-to-production parity.

## 7. Infrastructure & Deployment (Production-Parity & Low-Latency)
- **Co-Location (Colo):** Physical proximity to exchange matching engines (e.g., Equinix NY4, LD4). Light speed in fiber optic cable is ~5 µs/km; co-locating servers in exchange data centers cuts latency down to single-digit microseconds.
- **Local macOS vs. Linux Production:** 
  - *macOS/Docker:* Excellent for fast local development and iteration, but suffers from scheduling jitter, Apple Silicon/ARM differences, and virtualized networking overhead.
  - *Linux Bare-Metal:* Required for production parity, performance profiling, and deterministic execution.
- **Low-Level Linux Kernel Tuning:**
  - *CPU Core Isolation (`isolcpus`):* Pinning critical trading threads to dedicated CPU cores, blocking OS daemons and background interrupts.
  - *Disable Frequency Scaling:* Preventing CPU downclocking to avoid latency jitter.
  - *Kernel Bypass (DPDK / Solarflare EF_VI):* Bypassing the Linux network kernel stack to read network packets directly from the NIC into user-space via DMA (sub-microsecond I/O).
  - *HugePages:* Using 2MB/1GB memory pages instead of 4KB pages to eliminate TLB cache misses.

## 8. The Quant Lifecycle (From Alpha Idea to Live P&L)
- **Stage 1: Hypothesis Generation & Research [Python]:** Researchers analyze anomalies, alternative data, or order flow to form a thesis.
- **Stage 2: Backtesting & Validation [Event-Driven]:** Testing against historical tick data. Avoiding overfitting (data dredging) via **Walk-Forward Analysis** and out-of-sample testing.
- **Stage 3: Implementation & Integration [C++]:** Translating research models into high-performance C++, profiling latency, and verifying risk limits.
- **Stage 4: Paper Trading [Forward Testing]:** Running live feeds with simulated orders to catch operational bugs, disconnection handling, and slippage discrepancies.
- **Stage 5: Small-Capital Live Trading:** Deploying with minimal capital to test real-world fills, exchange/clearing fees, and adverse selection.
- **Stage 6: Scaling & Monitoring [Live P&L]:** Gradually scaling capital allocation while Layer 6 monitors Sharpe ratio, drawdowns, and alpha decay.

## 9. Professional Tech Stack & Toolchain
- **Languages:**
  - *Modern C++ (C++20):* Core performance-critical infrastructure, risk firewalls, and exchange gateways (Layers 1, 3, 4, 5). Target C++20 for optimal balance of modern features (`std::span`, Concepts) and compiler stability.
  - *Python (3.11+):* Alpha research, data science, machine learning, and monitoring/TUI dashboards (Layers 2 & 6).
- **Compilers & Build Infrastructure:**
  - *Compiler:* Clang (with strict warning flags: `-Wall -Wextra -Werror -Wpedantic`).
  - *Meta-Build System:* CMake.
  - *Low-Level Build Tool:* Ninja (for lightning-fast incremental builds and multi-core parallelism).
- **Package Management:**
  - *vcpkg (Manifest Mode):* Utilizing `vcpkg.json` for reproducible C++ dependency management integrated directly into CMake.
- **Messaging & IPC:**
  - *Aeron:* Lock-free ring buffers over shared memory (IPC) or UDP, featuring zero-allocation messaging and strict window-based backpressure.
- **Data Storage & Formats Placement:**
  - *Hot Execution Path:* Zero disk I/O. Relies entirely on RAM, CPU caches, and Aeron ring buffers.
  - *Research / Backtesting Input:* Apache Parquet / Apache Arrow columnar files fed into the offline backtest adapter.
  - *Audit & Replay Output:* Append-only binary log files written asynchronously by **one dedicated recorder process** (see `layers.md`, L6 (observability)) for post-mortem debugging and exact replay. *(Resolved: this originally said "passive subscribers" (plural), while the old specification put an "audit logger" on the OMS and Monitoring as the passive subscriber on every channel, so three owners existed and none wrote the log. The recorder is now its own process, because the log is the input to recovery (Investigation §13 step 4) and must not share a crash domain with the disposable TUI, and because L4 (OMS) alone cannot see `md`. Its completeness is made *detectable* rather than assumed: a killed or lagging recorder writes an explicit gap marker into the log, and the recorder never applies backpressure to producers — durability yields to the hot path.)*
- **Testing & Quality Assurance:**
  - *Unit Testing & Mocking:* GoogleTest (GTest) & Google Mock.
  - *Benchmarking:* Google Benchmark (nanosecond-level micro-benchmarks).
  - *Sanitizers:* AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
- **Containerization & Deployment:**
  - *Local Dev:* macOS + Docker containers (running Ubuntu Linux) for consistent compilation environments.
  - *Production:* Linux bare-metal (Ubuntu LTS / Rocky Linux) with kernel tuning (`isolcpus`, CPU frequency scaling disabled, HugePages, optional kernel bypass via DPDK / Solarflare EF_VI).
- **Time & Clocks:**
  - *Monotonic Clocks:* `clock_gettime(CLOCK_MONOTONIC)` for any timestamp that crosses a process boundary (see the `RDTSC` caveat above and contracts.md, "Timestamps").
  - *Clock Provider Pattern:* Abstracted via an `IClock` interface to support identical execution logic in `LiveClock` and `SimulationClock`.

## 10. Professional Dependencies & Libraries (Institution Standards)
- **Design Philosophy:** Ruthlessly minimize external dependencies. Every third-party library is a potential allocation black box or point of failure. Core primitives (ring buffers, fix parsers, flat maps) are often written in-house or strictly vetted.
- **Network & IPC:**
  - *Aeron:* Gold standard for lock-free IPC/UDP messaging with backpressure.
  - *Raw Sockets / Kernel Bypass:* Standard BSD sockets (`epoll`), or Solarflare EF_VI / Intel DPDK for direct NIC DMA access.
- **Protocol & Message Parsing:**
  - *Zero-Copy Parsers:* Simple Binary Encoding (SBE), Cap'n Proto, or FlatBuffers (access data in memory via pointers without unpacking).
  - *Raw ITCH/OUCH:* Custom pointer arithmetic and bitmask casting (`reinterpret_cast`) directly over byte arrays.
  - *JSON (WebSockets):* **Simdjson** (SIMD-accelerated) or **RapidJSON** (in-place parsing, zero allocation). Avoids heavy heap allocators like standard `nlohmann/json` on hot paths.
- **Logging & Telemetry:**
  - *Quill / spdlog:* Asynchronous, low-latency logging libraries utilizing lock-free queues to push string formatting to background threads.
  - *Binary Audit Loggers:* Custom ring-buffer loggers dumping raw POD structs straight to disk without string formatting.
- **Data Structures & Containers:**
  - *Abseil (`absl::flat_hash_map`):* Replaces slow, heap-fragmented `std::unordered_map` with contiguous memory flat hash maps for superior cache locality.
  - *EASTL:* High-performance alternative STL optimized for memory control.
- **Dependency Sourcing:**
  - *Vendoring:* Many HFT firms clone dependencies directly into a `/third_party/` directory within their monorepo, avoiding external package manager breakages and guaranteeing deterministic builds.

## 11. Low-Level Implementation Realities (The "Invisible" Toolkit)
- **Memory Management (No Heap on Hot Path):**
  - *Memory Pools / Arenas:* Pre-allocating a massive block of RAM at startup and manually managing fixed-size blocks for dynamic objects (like order books) to eliminate `malloc`/`new` latency and heap fragmentation.
- **Binary Layout & Packing:**
  - *Compiler Padding:* Compilers insert padding between struct members to satisfy alignment. Two approaches exist, and they are not interchangeable:
    - *`#pragma pack(push, 1)` / explicit packed structs:* no padding at all. Correct when the layout is a **wire format** — a serialized byte stream that must be identical across compilers, machines, or languages.
    - *Natural alignment (no packing):* the compiler pads as it sees fit. Correct when the layout is **only ever read by pointer-casting on one machine**, because the struct is never serialized.
  - *Which applies here:* **natural alignment. Dragonfly's message structs are not a wire format.** They travel through shared memory on a single box, are read by casting a pointer over the bytes, and never cross a machine or a compiler boundary with a different ABI. Packing would force misaligned `int64_t` loads, which costs more than the bytes saved and defeats the `static_assert(sizeof(...) == N)` contract. `contracts.md`, "Natural alignment, no packing" is authoritative and spells out the per-struct sizes and offsets.
  - *When packing would return:* if messages ever cross machines (UDP publication, a heterogenous fleet), the encoding becomes explicitly little-endian, packed, and behind a codec at that seam. Deliberately out of scope for the single-box core.
- **Numerical Precision:**
  - *Fixed-Point Math:* Avoiding IEEE 754 floating-point rounding errors (`float`/`double`) by scaling decimal values into integers. Dragonfly uses a fixed **1e9 (nano) scale** (`contracts.md`, "Numbers are fixed-point integers"), so `$105.50` is `105_500_000_000` and `1.25` BTC is `1_250_000_000`. One universal scale across every instrument and venue.
  - *The headroom caveat, which is easy to misstate:* the scale leaves `int64_t` headroom for a **single** value (±9.2 billion whole units), but not for a **product** of two of them. `1.0 BTC × $9.22` is the overflow ceiling; `1.0 BTC × $100k` exceeds it ~10,000×. Every notional, P&L and reservation computation therefore widens to `__int128` before dividing (`contracts.md`, "Numbers are fixed-point integers"). Stating "overflow-safe headroom" without that qualifier is how the bug hides.
- **Concurrency Primitives:**
  - *moodycamel::ConcurrentQueue:* Popular single-header, ultra-fast lock-free MPMC queue used when Aeron isn't appropriate.
  - *Avoiding Shared Pointers:* Strict avoidance of `std::shared_ptr` due to atomic reference-counting overhead; reliance on raw pointers, `std::unique_ptr`, or custom ownership models.
- **Error Handling:**
  - *No Exceptions on Hot Path:* Avoiding `try/catch` and `throw` due to non-deterministic stack unwinding; returning error codes, `std::optional`, or `std::expected` instead.

## 12. Domain 1: The Core Execution Pipeline (The Hot Path Anatomy)
- **Phase 1: Ingestion & Normalization (Layer 1):**
  - *Network Arrival:* Packet hits NIC (Kernel via `epoll` or Kernel Bypass via DPDK/Solarflare DMA into user-space RAM).
  - *Zero-Copy Parsing:* In-place JSON parsing via `Simdjson`, or binary casting via `reinterpret_cast` directly over the received buffer. **Not** `#pragma pack` — the internal message structs use natural alignment (`contracts.md`, "Natural alignment, no packing"); packing applies only to a parsed venue wire format, if one is packed at all.
  - *Timestamping:* Immediate monotonic timestamp via `clock_gettime(CLOCK_MONOTONIC)` or hardware NIC timestamps.
  - *Symbol Mapping:* Exchange string tickers mapped to canonical integer IDs via the inverted venue-listing table (~1 cycle). This is the single point where a venue symbol becomes a canonical id; everything published downstream carries only the integer. Both `BTCUSDT` (Binance) and `BTC-USD` (Coinbase) resolve to the same canonical `1001`.
  - *Publishing:* Populating fixed-size POD structs into Aeron ring buffers.
- **Phase 2: Strategy Evaluation (Layer 2):**
  - *Aeron Read:* Zero-copy read of normalized market data from Aeron subscription ring buffer on a pinned core.
  - *State Updates:* Updating rolling window statistics using pre-allocated vectors/ring buffers (zero dynamic memory resizing).
  - *Signal Generation:* Emitting `AlphaSignal` POD structs upon hitting alpha thresholds.
- **Phase 3: Risk Firewall (Layer 3):**
  - *Fast-Fail Pipeline (~10–30 ns):* Strict order of cheapest-to-most-complex checks, **first failure wins, no check skipped**. The authoritative ordering and the reason code each check emits are fixed in **layers.md, L3 (risk)**; this list is the rationale, not the spec:
    1. Global Kill Switch (latched bool load: ~1 cycle) → `KillSwitchActive`.
    2. Rate Limit (sliding-window token bucket: ~2-5 cycles) → `RateLimitExceeded`.
    3. Instrument status (reference-data lookup) → `InstrumentHalted`.
    4. Market-order policy (limit-table lookup) → `MarketOrderNotAllowed`.
    5. Order Size (primitive comparisons, incl. step size and min notional) → `OrderSizeTooLarge`.
    6. Price Collar (mid-price boundary validation: ~5-10 cycles) → `PriceCollarViolation`.
    7. Open-order count (state-mirror counter) → `MaxOpenOrdersExceeded`.
    8. Position Limit (flat array position lookup: ~10-30 cycles) → `PositionLimitExceeded`.
    9. Daily Loss (mark P&L to last mid) → `DailyLossExceeded`.
  - *Why cheap-first is also a security property:* the checks that read mutable state or mark P&L run last, so a flood of malformed signals is rejected by the stateless checks and never reaches the expensive ones.
  - *Collar reference price:* the collar compares against the mid recorded from the risk engine's own `md` subscription, never against the `limit_price` inside the signal under test.
  - *Outcome:* Dropped with logging on failure; converted to `NewOrderMsg` and forwarded on pass.
- **Phase 4: Order Translation & Routing (Layers 4 & 5):**
  - OMS verifies internal double-entry accounting state; EMS translates internal order structs into venue protocols (FIX / WebSocket JSON) and writes to exchange socket.
  - *Quantization happens here and nowhere else:* the requested price and quantity are snapped to the destination venue's tick and lot size immediately before the venue payload is built.
  - *Venue routing (resolved, contracts.md, `NewOrderMsg`):* orders carry `uint32_t venue_id`. `0` means "any venue — the receiving gateway claims it"; any other value targets one venue. Venue-agnostic strategies leave it `0` and never think about it; a cross-venue arbitrage leg sets it explicitly. The gateway check is one predicted branch, so N gateways can share the `orders` channel with no coordination and no extra process or hot-path hop.
- **Phase 5: The Feedback Loop (Fills & Acknowledgments):**
  - Exchange execution reports are ingested by Layer 5, normalized, published to Aeron, updating Layer 4's ledger and Layer 6's telemetry.

## 13. Domain 2: State Management & Crash Recovery
- **Volatile vs. Durable State:**
  - *Volatile:* Everything in CPU RAM (Aeron rings, order books, ledgers). Instant access, lost on crash.
  - *Durable:* Data persisted to disk or replicated across machines so state can be reconstructed byte-for-byte.
- **Write-Ahead Logging (WAL):**
  - Log the *intent* of a mutation to an append-only disk file before changing in-memory state. On restart, replay the log to rebuild RAM state. Disk writes must be off the hot path or batched.
- **Event Sourcing (Replay Determinism):**
  - The sequence of events is the source of truth; state is a derived projection. Never store "we own 500 shares" — store "bought 300 at 10:00" and "bought 200 at 10:05" and recompute. Guarantees bit-for-bit determinism.
- **Snapshots + Log Tail Recovery:**
  - Periodic binary snapshots of full in-memory state (e.g., every N minutes or at market close). On restart: load latest snapshot, then replay only the log tail after that snapshot's timestamp. Bounds recovery time regardless of uptime.
- **Sequence Numbers & Gap Detection:**
  - Exchanges stamp every market data update and execution report with monotonic sequence numbers. Feed handlers track the last seen sequence; a gap triggers a snapshot recovery request to refill missing messages. Prevents silent order book drift.
- **Reconciliation (The Perfect Failure):**
  - Never blindly trust recovered state. On restart, reconcile Layer 4 positions *and* open orders against the venue's own record of them. Discrepancies must be resolved (often human-gated) before trading resumes.
  - *Resolved in contracts.md, "Reconciliation".* The load-bearing architectural decision is that **the contract describes the comparison, not the acquisition.** `ReconResultMsg` carries only *the venue's* claim; L4 (OMS) performs the comparison, because L4 owns the state being compared. This keeps venue protocol (REST polling, FIX `OrderMassStatusRequest`, a drop-copy stream) entirely inside Layer 5, so **adding a venue adds zero message types** — the test for whether the design stayed general (`operations.md`, "Reference data").
  - *A correction to the drop-copy assumption.* This section originally assumed an exchange "drop-copy feed" as a standard, always-present data source. That is not true of every venue — Binance Spot offers a live user-data stream plus rate-limited REST queries, not a historical drop copy. The architecture must not be written against any one venue's verbs, so `contracts.md`, "Reconciliation" carries only venue-independent facts: a request, the venue's claim, and a verdict. *How* the claim is obtained is an L5 (EMS) adapter detail.
  - *The four disagreements* (position local-only, position venue-only, order local-only, order venue-only) are distinguished because they have different causes and severities. `contracts.md`, "Reconciliation" also adds `BasisDelta` (right quantity, wrong cost basis — invisible to a quantity-only check) and `FillCountDelta` (which disambiguates *missed fill* from *stale position*, the ambiguity a position snapshot alone cannot resolve).
  - *Fail-closed, and auto-resume on a clean pass.* An instrument counts as matched **only** if an explicit `Ok` result arrived and the comparison agreed; silence, truncation and rate-limiting are all `Inconclusive`. Treating "no news" as "no disagreement" is how a reconciliation system gives false assurance. A fully matched pass auto-resumes (requiring a human to approve a clean start would make the gate meaningless through reflex); anything else blocks, enforced by L3's (risk) `trading_enabled` flag rejecting with `RiskRejectReason::KillSwitchActive`.
- **Crash Timeline in Practice:**
  1. Process segfaults mid-session.
  2. Supervisor (systemd/custom) restarts it.
  3. Load last snapshot from disk.
  4. Replay binary event log from snapshot timestamp to crash time.
  5. Reconcile positions/open orders against the venue (`contracts.md`, "Reconciliation") — pull-based and rate-limited, so it is asynchronous and off the hot path.
  6. Human reviews reconciliation report and re-enables the trading flag. *(Refined: a fully matched pass clears the gate automatically; only a mismatch or an inconclusive result needs a human. `contracts.md`, "Reconciliation",)*
  7. System resumes with correct, synchronized state.
- **Key Design Questions:**
  - *Durability interval:* `fsync` every event (slow, safe) vs. batch every N ms (fast, risks losing N ms).
  - *Snapshot location:* Local disk (fast, single point of failure) vs. replicated network storage (slower, durable).
  - *Authority on disagreement:* The exchange always wins — and `contracts.md`, "Reconciliation" makes that concrete per disagreement class rather than leaving it as a slogan. "The exchange wins" is unambiguous for a *position* (adopt the venue's number) but not for the two order classes: `OrderLocalOnly` means the order is gone and we release the reservation, while `OrderVenueOnly` means there is live exposure the system never booked, which is **not** something to silently adopt — it blocks trading for a human. The distinction matters because "the exchange wins" applied blindly to `OrderVenueOnly` would have L4 *book* an order it never sent, which is worse than the mismatch.

## 14. Domain 3: Configuration, Instrument Master & Reference Data
- **What Reference Data Is:** Static/semi-static data (not market or transactional) required to interpret both. Changes daily or on events, but must be correct before trading begins.
- **The Instrument Master (Symbol Table):**
  - Maps external string symbols (`"BTCUSDT"`, `"ESZ4"`) to permanent internal integer `InstrumentID`s (e.g., `1001`) for cache-friendly, cycle-cheap lookups on the hot path.
  - **Two levels, and conflating them is a classic error.** The *canonical instrument* is the economic asset (BTC/USDT spot) and carries only venue-independent fields: base, quote, contract kind, settlement currency. The *venue listing* is one tradable contract for that asset on one venue and carries the micro-structure rules: tick size, lot/min quantity, min notional, contract multiplier, trading status. The same asset lists on different venues with genuinely different increments, so tick size and lot size can never be a property of the asset.
  - Loaded at startup into two read-only maps: `flat_hash_map<uint32_t, CanonicalInstrument>` for L2/L3, and `flat_hash_map<pair<Venue, Symbol>, uint32_t>` for L1's (ingestion) inbound symbol resolution. Never mutated on the hot path.
  - *Consequence:* risk limits and P&L aggregate on the canonical ID, so exposure on any venue counts toward the same cap. A strategy cannot evade a position limit by splitting across venues.
- **Symbol Mapping & Normalization:**
  - Layer 1 resolves exchange tickers to canonical IDs *before* building normalized structs.
  - Fast lookups via flat sorted arrays (binary search) or `absl::flat_hash_map<std::string_view, uint32_t>`.
  - The venue→canonical routing table is *derived by inverting* the venue listings rather than hand-maintained, so the two cannot drift. Validate at startup that no two canonical instruments claim the same `(venue, symbol)` pair — ambiguity there mis-routes real orders instead of failing loudly.
  - Same asset can have different venue names (`BTCUSDT` vs `BTC-USD` vs `XBTUSD`); each maps to one canonical ID — this is what makes Layer 2 venue-agnostic.
- **Static & Semi-Static Configuration:**
  - Risk limits (Layer 3), gateway endpoints/FIX sessions (Layer 5), strategy parameters/model weights (Layer 2), thread pinning and Aeron channel names (system).
  - Stored as YAML/JSON for readability, loaded and strictly validated at startup, never parsed on the hot path.
- **Corporate Actions & Trading Calendars:**
  - *Corporate actions:* splits, dividends, mergers, symbol changes — must be adjusted or P&L shows phantom gains/losses.
  - *Calendars:* market open/close, half-days, holidays, and futures rollover dates (e.g., `ESZ4` → `ESH5`).
  - *Tick size changes:* can occur intraday; hardcoded values cause valid order rejections.
- **Storage & Refresh Mechanics:**
  - Load once at startup into read-only memory; validate (no missing instruments, no conflicting IDs).
  - Daily refresh at close/pre-market (crypto reloads on a cadence with atomic config swap to avoid restarts).
  - Change detection: diff against previous day's reference data; flag unexpected changes for human review before trading resumes.
- **Why It Matters:** Wrong tick size → invalid orders rejected. Wrong multiplier → bad P&L and drifting risk limits. Missing mapping → dropped ticks → stale strategy prices. Unadjusted split → phantom losses → erroneous circuit breaker.

## 15. Domain 4: Simulation, Replay & Backtesting Harness
- **Core Principle (Simulation Fidelity):** A backtest is a model, not reality. Engineering discipline is about maximizing fidelity — every simplification is a lie the strategy will be punished for live.
- **The Replay Engine (Feeding the Hot Path):**
  - Not a separate system: it is Layer 1 with the network adapter swapped for a historical data adapter, feeding the *exact same* Layers 2–5.
  - Reads historical trades/quotes/L2/L3 events from disk, sorts chronologically, injects into Aeron in identical normalized format.
  - *Simulation Clock:* `SimulationClock` is advanced **to** each event's timestamp before that event is delivered, so duration logic inside Layers 2–5 (token buckets, staleness, deadlines) behaves identically to live. Note what this does and does not mean: the clock is advanced *to* the recorded event time, it does **not** overwrite the message's own `ts_ns`. Logged messages are republished byte-identically. *(Resolved in `contracts.md`, "Timestamps": `IClock` answers "what time is it now?" for durations; `MsgHeader.ts_ns` is the producer's stamp and is data. Re-stamping would break bit-exact replay, erase real event timing, and make restart detection depend on how the sim clock was seeded.)*
  - Backtests typically run at max CPU speed (a year of data in seconds), not real time.
- **Matching Engine & Fill Simulation (Hardest Problem):**
  - *Naive (cheating):* fill every limit order at the requested price → catastrophic look-ahead bias.
  - *Next-bar fills:* order at `t` filled at next bar open. Acceptable for daily, useless intraday.
  - *Queue position modeling:* your order sits behind existing orders at that price level; the market must trade through the queue ahead of you before you fill.
  - *Trade-through logic:* buy limit at $100 fills if a sell prints at $99.99; may not fill if a sell prints at exactly $100 without trading through.
  - *Adverse selection modeling:* limit orders tend to fill exactly when price is about to move against you; model toxic fill probability.
  - *Market orders:* consume actual available liquidity across multiple price levels; apply realistic slippage.
- **Latency & Friction Simulation:**
  - Order round-trip latency (tens–hundreds of µs), exchange matching delay, and market response delay (market moves before your order arrives).
  - Implementation: queue outgoing orders, hold for a simulated latency duration on the Simulation Clock, then release to the simulated matching engine.
  - *This is the one place a new `ts_ns` is legitimate.* The released fill is a **new message that was never in the log**, produced by the simulated matching engine. It is therefore a genuine producer stamping its own message, not replay re-stamping someone else's (`contracts.md`, "Timestamps"). The distinction is the rule's boundary: *replay preserves stamps, simulation creates them.*
- **Fees, Costs & Regulatory Reality:**
  - Maker rebates vs. taker fees (marginal strategies can be net-unprofitable), slippage/market impact, clearing/financing/borrow costs, taxes.
  - *Resolved in contracts.md, `ExecutionReportMsg`:* fees travel on the fill (`ExecutionReportMsg.fee`, signed, negative = maker rebate) rather than being derived locally, because a modelled fee is internal state that structurally cannot reconcile against the exchange. `liquidity_flag` (maker/taker) rides along, and `MsgFlag::ModelledFill` (the `flags` field in contracts.md) marks a fee that came from a simulation.
  - *Consequence for risk:* the daily-loss breaker runs on **net** P&L, so a strategy that is unprofitable only after costs trips the limit rather than running to the gross-loss threshold.
  - *Still out of scope at P0–P12:* clearing/financing/borrow costs, taxes, and slippage modelling beyond the simulated matching engine. Recorded here so they are not mistaken for solved.
- **The Overfitting Minefield:**
  - In-sample optimization (2018–2022) vs. out-of-sample testing (2023–2024).
  - Walk-forward analysis: re-optimize on rolling windows, test on the next.
  - Standard k-fold cross-validation leaks future data for time series; use time-series-aware validation.
  - Signature of overfitting: great in-sample Sharpe, terrible out-of-sample. A 15 Sharpe at 95% win rate is a bug, not a strategy.
- **Replay Validation & Self-Consistency:**
  - *Backtest-live parity test:* record a live trading day, replay it through the backtest engine; resulting P&L, positions, and order history must be identical. Any divergence exposes a flawed fill/latency model.
  - Ultimate proof of architectural integrity: Layers 1–5 behave identically in both modes (the point of the Clock Provider pattern and dual-source ingestion).
- **Key Design Questions:**
  - *Data granularity:* trades + BBO for some strategies; full L3 for market making.
  - *Queue position assumption:* front-of-queue is optimistic; back-of-queue is realistic.
  - *Latency assumptions:* model per-venue based on server proximity.
  - *Data gaps:* the backtest should drop packets too, to exercise gap-handling logic.

## 16. Domain 5: Deployment, CI/CD & Operational Tooling
- **Core Principle:** A system is not done when it compiles and passes tests locally. It is done when it builds reproducibly, deploys safely, is monitored continuously, and recovers automatically.
- **Continuous Integration (CI):**
  - On every commit: checkout/build (CMake + vcpkg + Clang) → `clang-tidy` + `clang-format --dry-run` → GoogleTest suites → sanitizer builds (`-fsanitize=address,undefined`) → Google Benchmark (catch perf regressions).
  - Tools: GitHub Actions (solo projects), GitLab CI, Jenkins (enterprise).
  - Gate: nothing merges to `main` unless every step passes.
- **Reproducible Builds & Artifact Management:**
  - *Problem:* "works on my machine" from compiler/library/environment drift.
  - *Solution:* multi-stage Docker builds from a pinned image, pinned Clang version baked in, `vcpkg.json` baseline commit hash pinning exact library versions.
  - Artifacts are versioned binaries built in CI; the production server never compiles.
- **Deployment & Release Strategy:**
  - *SSH + systemd (bare metal standard):* process supervision with crash-loop backoff, startup ordering, `CPUAffinity=` core pinning, secrets via protected env files.
  - *Shadow mode:* new binary receives live data but sends no orders; compare signals/latency against the old version.
  - *Canary deployment:* route small capital/traffic to the new version, expand only if metrics hold.
  - *Rollback:* one-command revert to the previous known-good artifact — non-negotiable.
- **Operational Tooling & Runbooks:**
  - *Runbooks:* documented procedures per scenario (feed dropping packets, circuit breaker tripped).
  - *Startup order:* reference data → feed handler → strategy → risk → OMS → EMS → monitoring. *Shutdown order:* stop new orders → cancel open orders → flatten positions → stop processes.
  - *Health checks:* every process publishes a heartbeat; supervisor alerts on silence exceeding N ms.
- **Monitoring & Alerting (Layer 6 in Production):**
  - *Critical (page on-call):* process down, circuit breaker tripped, reconciliation mismatch, exchange connection lost.
  - *Warning (market hours):* elevated p99 latency, spiking rejection rate, low disk space.
  - *Info:* new symbol, config change, backtest completed.
  - Tools: Prometheus + Alertmanager, or custom heartbeat monitors. Retain metrics to investigate past volatility spikes.
- **Log Management & Disk Hygiene:**
  - Append-only binary logs can reach hundreds of GB in days; a full disk crashes the trading process.
  - Mitigations: time/size-based rotation, retention policies (respecting regulatory requirements), alerts at 80% disk usage, and separating compliance audit logs from disposable debug logs.
- **Secrets Management:**
  - Never commit API keys/FIX passwords. Load via protected systemd env files, or a secret store (Vault, AWS Secrets Manager). Audit all access.
- **Time Synchronization:**
  - All processes across servers must agree on time to microsecond accuracy, or latency measurement and event ordering are meaningless.
  - PTP (IEEE 1588) with hardware-timestamping NICs for co-located systems (sub-100 ns); NTP as fallback (~ms).
- **Key Design Questions:**
  - What is the rollback procedure if a deployment goes bad mid-session?
  - What happens when the disk fills — auto-rotation, or crash?
  - Who gets paged when something breaks? (For solo work: you. Automate recovery and alerting first.)
  - How do you prove a release works before deploying? (Shadow mode + backtest-live parity testing.)
