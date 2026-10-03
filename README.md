# Dragonfly

An institutional-grade, market-agnostic, polyglot quantitative trading system.

Six layers run as separate processes and communicate over a message bus. Each layer owns one stage
of the path from market data to an executed trade. Money and state cross process boundaries only
as fixed-size messages.

| Layer  | Name          | Job                                                          | Language     |
| ------ | ------------- | ------------------------------------------------------------ | ------------ |
| **L1** | Ingestion     | Turns venue data into normalized messages and keeps the book | C++          |
| **L2** | Strategy      | Turns market observations into trading intentions            | C++ / Python |
| **L3** | Risk          | Approves or rejects each intention                           | C++          |
| **L4** | OMS           | Records positions, cash, orders, and P&L                     | C++          |
| **L5** | EMS           | Sends orders to the venue and reports the outcomes           | C++          |
| **L6** | Observability | Shows what is happening and lets a human stop it             | Python       |

Throughout these documents a layer is written as `L<n>` with its name in parentheses, such as
`L3 (risk)` or `L5 (EMS)`.

## Documentation

| Document                                   | Read it for                                                                           |
| ------------------------------------------ | ------------------------------------------------------------------------------------- |
| [`docs/overview.md`](docs/overview.md)     | The runtime picture: layers, processes, channels, and one tick end to end. Start here |
| [`docs/contracts.md`](docs/contracts.md)   | The message set: every field of every message, and the conventions they follow        |
| [`docs/layers.md`](docs/layers.md)         | What each layer does, what it must not do, and how it fails                           |
| [`docs/lifecycle.md`](docs/lifecycle.md)   | Three worked flows: a tick, an order, and a restart                                   |
| [`docs/decisions.md`](docs/decisions.md)   | Why the system is built this way, one decision per entry                              |
| [`docs/operations.md`](docs/operations.md) | Build and run, the configuration files, and the phase plan                            |

If you read one document, read `docs/overview.md`.

## Status

Phase P0: the repository skeleton and toolchain. One smoke test passes. The message contracts are
specified in `docs/contracts.md` but not yet implemented. That work is P1. The build sequence is in
`docs/operations.md`, "The phase plan".

## Toolchain

| Concern      | Choice                           |
| ------------ | -------------------------------- |
| C++ compiler | Clang (Apple Clang locally)      |
| Standard     | C++20                            |
| Meta-build   | CMake, driven by presets         |
| Build tool   | Ninja                            |
| C++ packages | vcpkg, manifest mode             |
| Host tools   | `pkg-config`, `clang-tidy`, `clang-format` |
| Python       | 3.11 or later, managed with `uv` |
| Testing      | GoogleTest                       |
| Benchmarking | Google Benchmark                 |

To build and run the tests, see `docs/operations.md`, "Build and run".
