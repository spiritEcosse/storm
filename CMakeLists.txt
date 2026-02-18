cmake_minimum_required(VERSION 3.30)
project(storm)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(
    FATAL_ERROR "This project requires Clang. Got: ${CMAKE_CXX_COMPILER_ID}")
endif()
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "21")
  message(
    FATAL_ERROR "Clang >= 21 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
endif()

message(STATUS "Using C++26 (standard build)")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Configure git hooks path (so pre-commit hook works on fresh clones)
execute_process(
  COMMAND git config core.hooksPath .githooks
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  ERROR_QUIET)

add_compile_options(-ferror-limit=1
                    $<$<CONFIG:Debug>:-ftemplate-backtrace-limit=0>)

include(cmake/libcxx.cmake)

# ── Core library ──────────────────────────────────────────────────────────────

add_library(${PROJECT_NAME} STATIC)

file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

target_sources(${PROJECT_NAME} PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
                                      FILES ${STORM_MODULE_UNITS})

apply_clang_flags(${PROJECT_NAME})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${STORM_MODULE_UNITS})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
         "${CMAKE_CURRENT_SOURCE_DIR}/include"
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

include(cmake/db.cmake)

link_sqlite(${PROJECT_NAME})
link_postgresql(${PROJECT_NAME})

include(cmake/cpm.cmake)
include(cmake/cmake-scripts.cmake)

# ── Feature options ───────────────────────────────────────────────────────────

option(ENABLE_TESTS "Enable testing support" OFF)
option(ENABLE_BENCH "Enable benchmarking with sqlite_orm" OFF)
option(ENABLE_COVERAGE "Enable code coverage (requires LLVM tools)" OFF)

# coverage flags must be set before test targets are compiled
include(cmake/coverage.cmake)
include(cmake/tests.cmake)
# coverage targets reference test targets — must come after tests.cmake
include(cmake/coverage-targets.cmake)
include(cmake/bench.cmake)
include(cmake/sanitizers.cmake)
include(cmake/format.cmake)

# ── Build summary ─────────────────────────────────────────────────────────────

message(STATUS "=== Storm Build Configuration ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(
  STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Testing: ${ENABLE_TESTS}")
message(STATUS "Benchmarks: ${ENABLE_BENCH}")
message(STATUS "Coverage: ${ENABLE_COVERAGE}")
message(STATUS "Sanitizer: ${USE_SANITIZER}")
message(STATUS "===================================")
