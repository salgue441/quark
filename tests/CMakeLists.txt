# Standalone harness (own main). Catch2 helpers live in cmake/catch2.cmake
# for future TEST_CASE-based suites.

add_executable(test_memory_safety test_memory_safety.cpp)

target_link_libraries(test_memory_safety PRIVATE
  quark::quark
  quark::warnings
  quark::sanitize
)

add_test(
  NAME memory_safety
  COMMAND test_memory_safety
)

set_tests_properties(memory_safety PROPERTIES
  LABELS "unit;memory"
)
