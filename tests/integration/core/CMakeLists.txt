# tests/integration/core — aggregates integration tests for core/ components.
#
# An intermediate level: it defines no targets, it only descends. See
# tests/CMakeLists.txt for the layout rule.
#
# Each component's line is added in the same change that creates its directory.
# The name is the unit target plus an _integration_tests suffix:
#
#   core/oms   -> df_core_oms_integration_tests
#   core/risk  -> df_core_risk_integration_tests
#
# A component with no integration test simply has no directory and no line.
