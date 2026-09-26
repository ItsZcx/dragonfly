Layer 1 process entry point. A thin `main()` that constructs a `LiveFeedHandler`
or `ReplayFeedHandler` from `core/feed/`, wires it to an Aeron market-data sink, and
runs the pinned event loop. Which venue it serves comes from
`config/feeds/<venue>.yaml` via `--config`.

Phase and build order: see the map in `core/CMakeLists.txt`.
