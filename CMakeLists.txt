cmake_minimum_required(VERSION 3.25)

project(
  quark
  VERSION 0.1.0
  DESCRIPTION "Modern C++23 lock-free concurrent data structures"
  LANGUAGES CXX
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include/quark/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/quark/core/version.hpp.in"
  "${CMAKE_BINARY_DIR}/include/quark/core/version.hpp"
  @ONLY
)

# Prefer target properties over global CMAKE_CXX_STANDARD for consumers,
# but keep these as a project default for our own targets.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QUARK_BUILD_TESTS "Build test suite" ON)
option(QUARK_BUILD_BENCH "Build Google Benchmark harness" OFF)
option(QUARK_LOGGING "Enable structured logging" ON)
set(QUARK_SANITIZER "" CACHE STRING
    "Sanitizer instrumentation: empty | address | thread | undefined")
set_property(CACHE QUARK_SANITIZER PROPERTY STRINGS "" address thread undefined)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# -- Warnings / definitions (for our targets; not forced on all consumers) ----
add_library(quark_warnings INTERFACE)
add_library(quark::warnings ALIAS quark_warnings)

target_compile_options(quark_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
    -Wall -Wextra -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
  >
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /permissive-
    /Zc:preprocessor
    /wd4324
  >
)

# -- Sanitizers ---------------------------------------------------------------
add_library(quark_sanitize INTERFACE)
add_library(quark::sanitize ALIAS quark_sanitize)

if(QUARK_SANITIZER)
  if(MSVC)
    message(WARNING "QUARK_SANITIZER is not supported with MSVC — ignoring")
  else()
    message(STATUS "Sanitizer enabled: ${QUARK_SANITIZER}")
    target_compile_options(quark_sanitize INTERFACE
      -fsanitize=${QUARK_SANITIZER}
      -fno-omit-frame-pointer
      -g
    )
    target_link_options(quark_sanitize INTERFACE
      -fsanitize=${QUARK_SANITIZER}
    )
  endif()
endif()

# -- Header-only library ------------------------------------------------------
add_library(quark INTERFACE)
add_library(quark::quark ALIAS quark)

target_compile_features(quark INTERFACE cxx_std_23)

# Required for correct __VA_OPT__ expansion in logging macros under MSVC.
target_compile_options(quark INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
)

target_include_directories(quark INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(quark INTERFACE
  $<$<BOOL:${QUARK_LOGGING}>:QUARK_LOGGING_ENABLED=1>
  $<$<NOT:$<BOOL:${QUARK_LOGGING}>>:QUARK_LOGGING_ENABLED=0>
)

find_package(Threads REQUIRED)
target_link_libraries(quark INTERFACE Threads::Threads)

# -- Tests / benchmarks -------------------------------------------------------
if(QUARK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(QUARK_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# -- Install / package export -------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/include/quark"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  PATTERN "version.hpp.in" EXCLUDE
)

install(
  FILES "${CMAKE_BINARY_DIR}/include/quark/core/version.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/quark/core"
)

install(
  TARGETS quark quark_warnings quark_sanitize
  EXPORT quarkTargets
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  EXPORT quarkTargets
  FILE quarkTargets.cmake
  NAMESPACE quark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/quark"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/quarkConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/quarkConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/quarkConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/quark"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/quarkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/quarkConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/quark"
)
