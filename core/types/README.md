Fixed-point price and quantity arithmetic (`SCALE = 1e9`, see contracts.md,
"Numbers are fixed-point integers") and strong ID types (`InstrumentId`, `ClientOrderId`,
`ExchangeOrderId`, and the rest of `df/types/ids.hpp`).
No allocation; header-only where practical.

Phase and build order: see the map in `core/CMakeLists.txt`.
