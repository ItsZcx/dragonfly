Layer 5 execution EMS: `IEMS`, order lifecycle state machine, venue protocol codec,
and the single quantization boundary where prices and quantities are snapped to the
target venue's tick and lot size (operations.md, "Reference data"). One instance per venue, filtered by
`venue_id` (contracts.md, `NewOrderMsg`).

Phase and build order: see the map in `core/CMakeLists.txt`.
