include(catch2)

quark_add_catch_test(NAME test_memory_safety SOURCES test_memory_safety.cpp)
quark_add_catch_test(NAME test_move_only_function SOURCES test_move_only_function.cpp)
quark_add_catch_test(NAME test_backoff SOURCES test_backoff.cpp)
quark_add_catch_test(NAME test_version SOURCES test_version.cpp)

if(NOT WIN32)
  add_executable(abort_destroy_with_handles abort/destroy_with_handles.cpp)
  target_link_libraries(abort_destroy_with_handles PRIVATE
    quark::quark quark::warnings quark::sanitize)
  add_test(NAME abort_destroy_with_handles COMMAND abort_destroy_with_handles)
  set_tests_properties(abort_destroy_with_handles PROPERTIES WILL_FAIL TRUE)
endif()
