Replay engine: drives `SimulationClock` to each event's timestamp before delivery,
and reads the append-only binary log. Shares the `IFeedHandler` seam with the live
handler (phases.md, "P4").

Phase and build order: see the map in `core/CMakeLists.txt`.
