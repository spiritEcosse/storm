project(storm_benchmarks LANGUAGES CXX)

message(STATUS "Configuring benchmarks")

# ============================================================================
# YAML to JSON conversion for benchmark tests
# ============================================================================
# benchmark_tests.yaml is the human-friendly source of truth
# benchmark_tests.json is auto-generated for C++ #embed parsing
set(YAML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_tests.yaml")
set(JSON_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_tests.json")
set(CONVERTER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/yaml_to_json.py")

find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)

add_custom_command(
  OUTPUT "${JSON_OUTPUT}"
  COMMAND ${Python3_EXECUTABLE} "${CONVERTER_SCRIPT}" "${YAML_SOURCE}"
          "${JSON_OUTPUT}"
  DEPENDS "${YAML_SOURCE}" "${CONVERTER_SCRIPT}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Converting benchmark_tests.yaml to JSON..."
  VERBATIM)

add_custom_target(generate_benchmark_json DEPENDS "${JSON_OUTPUT}")

# ============================================================================
# storm_bench — Google Benchmark entry point (Issue #235)
# ============================================================================
# Phase 1 keeps the benchmark-local .cppm module FILE_SET intact so the
# parser/registry/sizes/base/raw/schema modules continue to build (Phase 2+ will
# consume them from new fixtures). main.cpp itself imports nothing yet.
file(GLOB STORM_BENCH_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")

add_executable(storm_bench "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_sources(storm_bench PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
                                   ${STORM_BENCH_MODULE_UNITS})
# Bypass benchmark::benchmark's INTERFACE_INCLUDE_DIRECTORIES /
# INTERFACE_COMPILE_DEFINITIONS — we link the static archive directly so the
# benchmark include path + BENCHMARK_STATIC_DEFINE only apply to main.cpp via
# per-source properties below. Propagating them target-wide diverges the .cppm
# module compilation flags from libstorm.a's flags, which produces two PCM-cache
# hash subdirs both holding _Builtin_stddef.pcm and triggers a "module defined
# in both" fatal during `import storm;` (project_pcm_hash_divergence).
target_link_libraries(storm_bench PRIVATE storm pthread
                                          $<TARGET_FILE:benchmark::benchmark>)
add_dependencies(storm_bench benchmark::benchmark)
link_sqlite(storm_bench)
link_postgresql(storm_bench)
apply_clang_flags(storm_bench)
target_include_directories(
  storm_bench PRIVATE "${CMAKE_SOURCE_DIR}/src"
                      "${CMAKE_SOURCE_DIR}/third_party")

get_target_property(_benchmark_includes benchmark::benchmark
                    INTERFACE_INCLUDE_DIRECTORIES)
set_property(
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  APPEND
  PROPERTY INCLUDE_DIRECTORIES "${_benchmark_includes}")
set_property(
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  APPEND
  PROPERTY COMPILE_DEFINITIONS "BENCHMARK_STATIC_DEFINE")

add_dependencies(storm_bench generate_benchmark_json)

message(STATUS "Benchmark executable: storm_bench")
