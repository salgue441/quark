# cmake/catch2.cmake
#
# Fetches Catch2 v3 and provides quark_add_catch_test() for TEST_CASE-based
# suites. Included from tests/CMakeLists.txt when Catch2 tests are added.
#
# @author Carlos Salguero
# @date 2026-07-18

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.6.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

function(quark_add_catch_test)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})

  if(NOT ARG_NAME OR NOT ARG_SOURCES)
    message(FATAL_ERROR "quark_add_catch_test requires NAME and SOURCES")
  endif()

  add_executable(${ARG_NAME} ${ARG_SOURCES})
  target_link_libraries(${ARG_NAME} PRIVATE
    quark::quark
    quark::warnings
    quark::sanitize
    Catch2::Catch2WithMain
  )
  catch_discover_tests(${ARG_NAME})
endfunction()
