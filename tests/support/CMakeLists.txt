# tests/support — scaffolding shared by two or more test components.
#
# Reserved and empty by design. It exists so that the shared location is fixed
# before anything needs it, rather than each component inventing its own.
#
# An INTERFACE library, matching how df::contracts and df::types carry their
# own include paths: a consuming test target links one name, df_test_support,
# rather than reaching for a file path.
#
# The first inhabitant goes here when a helper is needed by tests in two or
# more components. Until then, a helper used by one component belongs inside
# that component's own directory, and moves here only when a second component
# wants it.

add_library(df_test_support INTERFACE)
add_library(df::test_support ALIAS df_test_support)

target_include_directories(df_test_support INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
