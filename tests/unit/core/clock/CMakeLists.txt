# tests/unit/core/clock — unit tests for core/clock.
#
# Target: df_core_clock_tests. Named for the component path (core/clock), with
# no `unit` segment: the kind is a property of the binary, not the component.
# See tests/CMakeLists.txt.

if(DF_HAVE_GTEST)
    add_executable(df_core_clock_tests
        live_clock_test.cpp)
    target_link_libraries(df_core_clock_tests PRIVATE df::clock GTest::gtest_main)

    target_compile_options(df_core_clock_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_test(NAME df_core_clock_tests COMMAND df_core_clock_tests)
else()
    message(STATUS "tests/unit/core/clock: skipped (GTest not found)")
endif()
