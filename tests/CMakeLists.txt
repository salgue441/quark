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

add_executable(test_move_only_function test_move_only_function.cpp)
target_link_libraries(test_move_only_function PRIVATE quark::quark quark::warnings)
add_test(NAME move_only_function COMMAND test_move_only_function)

# Intentional abort: domain destroyed while a thread handle is still live.
# WILL_FAIL TRUE → CTest passes when the process exits non-zero / is aborted.
# Skipped on Windows: MSVC abort() raises 0xC0000409 (Exception), which CTest
# does not invert via WILL_FAIL.
if(NOT WIN32)
  add_executable(abort_destroy_with_handles abort/destroy_with_handles.cpp)
  target_link_libraries(abort_destroy_with_handles PRIVATE
    quark::quark
    quark::warnings
  )
  add_test(NAME abort_destroy_with_handles COMMAND abort_destroy_with_handles)
  set_tests_properties(abort_destroy_with_handles PROPERTIES WILL_FAIL TRUE)
endif()
