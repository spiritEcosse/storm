project(storm_benchmarks LANGUAGES CXX)

message(STATUS "Configuring benchmarks")

# CPM is already included in main CMakeLists.txt, just use it

CPMAddPackage(
    NAME SqliteOrm
    GITHUB_REPOSITORY fnc12/sqlite_orm
    GIT_TAG v1.9.1
    OPTIONS "BUILD_TESTING OFF"
)

# Build sqlite_orm wrapper as a separate C++20 library
add_library(sqlite_orm_wrapper STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sqlite_orm_wrapper.cpp"
)

set_target_properties(sqlite_orm_wrapper PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Add sqlite_orm include directory
if(SqliteOrm_SOURCE_DIR)
    target_include_directories(sqlite_orm_wrapper PRIVATE
        "${SqliteOrm_SOURCE_DIR}/include"
    )
else()
    # Fallback to expected location
    target_include_directories(sqlite_orm_wrapper PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/sqliteorm-src/include"
    )
endif()

link_sqlite(sqlite_orm_wrapper)

# Use system clang and stdlib for sqlite_orm wrapper to avoid conflicts
set_target_properties(sqlite_orm_wrapper PROPERTIES
    CXX_COMPILER /usr/bin/clang++
)

target_compile_options(sqlite_orm_wrapper PRIVATE
    -fno-modules
    -stdlib=libstdc++
)
target_link_options(sqlite_orm_wrapper PRIVATE -stdlib=libstdc++)

# Helper function for C++20 benchmarks with system clang
function(add_system_clang_benchmark target_name source_file)
    add_executable(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
    link_sqlite(${target_name})
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_COMPILER /usr/bin/clang++
        LINK_OPTIONS "-stdlib=libstdc++"
    )
    target_compile_options(${target_name} PRIVATE
        -fno-modules
        -stdlib=libstdc++
    )
    target_link_libraries(${target_name} PRIVATE pthread)
endfunction()

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

# Build separate benchmark executables to avoid module conflicts

# SQLite-only benchmark (C++20, system clang, libstdc++)
add_system_clang_benchmark(bench_sqlite "bench_sqlite.cpp")

# sqlite_orm benchmark (C++20, system clang, libstdc++)
add_system_clang_benchmark(bench_sqlite_orm "bench_sqlite_orm.cpp")
target_link_libraries(bench_sqlite_orm PRIVATE sqlite_orm_wrapper)
target_include_directories(bench_sqlite_orm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # For sqlite_orm_wrapper.h
)

# Storm ORM benchmarks (C++26, custom clang, libc++)
add_storm_benchmark(bench_storm "bench_storm.cpp")
add_storm_benchmark(sql_generation_microbench "sql_generation_microbench.cpp")
add_storm_benchmark(join_performance_microbench "join_performance_microbench.cpp")
add_storm_benchmark(bench_join "bench_join.cpp")
add_storm_benchmark(bench_distinct "bench_distinct.cpp")
add_storm_benchmark(bench_distinct_scaling "bench_distinct_scaling.cpp")
add_storm_benchmark(bench_select_comparison "bench_select_comparison.cpp")
add_storm_benchmark(bench_aggregate "bench_aggregate.cpp")

# SELECT performance analysis (C++20, system clang, libstdc++)
add_system_clang_benchmark(bench_select_analysis "bench_select_analysis.cpp")

message(STATUS "Benchmark executables: bench_sqlite, bench_sqlite_orm, bench_storm, sql_generation_microbench, join_performance_microbench, bench_join, bench_select_analysis")