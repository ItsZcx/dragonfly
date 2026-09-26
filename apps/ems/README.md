Layer 5 process entry point. One process per venue, pinned to its own core, reading
`config/gateways/<venue>.yaml` and filtering incoming orders by `venue_id`
(contracts.md, `NewOrderMsg`).

Phase and build order: see the map in `core/CMakeLists.txt`.
