Backtest harness entry point. Drives `core/replay/` to feed a historical binary log
through the *same* Layers 2-5 as live, using `SimulationClock` so timestamp-dependent
logic behaves identically (phases.md, "P4").

Phase and build order: see the map in `core/CMakeLists.txt`.
