# apps — one thin executable per layer process (layers.md)
#
# Each executable is a thin main() that wires a core/ library to Aeron and
# starts its event loop. Logic lives in core/, never here, so tests can link
# the library without a process boundary.
#
# Phase map: see the single source of truth in core/CMakeLists.txt.
#
#   P4  feed_handler     L1: replay-capable feed handler
#   P4  replay           backtest harness entry point (drives SimulationClock)
#   P6  risk_engine      L3: risk firewall
#   P7  oms  ems         L4 ledger, L5 execution (EMS is one process per venue)
#   P8  strategy_engine  L2: strategy
#
# Layer 6 (monitoring) has no C++ executable: it lives in python/.
# Each subdirectory defines its own executable target. Uncomment a line in the
# same change that creates the subdirectory's CMakeLists.txt.

# add_subdirectory(feed_handler)     # P4
# add_subdirectory(replay)           # P4
# add_subdirectory(risk_engine)      # P6
# add_subdirectory(oms)              # P7
# add_subdirectory(ems)              # P7
# add_subdirectory(strategy_engine)  # P8
