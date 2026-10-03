# tests/unit/core/types — unit tests for core/types.
#
# Target: df_core_types_tests. Named for the component path (core/types), with
# no `unit` segment: the kind is a property of the binary, not the component.
# See tests/CMakeLists.txt.

if(DF_HAVE_GTEST)
    add_executable(df_core_types_tests fixed_test.cpp)
    target_link_libraries(df_core_types_tests PRIVATE df::types GTest::gtest_main)

    target_compile_options(df_core_types_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_test(NAME df_core_types_tests COMMAND df_core_types_tests)
else()
    message(STATUS "tests/unit/core/types: skipped (GTest not found)")
endif()
