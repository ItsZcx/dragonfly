add_library(df_types INTERFACE)
add_library(df::types ALIAS df_types)

target_include_directories(df_types INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
