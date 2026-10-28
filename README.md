<h1 align="center">
  <br>
  <img src="https://raw.githubusercontent.com/ItsZcx/dragonfly/master/docs/img/dragonfly-logo-black.svg" alt="Dragonfly" width="200"></a>
  <br>
  Dragonfly
  <br>
</h1>

<h4 align="center">A high performance, market-agnostic, quantitative trading system.
</h4>

<p align="center"> —</p>

<p align="center">
  <a href="#key-features">Key Features</a> •
  <a href="#documentation">Documentation</a> •
  <a href="#toolchain">Toolchain</a> •
  <a href="#status">Status</a> •
  <a href="#license">License</a>
</p>

<br>

## Key Features

Six layers run as separate processes and communicate over a message bus. Each layer owns one stage
of the path from market data to an executed trade.

| Layer | Name | Job | Language |
| ------ | ------------- | ------------------------------------------------------------ | ------------ |
| **L1** | Ingestion | Turns venue data into normalized messages and keeps the book | C++ |
| **L2** | Strategy | Turns market observations into trading intentions | C++ / Python |
| **L3** | Risk | Approves or rejects each intention | C++ |
| **L4** | OMS | Records positions, cash, orders, and P&L | C++ |
| **L5** | EMS | Sends orders to the venue and reports the outcomes | C++ |
| **L6** | Observability | Shows what is happening and lets a human stop it | Python / TBD |

## Documentation

| Document | Read it for |
| ------------------------------------------ | ------------------------------------------------------------------------------------- |
| [`docs/overview.md`](docs/overview.md) | The runtime picture: layers, processes, channels, and one tick end to end. Start here |
| [`docs/layers.md`](docs/layers.md) | What each layer does, what it must not do, and how it fails |
| [`docs/contracts.md`](docs/contracts.md) | The message set: every field of every message, and the conventions they follow |
| [`docs/lifecycle.md`](docs/lifecycle.md) | Three worked flows: a tick, an order, and a restart |
| [`docs/operations.md`](docs/operations.md) | How to build and run, and the configuration files |
| [`docs/phases.md`](docs/phases.md) | The implementation plan: sixteen phases, what each delivers, and what each one proves |
| [`docs/decisions.md`](docs/decisions.md) | Why the system is built this way, one decision per entry |

If you read one document, read `docs/overview.md`.

## Toolchain

| Concern | Choice |
| ------------ | -------------------------------- |
| C++ compiler | Clang 22 |
| Standard | C++20 |
| Meta-build | CMake, driven by presets |
| Build tool | Ninja |
| C++ packages | vcpkg, manifest mode |
| Host tools | `pkg-config`, `clang-tidy`, `clang-format` |
| Python | 3.11 or later, managed with `uv` |
| Testing | GoogleTest |
| Benchmarking | Google Benchmark |

To build, run or test the codebase, see `docs/operations.md`,

## Status

<p align="center">
  <img src="docs/img/status.svg" width="820" alt="3 of 16 phases are closed: P0, P1, and P2. P3 is in progress.">
</p>

<p align="center">
  <a href="https://github.com/ItsZcx/dragonfly/issues?q=is%3Aissue+is%3Aclosed"><img src="https://img.shields.io/github/issues-closed/ItsZcx/dragonfly?label=issues%20closed&labelColor=14172b&color=4c9be8" alt="Issues closed"></a>
  <a href="https://github.com/ItsZcx/dragonfly/pulls?q=is%3Apr"><img src="https://img.shields.io/github/issues-pr-closed/ItsZcx/dragonfly?label=pull%20requests&labelColor=14172b&color=4c9be8" alt="Pull requests closed"></a>
  <a href="https://github.com/ItsZcx/dragonfly/commits/dev"><img src="https://img.shields.io/github/commit-activity/t/ItsZcx/dragonfly?label=commits&labelColor=14172b&color=4c9be8" alt="Commits"></a>
</p>

P0 to P2 are closed: the contract, the fixed-point arithmetic, and the clock. P3 is open, and it
builds the runtime, the transport over Aeron, and the recorder. What each phase proves is in
`docs/phases.md`.

## License

<div align="center">

[`LICENSE`](LICENSE)

</div>