add_library(df_clock STATIC src/clock.cpp)
add_library(df::clock ALIAS df_clock)

target_include_directories(df_clock PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
