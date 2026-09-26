Layer 1. `IFeedHandler` with two implementations of the same seam:
`ReplayFeedHandler` (binary log) and `LiveFeedHandler` (venue socket). Replay first,
so CI needs no credentials or network (operations.md, "The phase plan").

Phase and build order: see the map in `core/CMakeLists.txt`.
