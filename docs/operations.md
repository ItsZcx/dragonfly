# Operations

**Audience:** anyone building, running, or configuring the system.

This file has three parts. The first is how to build and run. The second is reference material for
the configuration files. The third is the phase plan, which records the order in which the system
is built and why.

## Contents

- [Build and run](#build-and-run)
  - [Install the host tools](#install-the-host-tools)
  - [Choose a preset](#choose-a-preset)
  - [Where `compile_commands.json` points](#where-compile_commandsjson-points)
  - [Format the sources](#format-the-sources)
  - [Lint the sources](#lint-the-sources)
- [Configuration](#configuration)
  - [Reference data: config/instruments.yaml](#reference-data-configinstrumentsyaml)
  - [Risk limits: config/risk_limits.yaml](#risk-limits-configrisk_limitsyaml)
  - [Strategies: config/strategies.yaml](#strategies-configstrategiesyaml)
  - [Per-process configuration](#per-process-configuration-configfeeds-and-configgateways)
- [The phase plan](#the-phase-plan)
  - [P0: repository skeleton](#p0-repository-skeleton)
  - [P1: the contract structs and the fixed-point types](#p1-the-contract-structs-and-the-fixed-point-types)
  - [P2: the clock interface](#p2-the-clock-interface)
  - [P3: runtime, transport, and the recorder](#p3-runtime-transport-and-the-recorder)
  - [P4: replay, then the live feed handler](#p4-replay-then-the-live-feed-handler)
  - [P5: the telemetry process and a minimal monitor](#p5-the-telemetry-process-and-a-minimal-monitor)
  - [P6: L3 (risk) and its state mirror](#p6-l3-risk-and-its-state-mirror)
  - [P7: L4 (OMS) and L5 (EMS) against a simulated venue](#p7-l4-oms-and-l5-ems-against-a-simulated-venue)
  - [P8: L2 (strategy), C++ only](#p8-l2-strategy-c-only)
  - [P9: the Python bridge](#p9-the-python-bridge)
  - [P10: reconciliation](#p10-reconciliation)
  - [P11: recovery, snapshots, and log retention](#p11-recovery-snapshots-and-log-retention)
  - [P12: the full monitor and the emergency controls](#p12-the-full-monitor-and-the-emergency-controls)
  - [P13: the live venue adapter](#p13-the-live-venue-adapter)
  - [P14: deployment](#p14-deployment)

---

## Build and run

The build uses Clang, CMake, Ninja, vcpkg in manifest mode, and `pkg-config`.

The presets do not set `CXX`, so a local build uses whatever `c++` resolves to. On macOS that is
Apple Clang. CI sets `CXX=clang++` so both environments build with Clang.

The two compilers implement the same warning flag differently, so a construct that builds under
Clang can fail under GCC. `-Wpedantic` reports `__int128` on GCC and not on Clang, and `-Werror`
makes that fatal. GCC is not a supported compiler here.

`pkg-config` is a prerequisite, not an optional extra: vcpkg's `arm64-osx` triplet runs
`vcpkg_fixup_pkgconfig` after building a port, and that step invokes the `pkg-config` program.
Without it, a dependency fails to install with `Could not find pkg-config`, and the CMake errors
that follow (no Ninja, no compiler) are consequences of configuring aborting early rather than
separate problems. See [Install the host tools](#install-the-host-tools).

### Install the host tools

| Tool            | macOS                                    | Debian and Ubuntu                   |
| --------------- | ---------------------------------------- | ----------------------------------- |
| `pkg-config`    | `brew install pkg-config`                | `apt-get install pkg-config`        |
| `llvm@22`       | `brew install llvm@22`                   | see `.github/workflows/ci.yml`      |

First, fetch and bootstrap vcpkg. This step is needed once, and it is safe to re-run:

```bash
./scripts/bootstrap.sh
```

vcpkg always installs to `<repo>/.vcpkg`. The path is not configurable, so a build cannot pick up
another vcpkg from elsewhere on the machine.

Then configure and build one preset. A preset is required, and there is no default:

```bash
./scripts/build.sh dev
```

Run the tests for that preset:

```bash
./scripts/test.sh dev
```

### Choose a preset

Each preset configures its own build directory, so all four can exist at once.

| Preset    | Build type       | Sanitizers                        | Build directory |
| --------- | ---------------- | --------------------------------- | --------------- |
| `dev`     | `RelWithDebInfo` | none                              | `build/dev`     |
| `release` | `Release`        | none                              | `build/release` |
| `debug`   | `Debug`          | `address,undefined`               | `build/debug`   |
| `tsan`    | `Debug`          | `thread`                          | `build/tsan`    |

`debug` and `tsan` build `Debug`, so no `NDEBUG` is defined and every `assert` stays live. The
other two define `NDEBUG`, which compiles asserts out.

AddressSanitizer and ThreadSanitizer cannot share one build. Both need their own shadow memory,
and the compiler rejects the combination, which is why `debug` and `tsan` are separate presets.

Run `./scripts/build.sh` with no argument to list the presets.

### Where `compile_commands.json` points

`build.sh` creates a `compile_commands.json` symlink at the repository root. It points at the
`dev` or `release` build most recently configured, so clangd and clang-tidy use the flags of a
normal build. The `debug` and `tsan` presets leave the symlink alone.

To configure by hand instead, use a preset directly:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

If you skip the bootstrap step, the first configure fails, because the toolchain file it names
does not exist yet.

`scripts/replay.sh` is a placeholder. It prints the command it will run and exits with an error.
The replay harness it drives does not exist until P4.

### Format the sources

`scripts/format.sh` runs `clang-format` over every `.cpp` and `.hpp` under `contracts/`, `core/`,
`apps/`, `tests/`, and `bench/`. It rewrites the files in place:

```bash
./scripts/format.sh
```

Pass `--check` to report files that need formatting without changing them. CI runs this form:

```bash
./scripts/format.sh --check
```

### Lint the sources

`scripts/lint.sh` runs `clang-tidy` over every translation unit, using the
`compile_commands.json` symlink that `build.sh` maintains. Build a preset first:

```bash
./scripts/build.sh dev
./scripts/lint.sh
```

The script exits non-zero on any finding, so it works as a check. A clean run prints one status
line and `no findings`:

```
==> linting 1 translation units (clang-tidy 22.1.8)
==> no findings
```

Headers are checked through the include graph rather than as separate arguments, because
`compile_commands.json` holds only `.cpp` entries. Passing a header directly makes `clang-tidy`
guess a compile command, which fails on system headers. The checks applied to headers come from
`HeaderFilterRegex` in `.clang-tidy`.

On macOS, `clang-tidy` comes from Homebrew LLVM 22, which is keg-only and so not on `PATH` by
default:

```bash
export PATH="$(brew --prefix llvm@22)/bin:$PATH"
```

The version is pinned in both scripts and in CI. clang-format and clang-tidy change their output
between releases, so a local tool and the CI tool on different versions disagree: clang-format 18
and 21 put a short function's brace on its own line, and 22 and 23 keep it on the same line.
The scripts reject a mismatched version rather than reformat the tree to a style CI will not
accept.

`tests/p0_smoke.cpp` is excluded. It is P0 toolchain scaffolding whose findings its own purpose
makes unavoidable, and P0's obligations move to real tests as the phases land.

---

## Configuration

Configuration is read and validated at startup. Nothing is read from disk on the hot path. A file
that fails validation is a startup failure, not a runtime surprise.

### Reference data: `config/instruments.yaml`

This file describes the world. It holds two levels.

A **canonical instrument** is the economic asset, such as BTC against USDT. It has a
`canonical_id` and carries only venue-independent fields: `base`, `quote`, `kind`, and
`settlement_currency`. Positions and P&L aggregate on the canonical id, so exposure on any venue
counts toward one limit.

A **venue listing** is one tradable contract for that asset on one venue. It is nested under
`venues:` and carries the micro-structure rules: `venue_symbol`, `tick_size`, `step_size`,
`min_notional`, `contract_multiplier`, and `status`. These are not properties of the asset, because
two venues quote the same asset with different increments.

| Key                   | Meaning                                     |
| --------------------- | ------------------------------------------- |
| `version`             | format version of the file                  |
| `scale`               | fixed-point scale, 1e9                      |
| `canonical_id`        | the economic asset's id                     |
| `base`, `quote`       | the asset pair                              |
| `kind`                | `spot`, and later futures or options        |
| `settlement_currency` | the currency cash settles in                |
| `venue_symbol`        | the venue's own symbol for this listing     |
| `tick_size`           | minimum price increment for this listing    |
| `step_size`           | minimum quantity increment for this listing |
| `min_notional`        | minimum order value for this listing        |
| `contract_multiplier` | units per contract, `1` for spot            |
| `status`              | `active`, `halted`, `auction`, or `closed`  |

Prices and increments are written as quoted strings, not numbers. YAML reads an unquoted `0.01` as
a binary float, and binary floats cannot represent decimals exactly. The loader parses the string
directly into fixed-point.

The loader rejects the file at startup if any of these hold.

- A `canonical_id` is duplicated, or falls in the reserved range `1` to `999`.
- Two instruments claim the same `(venue, venue_symbol)` pair. This would make the routing table
  ambiguous and route real orders to the wrong asset.
- A listed venue has no matching `config/gateways/<venue>.yaml`.
- A tick size or step size is not positive, or is not exactly representable at the configured
  scale.
- `min_notional` is smaller than one tick times one step.
- A `status` is not one of the four legal values.

### Risk limits: `config/risk_limits.yaml`

This file is the rulebook for L3 (risk). The checks and their order are in `layers.md`.

| Tier             | Scope                            |
| ---------------- | -------------------------------- |
| `global`         | one value per process            |
| `per_instrument` | keyed by canonical instrument id |
| `per_strategy`   | keyed by `strategy_id`           |

| Key                     | Tier                         | Meaning                                                             |
| ----------------------- | ---------------------------- | ------------------------------------------------------------------- |
| `kill_switch_active`    | global                       | the kill switch latch, set at boot and toggled by a control command |
| `max_orders_per_second` | global, per_strategy         | token-bucket rate limit                                             |
| `max_daily_loss`        | global, per_strategy         | the daily-loss breaker                                              |
| `max_open_orders`       | global, per_strategy         | cap on resting orders                                               |
| `max_order_qty`         | per_instrument, per_strategy | cap on a single order                                               |
| `max_position_qty`      | per_instrument, per_strategy | cap on absolute net position                                        |
| `price_collar_bps`      | per_instrument               | how far from the mid an order may be priced                         |
| `allow_market_orders`   | per_instrument               | whether market orders are permitted                                 |

Three rules govern how the tiers combine.

Where two tiers apply, **both must pass**. A per-strategy cap cannot override a tighter
instrument cap, and the reverse is also true.

**An absent key is not a limit of zero.** Absent means "no constraint at this tier", and the check
falls through. Zero means "disabled" at the tier where it is written.

**Zero means disabled**, except for `kill_switch_active`, which is a latch rather than a numeric
limit.

### Strategies: `config/strategies.yaml`

This file lists the strategies and the processes that run them.

| Key                       | Meaning                                                                     |
| ------------------------- | --------------------------------------------------------------------------- |
| `strategy_id`             | the logical strategy, and the risk-attribution key                          |
| `name`                    | a human label, used in logs and attribution only                            |
| `impl`                    | the registered implementation this entry binds to                           |
| `enabled`                 | a deployment switch, independent of `impl`                                  |
| `instrument_id`           | the canonical instrument this strategy trades                               |
| `params`                  | strategy-specific parameters, validated against the implementation's schema |
| `processes[].producer_id` | the process that runs the listed strategies                                 |
| `processes[].core`        | the core that process is pinned to                                          |

`impl` is required. An `impl` that is not registered is a startup failure, not a warning, because
skipping a strategy produces a deployment that looks healthy and trades nothing.

`params` is validated against the implementation's own schema before the strategy is constructed,
so a mistyped parameter is a startup error rather than a silently defaulted value.

A disabled strategy is still validated, so configuration errors surface at deploy time rather than
when the flag is eventually flipped.

`strategy_id` must be unique across the fleet and less than 65536, because it occupies the top 16
bits of `signal_id`.

### Per-process configuration: `config/feeds/` and `config/gateways/`

There is one file per process. A feed handler reads `config/feeds/<venue>.yaml` and a gateway reads
`config/gateways/<venue>.yaml`. Adding a venue means adding a file in each directory and a systemd
unit, and changing no code in L2, L3, or L4.

| Key                  | Meaning                                              |
| -------------------- | ---------------------------------------------------- |
| `venue`              | the venue's name                                     |
| `venue_id`           | the venue's numeric id on the wire                   |
| `kind`               | `spot`, and later futures or options                 |
| `producer_id`        | the process's identity in the message header         |
| `core`               | the CPU core this process is pinned to               |
| `instruments`        | the canonical ids this process serves                |
| `reconnect`          | backoff settings for a dropped socket                |
| `fatal_status_codes` | HTTP statuses that abort at boot instead of retrying |
| `websocket`          | endpoints and stream subscriptions, feeds only       |
| `credentials`        | environment variable names, gateways only            |
| `venue_rate_limit`   | the venue's own published limit, gateways only       |

Two rules apply to both directories.

`producer_id` must be unique across running processes. Duplicate ids make sequence-gap detection
ambiguous.

The same venue's two files must declare the same `venue_id`, and the process refuses to reach
`ready` on a mismatch.

Credentials are never written into these files. A gateway file names the environment variables
that hold them, and the values are injected at runtime.

---

## The phase plan

The system is built in fifteen phases. Each phase is proven before the next one starts.

A phase closes when both of these are true: every issue in its list is closed, and every proof in
its section passes. Three obligations hold for every phase.

- **The contract moves with the code.** A phase that changes a layout updates `contracts.md` and the
  layout assertions in the same change. The contract is frozen against unrecorded change, not
  against extension, so a phase that adds a message adds its type code, its struct, its assertions,
  and its documentation together.
- **The smoke test shrinks.** `tests/p0_smoke.cpp` carries checks for phases that had not been built
  yet. Each phase names the checks it retires, and the file is deleted when no check remains.
- **Every phase answers the same seven questions.** What it is for, what it builds, the technologies
  it uses, what it depends on, what it proves, what it excludes on purpose, and what is still open
  inside it.

| Phase | Builds | Proves |
| ----- | ------ | ------ |
| **P0** | repository skeleton, toolchain, one smoke test | the build and `ctest` are green on four presets |
| **P1** | the contract structs and the fixed-point types | sizes and offsets match the document, arithmetic is exact |
| **P2** | the clock interface, live and simulated | the simulated clock is deterministic, the live one is monotonic |
| **P3** | runtime, transport, sinks, and the audit recorder | publish and subscribe over a real bus, a durable log, gap detection |
| **P4** | replay, then the live feed handler | replayed venue data produces the canonical messages exactly |
| **P5** | the telemetry process and a minimal monitor | a replayed run is visible, a slow monitor hurts nothing |
| **P6** | L3 (risk) and its state mirror | each check yields its reason code, and the gate refuses |
| **P7** | L4 (OMS) and L5 (EMS) against a simulated venue | a synthetic signal runs to a fill, deterministically |
| **P8** | L2 (strategy), C++ only | registration is enforced at startup |
| **P9** | the Python bridge | a Python strategy is a bus peer with its own identity |
| **P10** | reconciliation | a deliberate mismatch blocks trading, a clean pass reopens it |
| **P11** | recovery, snapshots, and log retention | a killed process restarts and reconstructs identically |
| **P12** | the full monitor and the emergency controls | the kill switch cancels and blocks new orders end to end |
| **P13** | the live venue adapter | the same L5 works against a real venue |
| **P14** | deployment | measured latency on bare metal, and a one-command rollback |

The order is not the layer order, for seven reasons.

**Contracts come first.** Every process embeds the message layouts, and the audit log stores them. A
layout change after P4 invalidates every test and every recorded log, so the data is frozen before
anything depends on it.

**Replay comes before any live feed.** Replay is deterministic and needs no credentials, so it is
testable in CI, and it forces the live adapter to be a thin second implementation of the same seam.

**Risk comes before strategy.** L3 has the clearest specification and can be tested with synthetic
signals, with no strategy involved. Building it first means the strategy is written against a fixed,
proven gate.

**Reconciliation is its own phase, before recovery.** They share only the word "restart". Recovery
asks whether the system can rebuild its own state, which is internal and deterministic.
Reconciliation asks whether that state agrees with the venue, which is external and produces a
trading gate. Putting the gate in place before recovery starts restarting processes means a restart
always lands somewhere that knows how to refuse to trade.

**The Python bridge follows the C++ strategy.** The C++ phase proves the pipeline and the
registration rules. The Python phase then mirrors a working API instead of inventing one, and the
two have no shared code, so a packaging problem in one cannot block the other.

**The full monitor comes before the live venue adapter.** The first real order is then placed by a
system someone can watch and stop, not by one whose only witness is a simulated venue.

**The live venue adapter comes after recovery.** The moment the system can trade real money,
it can also rebuild itself after a crash. This phase runs against the venue's test network,
and mainnet is P14.

---

### P0: repository skeleton

**Purpose.** Prove the toolchain end to end before any trading code exists, and freeze the
conventions every later phase inherits. Closed.

**Builds.**

- The build system: CMake with four presets, `dev`, `release`, `debug` with AddressSanitizer and
  UndefinedBehaviorSanitizer, and `tsan` with ThreadSanitizer, plus warnings as errors and a global
  C++20 compile contract.
- Dependencies through vcpkg in manifest mode, with a pinned `builtin-baseline` and a vcpkg checkout
  at a fixed repository-local path.
- The scripts: `bootstrap.sh`, `build.sh`, `test.sh`, `format.sh`, `lint.sh`, and
  `format-md-code.py`, each carrying the reason for the rule it enforces.
- Conventions: `.clang-format`, `.clang-tidy`, and `.clangd`, all pinned to LLVM 22 and all refused
  on any other major version.
- The test harness, which states the layout rule once: a test lives under
  `tests/<unit|integration>/` followed by the path of the source it tests, one binary per component,
  named for the component.
- CI: a matrix over all four presets, and a second job for formatting and linting.
- Sketches that later phases fill in: the five configuration files, `deploy/`, `python/`, `bench/`,
  `data/`, and the per-component READMEs.

**Technologies.** C++20, Clang 22, CMake 3.25 or later, Ninja, vcpkg with baseline `9cd6dd90`,
GoogleTest, Google Benchmark, clang-format and clang-tidy 22, bash compatible with macOS 3.2, Python
3.11 or later with `uv`, hatchling, ruff, mypy, and pytest, Ubuntu 24.04 in CI.

**Depends on.** Nothing.

**Proves.**

1. `./scripts/build.sh <preset>` and `./scripts/test.sh <preset>` pass for all four presets.
2. `./scripts/format.sh --check` reports no change under the pinned clang-format.
3. `./scripts/lint.sh` reports no findings under the pinned clang-tidy.

**Not in this phase.** No trading logic, no message body beyond `MsgHeader`, no bus, no process, and
no Python behaviour. Configuration files are shapes, not loaders.

**Gaps.** The phase keeps six checks in `tests/p0_smoke.cpp`: the toolchain checks, the two
permanent conventions, and the check on this document's own table. The other fifteen are retired by
the phases that replace them, listed below. That mapping was derived by reading each check, and it
needs verifying against each phase's real tests before it is treated as settled.

### P1: the contract structs and the fixed-point types

**Purpose.** Freeze the wire format and the arithmetic before anything depends on them. Closed,
across thirteen issues.

**Builds.**

- `MsgHeader`, the modelled-fill flag, and the message type codes, in reserved bands so a type can
  be added without renumbering.
- The closed value sets every message shares, and `VenueId` alongside the identifiers.
- Nineteen message structs and one nested level type, one header per family, with every fixed-length
  array written as `std::array`.
- Fixed-point arithmetic: the scale and a widening multiply that cannot wrap.
- The strong identifier types, the producer id ranges, and the one checked constructor, for
  `StrategyId`, because an unchecked value there is a documented defect.
- Two hundred and fifty compile-time assertions over size, offset, traits, and enum sentinels, two
  runtime test binaries, and `docs/contracts.md` as the single source of truth for all of it.

**Technologies.** C++20 headers only, `static_assert` and `offsetof` for layout, `__int128` for the
widening multiply, GoogleTest for the two runtime binaries. No macros for layout checks.

**Depends on.** P0 for the toolchain, the presets, and the test layout rule.

**Proves.**

1. Every message's size and every field offset match the document. Rule: `contracts.md`, "The
   message set".
2. Every wire type is trivially copyable, standard layout, and trivially default constructible.
3. Value-initializing any message type yields all-zero bytes, so padding is deterministic and byte
   comparison is sound. Rule: `contracts.md`, "Every message is value-initialized before use".
4. The widening multiply is exact at the documented boundary, rounds toward zero, and never wraps.
5. Strong identifiers are distinct types with no implicit conversion, match the wire widths, and
   `MakeStrategyId` rejects every value above 65 535 in every preset.

**Not in this phase.** No codec, no transport, no clock, and no `.cpp` file in either library. Both
`df::contracts` and `df::types` are header-only.

**Gaps.** Money stays unwrapped: prices, quantities, and cash are raw scaled integers, so the
compiler cannot catch a swapped price and quantity. That is deliberate, and the defence is the
documented field names and the risk pipeline's ordering. The build depends on `__int128`, which
makes Clang and GCC the only supported compilers. The first benchmark was deferred to P3.

### P2: the clock interface

**Purpose.** Make time injectable, so identical code measures durations identically live and
in backtest.

**Builds.**

- One interface with a single monotonic reading, and two implementations: a live clock reading
  `CLOCK_MONOTONIC`, and a simulation clock that the replay driver advances.
- The advance operation on the simulation clock alone, never on the interface, so production code
  cannot move time.
- The rule the whole system relies on: a component asks the clock for the current time to measure a
  duration, and reads `hdr.ts_ns` for the timestamp on a message. A clock never stamps a message.

**Technologies.** `clock_gettime(CLOCK_MONOTONIC)`, GoogleTest, and no timer thread at this phase.

**Depends on.** P0.

**Proves.**

1. The simulated clock changes only when it is told to, and to exactly the value it is told, so two
   runs from the same inputs read the same sequence of times.
2. The live clock never returns a value below an earlier reading.
3. A consumer written against the interface behaves the same under both clocks, which is the seam
   itself, and is tested by driving one consumer through each.

**Not in this phase.** No replay driver, no event loop, no timeout policy, and no heartbeat
staleness rule.

**Gaps.** Nothing consumes the clock until P4, so the third proof needs a test consumer. The phase
is kept before P3 because the seam is easier to hold to when it exists before either implementation.

### P3: runtime, transport, and the recorder

**Purpose.** Give every process a way to exist: load and validate its configuration, pin itself to a
core, log without blocking, publish and subscribe over the bus, and write the bus down durably.

**Builds.**

- Configuration loading and validation for the five files, including the six startup invariants that
  `config/instruments.yaml` declares, with messages that name the file and the offending key.
- Asynchronous logging, core pinning, signal handling with clean shutdown, and process identity.
- The reference-data hash: a content hash of the parsed `config/instruments.yaml`, computed at boot
  and published on every heartbeat.
- The transport: one publication and one subscription, and a concrete sink per channel carrying that
  channel's declared publish-failure policy.
- The audit recorder: its own process and its own `producer_id`, a durable append-only binary log
  with self-describing framing and explicit gap markers, and a reader for the phases that replay it.
- Consumer-side sequence tracking, distinguishing a producer restart from a lost message.
- `IControlConsumer` as the single inbound contract for `control`, and the executor table in
  `contracts.md` starts here with the liveness side.

**Technologies.** Aeron 1.52.2 from vcpkg, including `aeronmd`, which runs as its own supervised
process on its own core with the `archive` feature off. Quill for logging. yaml-cpp with
hand-written validation. `pthread_setaffinity_np`. POSIX signals. A file format for the log, decided
at this phase.

**Depends on.** P0 for the toolchain and CI, P1 for the header and the layout rule, P2 for the clock
that stamps `hdr.ts_ns`.

**Proves.**

1. The recorder writes a log that replays byte-identically. Rule: `contracts.md`, "Timestamps".
2. A killed recorder leaves a gap marker rather than a silent hole. Rule: `layers.md`, L6.
3. With a deliberately slow subscriber, `md` and `signals` drop and count, `orders` calls its
   fail-closed action instead of dropping, and `control` retries until delivered. Rule:
   `contracts.md`, "Publication failure is per-channel".
4. A killed producer that restarts is recognised as a restart, and a forward sequence jump is
   recognised as a gap. Rule: `contracts.md`, "Sequence numbers across a restart".
5. Two processes on different reference-data hashes are detected, and the mismatch is published.
   Rule: `contracts.md`, `HeartbeatMsg`.
6. Value-initializing any message type twice with identical fields yields equal bytes. Carried from
   P1 and kept as a regression.
7. Retires `PublishFailureIsHandledPerChannel`, `ConfigSkewIsDetectableNotSilent`,
   `SequenceDiscontinuityClassification`, `LogEntryFramingIsSelfDescribing`, and
   `RecorderIsItsOwnProducerAndOwnsTheLog`.

**Not in this phase.** No venue adapter, no replay driver, no layer logic, no monitor, no telemetry,
no snapshots, and no reconciliation.

**Gaps.** Proofs 3 and 5 end in the trading gate, which arrives at P6, so this phase proves that the
detection fires and the policy is invoked. P6 proves the refusal. The log format is this phase's
deliverable rather than an inherited one. `control` retries with bounded exponential backoff, never
dropping, and a sustained breach raises an operator alert.

### P4: replay, then the live feed handler

**Purpose.** Build the first real producer, and prove that historical data and live data are the
same shape.

**Builds.**

- The replay engine: a log reader and a driver that advances the simulation clock to each event's
  timestamp before delivering it.
- The feed seam with two implementations, a replay handler reading the log and a live handler
  reading the venue's socket, and L1's work behind both of them: parse, resolve the symbol to a
  canonical id, timestamp twice, publish, and maintain the local book.
- The book mechanism: snapshot, deltas, sequence tracking, gap detection, and resync, with its two
  distinct resync paths.
- The venue-side resync, which is internal to L1: a gap in the venue's own stream triggers a fresh
  snapshot from the venue over REST and a rebuild, governed by `gap_policy` in the feed config.
- The consumer-side resync: a `BookSnapshotRequestMsg` on `control` is answered on an off-path
  thread by an ordinary `BookSnapshotMsg` on `md`, with outstanding requests for one instrument
  coalesced.
- `apps/feed_handler` and `apps/replay`, already mapped as two binaries over one seam.
- The fixtures: a committed venue capture and a committed binlog segment, each with a committed
  expectation and a script that regenerates the expectation so a change appears as a diff.

**Technologies.** The client libraries for the live socket and the venue's JSON are chosen at this
phase, under two constraints: TLS to the venue, and no allocation on the hot thread. The log reader
and the simulated clock are ours.

**Depends on.** P1 for the market-data messages, P2 for the clock, P3 for configuration, sinks, the
log format, and sequence tracking.

**Proves.**

1. A recorded venue capture produces exactly the expected canonical messages.
2. A recorded binlog segment replays byte-identically. Rule: `contracts.md`, "Timestamps".
3. Both implementations satisfy the seam, proven by driving one consumer through each.
4. The book survives a gap: deltas apply in sequence, the gap is detected, a resync is requested and
   satisfied, and the rebuilt book equals the snapshot's book.
5. A `BookSnapshotRequestMsg` is answered off the ingest thread, with repeated requests coalesced.
6. Retires `ReplayRepublishesBytesUnchanged` and `FeedHandlerHasAnInboundPortForResync`.

**Not in this phase.** No strategy, risk, orders, gateway, or monitor. Live mode needs no
credentials, because market data is public.

**Gaps.** A binlog holds canonical messages, so replaying it proves replay and says nothing about
normalization. The two fixture kinds exist for that reason. The live adapter cannot be exercised in
CI, so it splits three ways: a pure decoder tested against committed frames, a transport tested
against a local fake server, and a written manual procedure whose capture becomes the next fixture.
The first capture is made by hand and treated as read-only. `book_depth`, `snapshot_depth`, and the
64-level message format are three different numbers, and the document should say which bounds what.

### P5: the telemetry process and a minimal monitor

**Purpose.** Make the system visible, so later phases can be watched while they are built.

**Builds.**

- The telemetry process: off the hot path and droppable, it subscribes to the bus and turns
  high-rate binary messages into a low-frequency summary. It is the component that keeps Python away
  from raw hot-path messages.
- The summary: message counts and drops per channel, process liveness, positions per canonical
  instrument, realised and unrealised P&L, and the reference-data hash per process.
- Heartbeat aggregation, meaning which processes are alive and which have gone quiet, detected here
  because this is where heartbeats arrive. The consequence is not attached at this phase.
- A minimal Python dashboard over the summary, which is the first Python in the repository that does
  something, and a CI job running ruff, mypy, and pytest over `python/`.

**Technologies.** C++ on the existing transport, a versioned newline-delimited JSON summary, and
Python 3.11 or later with Textual and Rich from the monitoring extra.

**Depends on.** P3 for the transport and the sinks, P4 for replay, which is the only way this phase
can be demonstrated without a venue.

**Proves.**

1. With a replay driving a binlog, the summary's counts match the input, so the pipeline is
   observable from a file.
2. A slow or dead monitor does not affect anything upstream, because the telemetry process drops the
   oldest summary rather than applying backpressure. Rule: `layers.md`, L6.
3. The monitor renders a replayed run.

**Not in this phase.** No kill switch, no alerting, no config-skew refusal, no reconciliation view,
and no strategy UI.

**Gaps.** The JSON transport, the summary schema, and the telemetry process's home are all decided
at this phase. The schema needs a version in every message, so an old monitor and a new producer
fail loudly rather than misreading. A terminal UI has no automated proof worth the name, so the data
half is tested and the rendering half is checked by hand.

### P6: L3 (risk) and its state mirror

**Purpose.** Build the firewall, the only layer that can say no, and the gate that keeps the system
from trading on state it has not proven.

**Builds.**

- The check pipeline: ten checks in a fixed cost order, first failure wins, and each maps to exactly
  one reason code.
- The limit table from `config/risk_limits.yaml`, at three tiers, with both applicable tiers
  required to pass, an absent key falling through, and zero meaning disabled.
- The state mirror: positions, open-order count, and daily P&L, derived from the same event stream
  that feeds the ledger rather than by asking it.
- The canonical suspension set, carried by a new `control` message that names an instrument and
  whether it is suspended, so the monitor can suspend and reactivate one instrument live.
- The trading gate, closed at boot and opened only by a complete reconciliation pass.
- The kill-switch latch, and the fail-closed action the `orders` sink calls.
- Rejection reporting on `risk_events`.
- The `Halt` and `Resume` rows of the executor table in `contracts.md`.

**Technologies.** yaml-cpp through P3's loader, a token bucket refilled from P2's clock, the
collar's mid from L3's own `md` subscription, and fixed-size tables with no allocation on the
hot thread.

**Depends on.** P1 for the signal and order messages, P2 for the clock, P3 for configuration and the
per-channel policy, and P4 for `md`.

**Proves.**

1. Each of the ten checks produces exactly its own reason code.
2. A signal that fails several checks yields the code of the first one in the sequence.
3. The tiers combine correctly: both must pass, an absent key falls through, and zero disables.
4. The gate is closed at boot and opens only after a complete, fully matched pass. Rule:
   `contracts.md`, "Reconciliation".
5. A detected reference-data mismatch keeps the gate closed, which is P3's proof 5 wired to a
   refusal.
6. A failed `orders` publication latches the kill switch, which is P3's proof 3 wired to a refusal.
7. An order with no mid is refused as `PriceCollarViolation`, so a missing reference price and a
   price outside the collar share one response.
8. Retires `RiskLimitsConfigCanExpressThePipeline`.

**Not in this phase.** No venue, no order placement, no reconciliation comparison, no dynamic
budgets, and nothing for a human to look at.

**Gaps.** The gate needs a way to know a pass is complete, because one verdict per instrument never
says "that was the last one". The marker is decided at the implementing phase, and the candidates
are a final flag on the venue's last result, a separate message, or a declared count. The open-order
count's source is explored here, since L3 does not subscribe to `orders` and cancels are published
there. The budget message from L4 to L3 stays open, so this phase ships configured limits only.

### P7: L4 (OMS) and L5 (EMS) against a simulated venue

**Purpose.** Build the ledger that records what we own and the layer that puts orders on the wire,
with a matching engine standing in for an exchange.

**Builds.**

- The ledger in legs: net quantity and basis per (venue, instrument), with the canonical position
  derived as their sum. A fill names a venue, so it updates exactly one leg, and realised P&L is
  computed per leg against that leg's basis.
- Cash per settlement currency as settled and reserved, with available derived.
- Order records carrying the states that separate live-at-venue from open-from-our-side, the
  pending-cancel and pending-replace states, and the reservation each order holds.
- Reservations: a buy limit reserves quantity times price, a sell reserves nothing, and neither a
  cancel nor a replace releases anything until a terminal report arrives.
- `OnFill` as the only operation that moves money, with the three position cases, the weighted
  basis, the basis reset at flat, and fees kept separate from realised P&L.
- Duplicate-fill detection from cumulative quantity, with a counter.
- The off-path abandonment sweep, which times out an order whose cancel report never arrived and
  records a ledger adjustment before applying it.
- L5: the lifecycle state machine, quantization at the single boundary from the venue listing, the
  obligation to report every outcome, fees either reported or modelled and marked, the two rate
  limits, and venue routing.
- The venue transport seam and the simulated venue behind it, with three disagreement modes: a claim
  bias, report suppression, and order injection.
- Marks from `md`, so unrealised P&L and the daily-loss breaker stay correct.
- The `CancelAll` and `Flatten` rows of the executor table.

**Technologies.** Venue listing data from `config/instruments.yaml`, parsed from decimal strings at
load time and never through a binary float. The monotonic clock for the sweep deadline and the rate
limiter. A simulated matching engine behind the transport seam, in process or as a peer.

**Depends on.** P1 for the messages, P2 for the clock, P3 for the transport and the per-channel
policy, P4 for replay, and P6 for approved orders.

**Proves.**

1. A synthetic signal runs to a fill, and the same input produces an identical ledger.
2. A pending replace releases nothing while in flight, a rejected amendment reverts the terms with
   the reservation untouched, a reduce is always admissible, and an increase must fit available
   cash. Rule: `layers.md`, L4.
3. The same execution report delivered twice leaves settled cash, fees, filled quantity, and
   realised P&L unchanged, and increments the duplicate counter. Rule: `contracts.md`,
   `ExecutionReportMsg`.
4. Quantization happens once and only here: an order off the grid is snapped or refused locally and
   reported as an execution report, never as a risk rejection.
5. A cancel the venue rejects still produces a terminal report, and the reservation is released.
6. Retires `DuplicateFillIsANoOpNotAnError` and `ReplaceIsNotCancelPlusNew`.

**Not in this phase.** No reconciliation, no snapshots, no recovery, no live venue, and no strategy.
There is no journal: the log is the single durable source, so the ledger writes nothing durable here
beyond what it publishes. No strategy-driven cancel or replace exists in v1, and the cancel path
serves the abandonment sweep and `CancelAll`.

**Gaps.** The simulated venue's home and target are confirmed here. The live venue adapter is P13,
so this phase proves the seam rather than the venue. The cross-layer budget stays open; the ledger
checks only its own available cash.

### P8: L2 (strategy), C++ only

**Purpose.** Build the layer that decides, and prove the registration rules that keep a bad
configuration from producing a deployment that silently trades nothing.

**Builds.**

- The strategy interface and one threshold implementation, read, decide, publish.
- The compiled-in factory keyed by the `impl` string, where an unknown key is a startup failure that
  lists the available keys.
- A small declarative parameter schema per implementation, carrying name, type, range, and default,
  validated before the factory runs so a failure names the key and the file.
- The `enabled` switch, validated even when disabled.
- `strategy_id` uniqueness and the 16-bit ceiling at load, through `MakeStrategyId`.
- The mapping from strategies to processes, with `producer_id` and core, and the rule that a process
  consumes only the feeds and gateways it is wired to.

**Technologies.** C++20 on the existing runtime, yaml-cpp for configuration.

**Depends on.** P1 for the signal message, P2 and P3 for the clock and the runtime, P4 for market
data, P6 to see rejections, and P7 for a fill.

**Proves.**

1. An unknown `impl` fails at startup with the available keys listed.
2. A parameter typo fails schema validation before construction.
3. A duplicate `strategy_id` is rejected at boot naming the file and the id, and a value of 65 536
   or more is rejected because it would truncate and collide. Rule: `layers.md`, L2.
4. A trivial strategy trades end to end on replayed data.
5. Retires `StrategyRegistrationBindsConfigToCode`.

**Not in this phase.** No alpha. The system supplies machinery and the strategy supplies the idea,
so this phase ships a placeholder that proves the plumbing. No venue selection, no order management,
and no durable strategy state, which is deliberate.

**Gaps.** None open. The Python bridge is P9, and the placeholder strategy here is the API
it mirrors.

### P9: the Python bridge

**Purpose.** Let a strategy run in Python as an ordinary bus peer, without putting an interpreter, a
garbage collector, or the GIL near a hot thread it does not already own.

**Builds.**

- A pybind11 extension module over the existing transport, exposing a subscription, a publication,
  and the message structs.
- The polling style: Python owns the loop, and the blocking poll releases the GIL while it waits and
  reacquires it to return a decoded message.
- The binding generator: an explicit registration list, generated accessors for every message, and a
  test that fails when a message type has no binding.
- The Python strategy API, a subclass with explicit handler methods and a framework-owned loop, plus
  a boilerplate strategy mirroring the C++ placeholder so the whole system exercises it.
- Packaging: the extension in the CMake build, imported from the build tree during tests, with the
  wheel assembled afterwards. The lockfile that P5 deferred lands here.

**Technologies.** pybind11 from the `contracts` extra, promoted to a requirement. Python 3.11 or
later. The same transport and the same generated layouts as everything else.

**Depends on.** P8 for the API being mirrored, P3 for the transport, P1 for the contracts, and P6
and P7 for anything to arrive at the end of the path.

**Proves.**

1. A Python strategy carries its own `producer_id`, and its signals are indistinguishable downstream
   from a C++ strategy's.
2. The boilerplate Python strategy trades end to end on replayed data.
3. Every message type has a binding, enforced by a test rather than by inspection.
4. The GIL is never held across a blocking transport call.
5. Retires `BridgedStrategyIsADistinctProducer`.

**Not in this phase.** No alpha, and no new transport. This is a second language at the same seam.

**Gaps.** Every transport decision is made at the implementing phase, which is where the two
constraints live: the GIL must not be held across a blocking call, and layouts must not be copied
into a second representation. Packaging is the part most likely to consume more time than expected.

### P10: reconciliation

**Purpose.** Answer one question after a restart: does the ledger agree with the venue? Turn the
venue's claim into a verdict per instrument, and make the gate an explicit consequence.

**Builds.**

- The request, published by L4 at startup and on operator request, with a fresh `recon_id`.
- The venue's claim, reported by L5 one message per instrument, with no comparison, because
  comparing is accounting.
- The comparison in L4, producing one verdict per (venue, instrument) with an outcome and, on a
  disagreement, one of the six classes with its severity.
- The end-of-pass marker, which is decided at this phase.
- L3's gate consumer, which opens only when it saw the request, saw every verdict, and saw every
  verdict matched.
- The mismatch workflow: a human resolves the disagreement, then requests a fresh pass to prove the
  fix landed.
- A command-line flag on the OMS that requests a pass, which is the same message the monitor sends
  later.
- The simulated venue's three disagreement modes, exercised here.

**Technologies.** The `recon` channel, bounded backoff around venue queries because reconciliation
is rate-limited, and the clock for pass timing.

**Depends on.** P3 for the transport, P7 for the ledger state and the simulated venue, P6 for the
gate and its consumer.

**Proves.**

1. A deliberate mismatch blocks trading, with the correct class attached to the correct instrument.
2. A clean pass reopens trading automatically, with no operator action.
3. An inconclusive pass, from a rate-limited or unavailable venue or from a truncated claim, keeps
   the gate closed. Rule: `contracts.md`, `ReconStatus`.
4. A pass whose end marker never arrives keeps the gate closed.
5. An operator pass after a resolution proves the fix rather than assuming it.
6. Retires `ReconciliationComparesOrderIdentityNotCount`.

**Not in this phase.** No snapshot or restart procedure, no dashboard, and no continuous
reconciliation. The design reconciles at restart and on request, and the document should keep that
limitation visible.

**Gaps.** The per-venue comparison depends on the leg-first storage from P7, which is why that
landing point matters. `recon` is not recorded, so an adjustment can be traced to a `recon_id` the
log does not contain, and the decision about the log's contents belongs to P11.

### P11: recovery, snapshots, and log retention

**Purpose.** Make a killed process come back to the same state, using the log as the only record of
what happened.

**Builds.**

- Snapshots for the ledger: legs, cash, order records, and the per-producer sequence watermarks it
  has applied, written off the hot path with an atomic replace.
- Recovery as snapshot plus log tail, which is the only path now that there is no journal.
- Snapshot cadence, a fixed interval with a bounded tail, plus one before a graceful shutdown.
- A stated retention policy: enough history to cover the snapshot interval with margin, rotation by
  size and date, and a disk that cannot accept a log write treated as fatal.
- L3's mirror rebuild, by replaying the log at startup, before the gate can open.
- Restart supervision for the processes that exist by this phase.

**Technologies.** Atomic file replacement through a temporary file, fsync, and rename. The monotonic
clock for scheduling. systemd for restart policy.

**Depends on.** P3 for the log, P7 for the ledger state, P10 for the gate that closes
during recovery.

**Proves.**

1. A process killed after an abandonment comes back holding the released reservation, because the
   adjustment is in the log. Rule: `contracts.md`, "Ledger adjustments".
2. A process killed after an adopted correction comes back matching, not reverted. Rule:
   `contracts.md`, "Ledger adjustments".
3. With the `ledger` publication forced to fail, the live ledger does not apply the adjustment.
   Rule: `contracts.md`, "Ledger adjustments".
4. Replaying a log with a duplicated adjustment changes nothing.
5. A snapshot plus its log tail reconstructs the state the process held before it died.
6. A log with a hole reconstructs to `Inconclusive` and refuses to trade rather than guessing.
7. Retires `LedgerAdjustmentsAreRecordedAndIdempotent`.

**Not in this phase.** No fleet deployment and no rollback, which is P14. No new trading behaviour.

**Gaps.** The snapshot format is decided here, including what a watermark means for a stream with
several producers. The line with P14 is that this phase owns restart supervision for the processes
that exist, and P14 owns the fleet picture.

### P12: the full monitor and the emergency controls

**Purpose.** Give a human the two things L6 exists for: see what the system is doing, and stop it.

**Builds.**

- The full dashboard over the telemetry summary, with this metric set: message counts and drops per
  channel, latency percentiles from `md` to signal to order to fill, per-strategy attribution with
  rejections by reason code, open orders per venue, reconciliation passes with verdicts and mismatch
  classes, suspension state per instrument, and the alert panel.
- The kill switch interface, showing each command's effect, plus a command-line route that publishes
  the same messages, so the emergency stop does not depend on the dashboard.
- Per-instrument suspension and reactivation, over the message added at P6.
- The reconciliation view, with a control that requests a fresh pass. It shows and requests, and it
  never sets the gate.
- The alert panel, and the executor wiring that makes `CancelAll` reach L4 and `Flatten` reach L4
  and L5.

**Technologies.** Python with Textual and Rich over the JSON summary, and the same transport the C++
processes use, through the P9 bridge.

**Depends on.** P5 for the telemetry process and the summary, P6 for the latch and the suspension
message, P7 for the orders that cancels and flattens act on, P10 for verdicts, and P11 for a system
that survives being told to stop.

**Proves.**

1. The kill switch cancels live orders and blocks new ones end to end, with the ladder behaving as a
   ladder. Rule: `contracts.md`, `KillSwitchMsg`.
2. Every command reaches a named executor, and a command whose executor does nothing is a failure.
3. `Flatten` is operator-only, transmits orders while latched, and is the single documented
   fail-open path.
4. A mismatch is visible with its class, and a fresh pass after a resolution reopens trading without
   the monitor being able to force it.
5. A suspended instrument is refused with its reason code, and reactivation restores trading.
6. Config skew and a dead process are both visible.
7. Retires `EveryKillCommandHasANamedExecutor`.

**Not in this phase.** No trading logic, no new transport, and no change to the gate's rules. The
monitor observes and requests.

**Gaps.** Alert destinations and thresholds are decided at this phase, with every threshold a
configuration value. "Full" is bounded by the metric list above. A terminal UI has no automated
proof, so the data half is tested and the rendering half is checked by hand.

### P13: the live venue adapter

**Purpose.** Replace the simulated venue with a real exchange session on the venue's test network,
so the system places orders against a real protocol and finds out where its assumptions were wrong.

**Builds.**

- An authenticated session, with credentials from the environment variables named in the gateway
  config, requests signed over a payload that carries a timestamp and a receive window, and the
  venue's clock consulted so our timestamps land inside it.
- The outbound encoder: scaled integers formatted as the venue's decimal strings at the venue's
  precision, and our `client_order_id` carried in the venue's client-id field.
- The inbound decoder: the venue's order events mapped onto `ExecutionReportMsg`, including its
  status set, its executed and cumulative quantities, its commission, and its maker flag.
- Place and cancel only. There is no amend path in v1, because nothing requests a replace.
- The session lifecycle: keepalive, reconnect with backoff, the fatal-status path that aborts at
  boot, and the venue's own rate limits with the lower of the two binding.
- Reconnect reconciliation: on losing a session, L5 requests a pass, because events delivered while
  disconnected are gone.
- The three-part split from P4: a pure encoder and decoder tested against committed venue frames, a
  transport tested against a local fake server, and a written manual procedure whose capture becomes
  fixtures.

**Technologies.** The client libraries are chosen at this phase, under the same two constraints as
P4: TLS to the venue, and no allocation on the hot thread. Order entry uses the venue's WebSocket
API with the user-data stream, and REST serves the reconciliation queries.

**Depends on.** P4 for the live-adapter pattern, P7 for the transport seam, P10 for reconnect
reconciliation, P11 for recovery during live trading, and P12 for a system someone is watching.

**Proves.**

1. The encoder produces exactly the request bodies the venue expects, against committed
   expectations.
2. The decoder turns committed venue frames into the same canonical messages the simulator produced.
3. The transport reconnects after a dropped session, applies backoff, and distinguishes a fatal
   status from a transient one, against a fake server.
4. A real order is placed, acknowledged, and cancelled on the venue's test network, and the capture
   becomes fixtures.
5. The seam holds: the same L5 code runs against the simulator and the venue with no branch on which
   one it is.
6. A reconnect triggers a pass, and a fill missed while disconnected is caught.

**Not in this phase.** No strategy change, no new message type, no amend path, and no mainnet. This
phase runs against the test network only.

**Gaps.** Venue specifics are verified at this phase against the venue's own documentation, because
the investigation notes are a record of one point in time. Decimal formatting is a rejection source
that a simulator never reproduces, which is why there is a fixture per order type. Part of the phase
is blocked on P14's host, since a venue that only accepts allowlisted addresses cannot be exercised
from a laptop.

### P14: deployment

**Purpose.** Run the whole fleet on one box at the latency the design claims, with real credentials,
and with a way back.

**Builds.**

- The host: cores isolated from the scheduler, one process per core, and everything that steals
  cycles away from them kept off.
- One systemd unit per process, including the media driver, with ordering, restart policy,
  environment files, and hardening.
- The core allocation for the box, written once, consistent with every `core:` value and every
  `CPUAffinity=`, with each process refusing to start when its configured core is not in the allowed
  set.
- Secrets: `/etc/dragonfly` with the right ownership and modes, one environment file per process,
  the gateway variant that aborts at boot when credentials are missing, and separate files for
  testnet and mainnet so a wrong variable cannot reach the wrong network.
- Mainnet validation: the first real order at the smallest size the venue permits, from an
  allowlisted address, cancelled immediately.
- The latency measurement: a defined path, stated percentiles, a committed script, and a recorded
  baseline from the target hardware with a reproducibility tolerance.
- Rollback as one command, with the binary and config shipped as one versioned artifact, and the
  snapshot and log carrying a format version that a binary refuses to read when it does not
  recognise it.
- Data and retention wiring, so the log and snapshot directories are the ones P11's policy assumes.

**Technologies.** systemd, `isolcpus` and its kernel command line, per-process environment files,
Google Benchmark for the measurement harness, and containers for CI and replay only.

**Depends on.** Every phase before it.

**Proves.**

1. The fleet starts in dependency order, the media driver first, and every process reaches
   `Operating`.
2. Latency is measured on the metal, the figures are recorded, and the script that produced them is
   in the tree.
3. A rollback returns to the previous version and restarts cleanly, and the ledger reconciles
   afterwards.
4. The first mainnet order is placed at the minimum size and cancelled, with the capture kept.
5. No secret exists in the repository or the image, and a missing credential stops a gateway at
   boot.

**Not in this phase.** No feature, no message, and no logic that does not already exist and
have a test.

**Gaps.** The latency target is a measurement rather than a number, because the design never
committed to a budget. CI cannot measure latency, so the benchmark runs there to prove the harness
works and the baseline comes from the target box, compared by hand at release.
