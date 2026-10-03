# tests — GoogleTest suites (operations.md, "Build and run")
#
# THE LAYOUT RULE LIVES HERE. Nothing else restates it.
#
#   tests/<kind>/<source path>/...
#
# A test file lives under the path of the source it tests, beneath one of two
# kind namespaces:
#
#   unit/         one process, in-memory sinks, no bus
#   integration/  multiple processes against a real local bus
#
# So a test of core/types/include/df/types/fixed.hpp lands at
# tests/unit/core/types/, and a test of contracts/include/df/msg/header.hpp
# lands at tests/unit/contracts/. contracts/ and core/ are siblings in the
# source tree, so they are siblings here.
#
# Two rules follow from that, and both are deliberate:
#
#   1. `unit` and `integration` name a KIND of test, never a component. They
#      are namespaces, not owners, so they appear in a path but never in a
#      target name or a gtest suite name. A target is named for the component
#      it tests: core/types -> df_core_types_tests. The binary already tells
#      you which kind it is, so encoding the kind twice would let the two
#      disagree.
#
#   2. One executable per component, not one for the tree and not one per top
#      level. A per-component binary links only what it needs, so a types test
#      cannot reach into L4 by accident, and a red binary names the component
#      that broke.
#
# Every directory level carries its own CMakeLists.txt, mirroring core/. The
# intermediate levels are a deliberate cost: they let a component be added by
# touching one file in one place, rather than editing a central list that must
# track the whole nested path. Adding a component means adding its directory,
# its CMakeLists.txt, and one add_subdirectory line in its parent.
#
# tests/support/ is an INTERFACE library (df_test_support) for scaffolding that
# two or more components share. It is reserved and empty by design.
#
# tests/p0_smoke.cpp is the one exception to the rule above. It proves the
# toolchain end to end before any real code exists, so it belongs to no
# component and stays at this root. It is retired once the phases it
# pre-verifies have real tests.

# The toolchain smoke test. Belongs to no component, so it stays at the root.
if(DF_HAVE_GTEST)
    add_executable(df_p0_smoke p0_smoke.cpp)
    target_link_libraries(df_p0_smoke PRIVATE GTest::gtest_main)

    target_compile_options(df_p0_smoke PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_test(NAME p0_smoke COMMAND df_p0_smoke)
else()
    message(STATUS "tests: skipped (GTest not found — run ./scripts/bootstrap.sh)")
endif()

add_subdirectory(support)
add_subdirectory(unit)
add_subdirectory(integration)
