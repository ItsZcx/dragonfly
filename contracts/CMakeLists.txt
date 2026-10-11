# contracts — single source of truth for binary message layouts (contracts.md)
#
# Header-only interface target. Every other target depends on df::contracts.

add_library(df_contracts INTERFACE)
add_library(df::contracts ALIAS df_contracts)

target_include_directories(df_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
