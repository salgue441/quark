include(benchmark)

# Add sources here as they land, e.g.:
#   quark_add_benchmark(NAME quark_bench SOURCES bench_main.cpp)

message(STATUS
  "QUARK_BUILD_BENCH=ON — add sources under bench/ and call "
  "quark_add_benchmark() in bench/CMakeLists.txt")
