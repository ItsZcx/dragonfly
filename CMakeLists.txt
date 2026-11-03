cmake_minimum_required(VERSION 3.25)

project(dragonfly
    VERSION 0.1.0
    DESCRIPTION "High performance, market-agnostic quantitative trading system"
    LANGUAGES CXX)

# ---- Global compile contract ------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror -Wpedantic)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---- Dependencies (vcpkg manifest mode) -------------------------------------

find_package(Threads REQUIRED)

set(DF_HAVE_GTEST FALSE)
find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    set(DF_HAVE_GTEST TRUE)
endif()

set(DF_HAVE_BENCHMARK FALSE)
find_package(benchmark CONFIG QUIET)
if(benchmark_FOUND)
    set(DF_HAVE_BENCHMARK TRUE)
endif()

if(NOT DF_HAVE_GTEST OR NOT DF_HAVE_BENCHMARK)
    message(STATUS "")
    message(STATUS "Missing C++ dependencies:")
    if(NOT DF_HAVE_GTEST)
        message(STATUS "  - GTest")
    endif()
    if(NOT DF_HAVE_BENCHMARK)
        message(STATUS "  - benchmark")
    endif()
    message(STATUS "")
    message(STATUS "Run ./scripts/bootstrap.sh, then ./scripts/build.sh <preset>.")
    message(STATUS "Available presets: dev, release, debug, tsan.")
    message(STATUS "")
endif()

# ---- Targets ----------------------------------------------------------------

add_subdirectory(contracts)
add_subdirectory(core)
add_subdirectory(apps)

# ---- Tests ------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
add_subdirectory(bench)
