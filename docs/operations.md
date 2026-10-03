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
  - [What each phase must prove](#what-each-phase-must-prove)

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

| Key                     | Tier                         | Meaning                                                         |
| ----------------------- | ---------------------------- | --------------------------------------------------------------- |
| `kill_switch_active`    | global                       | the breaker latch, set at boot and toggled by a control command |
| `max_orders_per_second` | global, per_strategy         | token-bucket rate limit                                         |
| `max_daily_loss`        | global, per_strategy         | the daily-loss breaker                                          |
| `max_open_orders`       | global, per_strategy         | cap on resting orders                                           |
| `max_order_qty`         | per_instrument, per_strategy | cap on a single order                                           |
| `max_position_qty`      | per_instrument, per_strategy | cap on absolute net position                                    |
| `price_collar_bps`      | per_instrument               | how far from the mid an order may be priced                     |
| `allow_market_orders`   | per_instrument               | whether market orders are permitted                             |

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

The system is built in phases. Each phase is proven before the next one starts.

| Phase   | Builds                                          | Proven by                                                                    |
| ------- | ----------------------------------------------- | ---------------------------------------------------------------------------- |
| **P0**  | repository skeleton, toolchain, one smoke test  | the build and `ctest` are green                                              |
| **P1**  | the contract structs and the fixed-point types  | sizes and offsets match, arithmetic is exact                                 |
| **P2**  | the clock interface, live and simulated         | the simulated clock advances deterministically, the live one is monotonic    |
| **P3**  | runtime, IPC, sinks, and the audit recorder     | publish and subscribe over a real bus, byte-identical receipt, gap detection |
| **P4**  | replay, then the live feed handler              | a replayed fixture produces exact normalized messages                        |
| **P5**  | a minimal monitor                               | replayed market data is visible                                              |
| **P6**  | L3 (risk) and its state mirror                  | each check produces its exact reason code, and the ordering is observable    |
| **P7**  | L4 (OMS) and L5 (EMS) against a simulated venue | a synthetic signal runs to a fill, and the ledger is deterministic           |
| **P8**  | L2 (strategy), then the Python bridge           | a trivial strategy trades end to end on replayed data                        |
| **P9**  | reconciliation                                  | a deliberate mismatch blocks trading, a clean pass reopens it                |
| **P10** | recovery, snapshots, and the log                | a killed process restarts and reconstructs identically                       |
| **P11** | the full monitor                                | the kill switch cancels and blocks new orders end to end                     |
| **P12** | deployment                                      | latency measured on bare metal, and a one-command rollback                   |

The order is not the layer order, for four reasons.

**Contracts come first.** Every process embeds the message layouts, and the audit log stores them.
A layout change after P4 invalidates every test and every recorded log, so the data is frozen
before anything depends on it.

**Replay comes before any live feed.** Replay is deterministic and needs no credentials, so it is
testable in CI and it forces the live adapter to be a thin second implementation of the same seam.

**Risk comes before strategy.** L3 has the clearest specification and can be tested with synthetic
signals, with no strategy involved. Building it first means the strategy is written against a
fixed, proven gate.

**Reconciliation is its own phase, before recovery.** They share only the word "restart". Recovery
asks "can I rebuild my own state?", which is internal and deterministic. Reconciliation asks "does
my state agree with the venue's?", which is external and produces a trading gate. Putting the gate
in place before recovery starts restarting processes means a restart always lands somewhere that
knows how to refuse to trade.

### What each phase must prove

Some phases are proven by behaviour that does not fit in a table cell. Those obligations are here.
Each one names the document that states the rule, so the test checks a written contract rather
than the implementer's memory.

**P3, runtime, IPC, and the recorder**

| #   | Test                                                                                                                                                                                                        | Rule                                                            |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| 1   | The recorder writes a log that replays byte-identically.                                                                                                                                                    | `contracts.md`, "Timestamps"                                    |
| 2   | A killed recorder leaves a sequence-gap marker in the log rather than a silent hole.                                                                                                                        | `layers.md`, L6                                                 |
| 3   | Two processes started on different reference-data hashes: the second refuses to enter `trading`, and L6 (observability) raises the skew.                                                                    | `contracts.md`, `HeartbeatMsg`                                  |
| 4   | Value-initialize each message type twice with identical fields, and assert the bytes are equal.                                                                                                             | `contracts.md`, "Every message is value-initialized before use" |
| 5   | Kill a producer mid-stream and restart it. The consumer resets that `producer_id`'s watermark when the sequence goes backwards and the timestamp goes forward, and treats a forward sequence jump as a gap. | `contracts.md`, "Sequence numbers across a restart"             |
| 6   | With a subscriber kept deliberately slow: `md` and `signals` drop and count, `orders` fails closed by latching the kill switch rather than dropping an order, and `control` retries until delivered.        | `contracts.md`, "Publication failure is per-channel"            |

Test 6 needs care, because it asserts a negative: that an order was **not** silently dropped. A
pass requires observing the latch, not merely the absence of an error.

**P7, order lifecycle and fills**

| #   | Test                                                                                                                                                                                                 | Rule                                 |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| 1   | A pending replace releases nothing while in flight. A rejected amendment reverts the terms with the reservation untouched. A reduce is always admissible, and an increase is checked against budget. | `layers.md`, L4 (OMS)                |
| 2   | Deliver the same `ExecutionReportMsg` twice. Settled cash, fees, filled quantity, and realised P&L are unchanged, and the duplicate counter increments.                                              | `contracts.md`, `ExecutionReportMsg` |

**P8, strategy registration**

| #   | Test                                                                                                                                                                      | Rule                       |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------- |
| 1   | An unknown `impl` fails at startup with the available keys listed.                                                                                                        | `layers.md`, L2 (strategy) |
| 2   | A `params` typo fails schema validation before construction.                                                                                                              | `layers.md`, L2 (strategy) |
| 3   | A duplicate `strategy_id` is rejected at boot, naming the offending file and id. A `strategy_id` of 65536 or more is rejected too, because it would truncate and collide. | `layers.md`, L2 (strategy) |

**P10, recovery, snapshots, and ledger adjustments**

| #   | Test                                                                                                                                                          | Rule                                 |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| 1   | Kill a process after an abandonment, and restart. The reconstructed ledger holds the released reservation, because the `OrderAbandonedMsg` is in the log.     | `contracts.md`, "Ledger adjustments" |
| 2   | Kill a process after an adopted reconciliation correction, and restart. The reconstruction matches, rather than reverting to the state before the correction. | `contracts.md`, "Ledger adjustments" |
| 3   | With the `ledger` publication forced to fail, the live ledger does not apply the adjustment. This proves the publish-before-apply ordering.                   | `contracts.md`, "Ledger adjustments" |
| 4   | Replaying a log with a duplicated adjustment changes nothing, because the operations are assignments or terminal transitions rather than accumulations.       | `contracts.md`, "Ledger adjustments" |
