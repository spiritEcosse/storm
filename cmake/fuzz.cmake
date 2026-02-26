option(ENABLE_FUZZING
       "Build libFuzzer harnesses (requires -fsanitize=address,fuzzer support)"
       OFF)

if(ENABLE_FUZZING)
  # Compile the storm library with the same instrumentation flags as the fuzz
  # harnesses (-fsanitize=address,fuzzer-no-link). This is required because:
  #
  # 1. clang's module cache hash is derived from compilation flags.  If the storm
  #   library and fuzz harnesses have DIFFERENT flags, _Builtin_stddef (and
  #   similar built-in modules) are cached under different hash sub-
  #   directories.  When loading the storm BMI in a fuzz harness compilation,
  #   clang finds built-in modules in BOTH directories and emits a fatal
  #   "defined in both" error.
  #
  # 1. Using fuzzer-no-link here (rather than plain fuzzer) means the fuzzer
  #   runtime is NOT linked into libstorm.a — it is linked only into each fuzz
  #   harness binary (via target_link_options in fuzz/CMakeLists.txt).
  #
  # 1. A shared, build-local module cache (module-cache) keeps all fuzz-build
  #   module I/O out of the global ~/.cache/clang/ModuleCache/ to avoid
  #   cross-preset contamination.
  set(FUZZ_INSTRUMENT_FLAGS
      -fsanitize=address,fuzzer-no-link -g -fno-omit-frame-pointer
      "-fmodules-cache-path=${CMAKE_BINARY_DIR}/module-cache")
  target_compile_options(storm PRIVATE ${FUZZ_INSTRUMENT_FLAGS})
  target_link_options(storm PRIVATE -fsanitize=address -g
                      -fno-omit-frame-pointer)
  message(STATUS "Fuzzing is enabled.")
  add_subdirectory(fuzz)
else()
  message(STATUS "Fuzzing is disabled.")
endif()
