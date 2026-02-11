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

# Find Python interpreter
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Custom command to convert YAML to JSON
add_custom_command(
    OUTPUT "${JSON_OUTPUT}"
    COMMAND ${Python3_EXECUTABLE} "${CONVERTER_SCRIPT}" "${YAML_SOURCE}" "${JSON_OUTPUT}"
    DEPENDS "${YAML_SOURCE}" "${CONVERTER_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Converting benchmark_tests.yaml to JSON..."
    VERBATIM
)

# Custom target for the conversion (allows manual regeneration)
add_custom_target(generate_benchmark_json
    DEPENDS "${JSON_OUTPUT}"
)

# Helper function for Storm ORM benchmarks with custom clang
function(add_storm_benchmark target_name source_file)
    add_executable(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
    target_link_libraries(${target_name} PRIVATE
        storm
        pthread
    )
    link_sqlite(${target_name})
    link_postgresql(${target_name})
    apply_clang_flags(${target_name})
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/third_party"
    )
endfunction()

# Storm ORM Compile-Time Benchmark System
# Loads tests from JSON at compile time, zero runtime overhead
add_storm_benchmark(storm_bench "main.cpp")

# Ensure JSON is generated before building storm_bench
add_dependencies(storm_bench generate_benchmark_json)

# Increase constexpr steps limit for compile-time JSON parsing
# Required due to large number of benchmark tests parsed at compile time
target_compile_options(storm_bench PRIVATE -fconstexpr-steps=20000000)

message(STATUS "Benchmark executable: storm_bench")
