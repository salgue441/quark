# cmake/benchmark.cmake
#
# Fetches Google Benchmark and provides quark_add_benchmark() for harness
# executables under bench/.
#
# @author Carlos Salguero
# @date 2026-07-18

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.4
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(benchmark)

function(quark_add_benchmark)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})

  if(NOT ARG_NAME OR NOT ARG_SOURCES)
    message(FATAL_ERROR "quark_add_benchmark requires NAME and SOURCES")
  endif()

  add_executable(${ARG_NAME} ${ARG_SOURCES})
  target_link_libraries(${ARG_NAME} PRIVATE
    quark::quark
    quark::warnings
    benchmark::benchmark
    benchmark::benchmark_main
  )
  target_compile_options(${ARG_NAME} PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Release>>:
      -O3 -march=native -fno-omit-frame-pointer
    >
  )
endfunction()
