Async logging (Quill), `pthread_setaffinity_np` core pinning, config loading and
validation, signal handling. Grouped with `ipc` at P3 because the Aeron sink
implementations need the logger, and P3 is the transport phase.

Phase and build order: see the map in `core/CMakeLists.txt`.
