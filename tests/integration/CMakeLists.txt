# tests/integration — tests that span more than one process (see tests/CMakeLists.txt).
#
# A test belongs here when it needs a real local bus and more than one process.
# Same layout rule as unit/: the path of the source under test, beneath this
# kind namespace. Targets are named <component>_integration_tests, and the
# second process wiring lands with the first real test rather than here.
#
# The README in this directory says "against a real local Aeron driver". The
# transport is not decided or vendored yet (P3), so treat that as intent to be
# confirmed, not as settled fact.

# add_subdirectory(core)       # core/     -> df_core_*_integration_tests
# add_subdirectory(contracts)  # contracts/ -> df_contracts_integration_tests
