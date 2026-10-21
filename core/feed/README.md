Layer 1. `IFeedHandler` with two implementations of the same seam:
`ReplayFeedHandler` (binary log) and `LiveFeedHandler` (venue socket). Replay first,
so CI needs no credentials or network (phases.md, "P4").

Phase and build order: see the map in `core/CMakeLists.txt`.
