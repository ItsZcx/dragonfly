# tests/unit/contracts — unit tests for contracts/ (see tests/CMakeLists.txt).
#
# contracts/ sits at the source root, so it is a sibling of core/ here.
#
# No target yet. The first one is df_contracts_tests, added in the same change
# as the first test file. It links df::contracts and GTest::gtest_main.
#
# df_contracts_tests is where the "every message type is covered, and adding one
# without covering it is detectable" obligation lands (docs/contracts.md,
# "Every message is value-initialized before use").

# There is no test file here yet, so there is no target. Uncomment when
# tests/unit/contracts/header_test.cpp exists:
#
# if(DF_HAVE_GTEST)
#     add_executable(df_contracts_tests header_test.cpp)
#     target_link_libraries(df_contracts_tests PRIVATE df::contracts GTest::gtest_main)
#     target_compile_options(df_contracts_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
#     add_test(NAME df_contracts_tests COMMAND df_contracts_tests)
# else()
#     message(STATUS "tests/unit/contracts: skipped (GTest not found)")
# endif()
