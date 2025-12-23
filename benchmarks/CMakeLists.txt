project(storm_benchmarks LANGUAGES CXX)

message(STATUS "Configuring benchmarks")

# Helper function for Storm ORM benchmarks with custom clang
function(add_storm_benchmark target_name source_file)
    add_executable(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
    target_link_libraries(${target_name} PRIVATE
        storm
        pthread
    )
    link_sqlite(${target_name})
    apply_clang_flags(${target_name})
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )
endfunction()

# Storm ORM Compile-Time Benchmark System
# Loads tests from JSON at compile time, zero runtime overhead
add_storm_benchmark(storm_bench "main.cpp")

message(STATUS "Benchmark executable: storm_bench")
