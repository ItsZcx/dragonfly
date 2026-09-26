Layer 3 process entry point. Constructs the risk firewall from `core/risk/` with
limits loaded from `config/risk_limits.yaml` and runs the pinned firewall thread.

Phase and build order: see the map in `core/CMakeLists.txt`.
