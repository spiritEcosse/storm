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

# Build separate benchmark executables to avoid module conflicts

# SQLite-only benchmark (C++20, system clang, libstdc++)
add_executable(bench_sqlite "${CMAKE_CURRENT_SOURCE_DIR}/bench_sqlite.cpp")
link_sqlite(bench_sqlite)
set_target_properties(bench_sqlite PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_COMPILER /usr/bin/clang++
    LINK_OPTIONS "-stdlib=libstdc++"
)
target_compile_options(bench_sqlite PRIVATE
    -fno-modules
    -stdlib=libstdc++
)

# sqlite_orm benchmark (C++20, system clang, libstdc++)
add_executable(bench_sqlite_orm "${CMAKE_CURRENT_SOURCE_DIR}/bench_sqlite_orm.cpp")
target_link_libraries(bench_sqlite_orm PRIVATE
    sqlite_orm_wrapper
    pthread
)
link_sqlite(bench_sqlite_orm)
set_target_properties(bench_sqlite_orm PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_COMPILER /usr/bin/clang++
    LINK_OPTIONS "-stdlib=libstdc++"
)
target_compile_options(bench_sqlite_orm PRIVATE
    -fno-modules
    -stdlib=libstdc++
)
target_include_directories(bench_sqlite_orm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # For sqlite_orm_wrapper.h
)

# Storm ORM benchmark (C++26, custom clang, libc++)
add_executable(bench_storm "${CMAKE_CURRENT_SOURCE_DIR}/bench_storm.cpp")
target_link_libraries(bench_storm PRIVATE
    storm
    pthread
)
link_sqlite(bench_storm)
apply_clang_flags(bench_storm)
target_include_directories(bench_storm PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

message(STATUS "Benchmark executables: bench_sqlite, bench_sqlite_orm, bench_storm")