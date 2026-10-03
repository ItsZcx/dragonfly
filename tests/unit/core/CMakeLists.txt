# tests/unit/core — aggregates unit tests for core/ components.
#
# An intermediate level: it defines no targets, it only descends. See
# tests/CMakeLists.txt for the layout rule.
#
# Add one line per core/ component that has unit tests. The line is added in
# the same change that creates the component's directory.

# add_subdirectory(clock)      # core/clock      -> df_core_clock_tests
# add_subdirectory(ems)        # core/ems        -> df_core_ems_tests
# add_subdirectory(feed)       # core/feed       -> df_core_feed_tests
# add_subdirectory(ipc)        # core/ipc        -> df_core_ipc_tests
# add_subdirectory(oms)        # core/oms        -> df_core_oms_tests
# add_subdirectory(replay)     # core/replay     -> df_core_replay_tests
# add_subdirectory(risk)       # core/risk       -> df_core_risk_tests
# add_subdirectory(runtime)    # core/runtime    -> df_core_runtime_tests
# add_subdirectory(strategy)   # core/strategy   -> df_core_strategy_tests
add_subdirectory(types)        # core/types      -> df_core_types_tests