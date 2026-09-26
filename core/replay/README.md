Replay engine: drives `SimulationClock` to each event's timestamp before delivery,
and reads the append-only binary log. Shares the `IFeedHandler` seam with the live
handler (operations.md, "The phase plan").

Phase and build order: see the map in `core/CMakeLists.txt`.
