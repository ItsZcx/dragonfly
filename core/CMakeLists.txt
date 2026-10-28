# core — one static library target per layer (layers.md)
#
# Each subdirectory defines its own target. Nothing is wired yet; uncomment a line in the same change that creates the subdirectory's CMakeLists.txt.

add_subdirectory(types)      # P1
add_subdirectory(clock)      # P2
# add_subdirectory(runtime)    # P3
# add_subdirectory(ipc)        # P3
# add_subdirectory(feed)       # P4
# add_subdirectory(replay)     # P4
# add_subdirectory(risk)       # P7
# add_subdirectory(oms)        # P8
# add_subdirectory(ems)        # P8
# add_subdirectory(strategy)   # P9
