# tests/unit — per-component unit tests.
#
# The layout rule is stated once, in tests/CMakeLists.txt. This file only
# aggregates its subdirectories; it defines no targets. Each component gets its
# own directory here, mirroring the source tree, and owns its own target.

add_subdirectory(contracts)   # contracts/
add_subdirectory(core)        # core/
