include(catch2)

quark_add_catch_test(NAME test_memory_safety SOURCES test_memory_safety.cpp)
quark_add_catch_test(NAME test_move_only_function SOURCES test_move_only_function.cpp)
quark_add_catch_test(NAME test_backoff SOURCES test_backoff.cpp)
quark_add_catch_test(NAME test_version SOURCES test_version.cpp)
quark_add_catch_test(NAME test_spsc_queue SOURCES test_spsc_queue.cpp)
quark_add_catch_test(NAME test_ms_queue SOURCES test_ms_queue.cpp)

# Expected-abort coverage: CTest WILL_FAIL does not invert SIGABRT/Exception,
# so drive the binary through expect_abort.cmake on all platforms.
add_executable(abort_destroy_with_handles abort/destroy_with_handles.cpp)
target_link_libraries(abort_destroy_with_handles PRIVATE
  quark::quark
  quark::warnings
  quark::sanitize
)
add_test(
  NAME abort_destroy_with_handles
  COMMAND ${CMAKE_COMMAND}
    -DTEST_COMMAND=$<TARGET_FILE:abort_destroy_with_handles>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_abort.cmake
)
