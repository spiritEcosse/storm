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
        "${CMAKE_CURRENT_SOURCE_DIR}/common"  # Common benchmark utilities
    )
endfunction()

# Unified benchmark system (C++26, custom clang, libc++)
# This is the main benchmark management system that uses Storm ORM
# to manage test registry and performance history
add_storm_benchmark(bench_unified "bench_unified.cpp")

# Low-level microbenchmarks for performance analysis
add_storm_benchmark(sql_generation_microbench "sql_generation_microbench.cpp")
add_storm_benchmark(join_performance_microbench "join_performance_microbench.cpp")

message(STATUS "Benchmark executables: bench_unified, sql_generation_microbench, join_performance_microbench")
