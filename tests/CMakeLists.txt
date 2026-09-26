# tests — GoogleTest suites (operations.md, "Build and run")
#
# unit/        per-layer tests with in-memory recorder sinks
# integration/ multi-process tests against a real local Aeron driver
#
# P0 ships one smoke test that proves the toolchain (Clang, C++20, CMake,
# Ninja, GoogleTest, CTest) end to end, and asserts the language and layout
# properties the message contracts depend on.

if(DF_HAVE_GTEST)
    add_executable(df_p0_smoke p0_smoke.cpp)
    target_link_libraries(df_p0_smoke PRIVATE GTest::gtest_main)

    target_compile_options(df_p0_smoke PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_test(NAME p0_smoke COMMAND df_p0_smoke)
else()
    message(STATUS "tests: skipped (GTest not found — run ./scripts/bootstrap.sh)")
endif()

# unit/ and integration/ are added as phases land.
# add_subdirectory(unit)
# add_subdirectory(integration)
