# tests/unit/contracts — unit tests for contracts/ (see tests/CMakeLists.txt).
#
# contracts/ sits at the source root, so it is a sibling of core/ here.
#
# Target: df_contracts_tests. Named for the component path, with no `unit`
# segment: the kind is a property of the binary, not the component.
#
# df_contracts_tests is where the "every message type is covered, and adding one
# without covering it is detectable" obligation lands (docs/contracts.md,
# "Every message is value-initialized before use").

if(DF_HAVE_GTEST)
    add_executable(df_contracts_tests
        common_test.cpp
        control_test.cpp
        execution_test.cpp
        header_test.cpp
        market_data_test.cpp
        orders_test.cpp
        signals_test.cpp)
    target_link_libraries(df_contracts_tests PRIVATE df::contracts GTest::gtest_main)

    target_compile_options(df_contracts_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_test(NAME df_contracts_tests COMMAND df_contracts_tests)
else()
    message(STATUS "tests/unit/contracts: skipped (GTest not found)")
endif()
