cmake_minimum_required(VERSION 3.30)

# Gate for CMake's experimental `import std;` support. Must be set BEFORE
# project(). The UUID is CMake-version-specific — see issue #326. Verified for
# CMake 4.3.3. Bump UUID if the minimum-supported CMake changes.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

# Pin the import-std descriptor to clang-p2996's libc++.modules.json before
# project(). Without this, CMake's auto-detection picks up the host GCC's
# /usr/lib64/libstdc++.modules.json and tries to build bits/std.cc against our
# libc++ include paths — which fails. See issue #326.
#
# LIBCXX_ROOT is passed via preset/-D, so we resolve the JSON path the same way
# cmake/libcxx.cmake does, but without including it (project() must run first
# for that). Falls back to letting CMake auto-detect when LIBCXX_ROOT is unset
# (e.g. plain library configures, where import-std is not used).
if(DEFINED LIBCXX_ROOT)
  set(CMAKE_CXX_STDLIB_MODULES_JSON
      "${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu/libc++.modules.json")
endif()

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

# Pin Clang's module cache to the build dir. Clang hashes compile flags into the
# module cache path, so two presets with different flags (coverage vs no
# coverage, sanitizer vs none, etc.) write _Builtin_stddef etc. under different
# hash subdirs of the shared global cache (~/.cache/clang/ModuleCache/). When a
# later TU imports a module, Clang sees the same builtin module in both dirs and
# emits a fatal "module defined in both" error. Keeping the cache local to
# ${CMAKE_BINARY_DIR} guarantees preset isolation, since every preset already
# has its own binaryDir.
add_compile_options("-fmodules-cache-path=${CMAKE_BINARY_DIR}/module-cache")

# import std; migration (issue #326): with -fmodules dropped (see
# apply_clang_flags() in cmake/libcxx.cmake), the -fmodules-cache-path flag
# above is no longer consumed; -Werror would otherwise promote the resulting
# -Wunused-command-line-argument to a hard error.
add_compile_options(-Wno-unused-command-line-argument)

include(cmake/libcxx.cmake)

# ── Sanitizer flags (must precede the storm target) ───────────────────────────
# import std; migration (issue #326): sanitizer flags are applied globally via
# add_compile_options()/add_link_options() in cmake/sanitizers.cmake. Those only
# affect targets created AFTER the include, so the include MUST come before
# add_library(storm) below — otherwise neither libstorm nor the
# CMake-synthesized `__cmake_cxx_std_26` std-module target (which compiles
# libc++'s std.cppm) gets the sanitizer instrumentation, and the std module's
# container-annotation calls (`__sanitizer_annotate_contiguous_container`)
# link-fail with an undefined reference. cmake-scripts (the sanitizer backend)
# needs CPM, so fetch that first.
include(cmake/cpm.cmake)
include(cmake/cmake-scripts.cmake)
include(cmake/sanitizers.cmake)

# ── Core library ──────────────────────────────────────────────────────────────

add_library(${PROJECT_NAME} STATIC)

# import std; migration (issue #326): consume the std named module on this
# target.
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)

file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

target_sources(${PROJECT_NAME} PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
                                      FILES ${STORM_MODULE_UNITS})

target_compile_options(${PROJECT_NAME} PRIVATE -Werror)

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

include(cmake/uuid.cmake)

# ── Feature options ───────────────────────────────────────────────────────────

option(ENABLE_TESTS "Enable testing support" OFF)
option(ENABLE_BENCH "Enable benchmarking with sqlite_orm" OFF)
option(ENABLE_COVERAGE "Enable code coverage (requires LLVM tools)" OFF)
option(ENABLE_TOOLS "Build CLI tools (storm-schema, etc.)" OFF)

# coverage flags must be set before test targets are compiled
include(cmake/coverage.cmake)
include(cmake/tests.cmake)
# coverage targets reference test targets — must come after tests.cmake
include(cmake/coverage-targets.cmake)
include(cmake/bench.cmake)
include(cmake/fuzz.cmake)
include(cmake/format.cmake)
include(cmake/tools.cmake)

# ── Build summary ─────────────────────────────────────────────────────────────

message(STATUS "=== Storm Build Configuration ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(
  STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(
  STATUS
    "SQLite: ${SQLite3_VERSION} (RIGHT_JOIN=${STORM_SQLITE_RIGHT_JOIN}, STRICT=${STORM_SQLITE_STRICT_TABLES})"
)
message(STATUS "Testing: ${ENABLE_TESTS}")
message(STATUS "Benchmarks: ${ENABLE_BENCH}")
message(STATUS "Coverage: ${ENABLE_COVERAGE}")
message(STATUS "Sanitizer: ${USE_SANITIZER}")
message(STATUS "Fuzzing: ${ENABLE_FUZZING}")
message(STATUS "Tools: ${ENABLE_TOOLS}")
message(STATUS "===================================")
