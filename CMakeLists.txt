cmake_minimum_required(VERSION 3.30)

# CMake's experimental `import std;` support gate. Token re-rolls as the feature
# evolves; current value valid for CMake 3.30–4.x. Must be set before project().
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

project(storm)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "21")
    message(
      FATAL_ERROR "Clang >= 21 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16")
    message(
      FATAL_ERROR "GCC >= 16 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
else()
  message(
    FATAL_ERROR
      "This project requires Clang >= 21 or GCC >= 16. Got: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  )
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

add_compile_options(
  $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=1>
  $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=1>
  $<$<AND:$<CXX_COMPILER_ID:Clang>,$<CONFIG:Debug>>:-ftemplate-backtrace-limit=0>
)

# Pin Clang's module cache to the build dir. Clang hashes compile flags into the
# module cache path, so two presets with different flags (coverage vs no
# coverage, sanitizer vs none, etc.) write _Builtin_stddef etc. under different
# hash subdirs of the shared global cache (~/.cache/clang/ModuleCache/). When a
# later TU imports a module, Clang sees the same builtin module in both dirs and
# emits a fatal "module defined in both" error. Keeping the cache local to
# ${CMAKE_BINARY_DIR} guarantees preset isolation, since every preset already
# has its own binaryDir. GCC writes BMIs to gcm.cache/ in the build dir
# automatically — no equivalent flag is needed.
add_compile_options(
  $<$<CXX_COMPILER_ID:Clang>:-fmodules-cache-path=${CMAKE_BINARY_DIR}/module-cache>
)

include(cmake/cxx_flags.cmake)

# ── Core library ──────────────────────────────────────────────────────────────

add_library(${PROJECT_NAME} STATIC)

file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

target_sources(${PROJECT_NAME} PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
                                      FILES ${STORM_MODULE_UNITS})

apply_cxx_flags(${PROJECT_NAME})

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
include(cmake/uuid.cmake)
include(cmake/cmake-scripts.cmake)

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
include(cmake/sanitizers.cmake)
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
