# core — one static library target per layer (layers.md)
#
# THIS BLOCK IS THE SINGLE SOURCE OF TRUTH FOR THE PHASE -> TARGET MAP.
# Subdirectory READMEs point here rather than restating phases, so the map
# cannot drift between files. operations.md, "The phase plan" is the
# authoritative phase list; if it disagrees with this map, operations.md is
# right and this comment is wrong.
#
#   P1  df::types      fixed-point, strong ID types (phase P1)
#   P2  df::clock      IClock, LiveClock, SimulationClock (phase P2)
#   P3  df::runtime    logging, core pinning, config load, signals
#   P3  df::ipc        Aeron wrappers + concrete sink implementations
#   P4  df::feed       Layer 1 feed handlers (replay first)
#   P4  df::replay     replay engine: SimulationClock driver, binlog reader
#   P6  df::risk       Layer 3 risk firewall + state mirror
#   P7  df::oms        Layer 4 double-entry ledger
#   P7  df::ems        Layer 5 order lifecycle + venue codec
#   P8  df::strategy   Layer 2 strategy
#
# runtime is grouped with ipc at P3 because the Aeron sink implementations
# need the logger, and P3 is already the "wire up the transport" phase.
#
# Each subdirectory defines its own target. Nothing is wired yet; uncomment a
# line in the same change that creates the subdirectory's CMakeLists.txt.

# add_subdirectory(types)      # P1
# add_subdirectory(clock)      # P2
# add_subdirectory(runtime)    # P3
# add_subdirectory(ipc)        # P3
# add_subdirectory(feed)       # P4
# add_subdirectory(replay)     # P4
# add_subdirectory(risk)       # P6
# add_subdirectory(oms)        # P7
# add_subdirectory(ems)        # P7
# add_subdirectory(strategy)   # P8
