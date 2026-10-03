Scaffolding shared by two or more test components.

Reserved and empty on purpose. The rule is in `tests/CMakeLists.txt`; this file
records why the directory exists and what qualifies to live here.

A helper used by tests in one component belongs in that component's own
directory. It moves here only when a second component needs it.

The first inhabitant arrives as `include/df/test/<name>.hpp`, and consuming
test targets link `df::test_support`.
