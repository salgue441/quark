add_executable(fuzz_hp_retire hp_retire.cpp)
target_link_libraries(fuzz_hp_retire PRIVATE quark::quark quark::warnings)
target_compile_options(fuzz_hp_retire PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
target_link_options(fuzz_hp_retire PRIVATE -fsanitize=fuzzer,address)
