# Operations

**Audience:** anyone building, running, or configuring the system.

This file covers how to build and run the system, and the configuration files it reads. The order in
which the system is built, and what each phase delivers, is in [`phases.md`](phases.md).

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

---

## Build and run

The build uses Clang, CMake, Ninja, vcpkg in manifest mode, and `pkg-config`.

The presets do not set `CXX`. `scripts/build.sh` points it at Homebrew `llvm@22` when that is
installed, and CI installs LLVM 22 and sets `CXX=clang++`. The compiler, the formatter, and the
linter therefore come from one LLVM release, which is what keeps local and CI output identical.

GCC is not a supported compiler. It implements the same warning flags differently, so a construct
that builds under Clang can fail under GCC: `-Wpedantic` reports `__int128` on GCC and not on
Clang, and `-Werror` makes that fatal.

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
==> linting 12 translation units (clang-tidy 22.1.8)
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
