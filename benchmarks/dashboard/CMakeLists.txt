# ============================================================================
# storm_bench_dashboard — real-time benchmark result dashboard (Issue #247)
# ============================================================================
# Phase 1: skeleton binary that opens the SQLite store, verifies the
# bench_dashboard schema is current, reports row counts, and exits.
#
# This TU `import storm;` and does NOT include <benchmark/benchmark.h> — same
# decoupling discipline as benchmarks/register.cpp. The Unix socket added in
# Phase 2 keeps the gbench world (storm_bench) and the storm world (this binary)
# in separate processes so their PCM-cache hashes never collide (memory
# project_pcm_hash_divergence, feedback_bench_main_register_split).

add_executable(storm_bench_dashboard "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

# C++26 modules owned by the dashboard binary. socket_server.cppm exposes the
# AF_UNIX listener; future phases add db.cppm + tui.cppm to the same FILE_SET.
file(GLOB STORM_DASHBOARD_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")
target_sources(
  storm_bench_dashboard PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
                                ${STORM_DASHBOARD_MODULE_UNITS})
# import std; migration (issue #326): main.cpp and the dashboard .cppm units do
# `import std;`, so this target must consume the std named module too.
set_target_properties(storm_bench_dashboard PROPERTIES CXX_MODULE_STD ON)

target_link_libraries(storm_bench_dashboard PRIVATE storm pthread)
link_sqlite(storm_bench_dashboard)
apply_clang_flags(storm_bench_dashboard)

target_include_directories(
  storm_bench_dashboard PRIVATE "${CMAKE_SOURCE_DIR}/src"
                                "${CMAKE_CURRENT_SOURCE_DIR}")

# ----------------------------------------------------------------------------
# bench_dashboard schema → Atlas migrations
# ----------------------------------------------------------------------------
# Reuses the same Storm reflection-driven Atlas pipeline as the application
# schema. storm_enable_migrations() registers `makemigrations` / `migrate` at
# global cmake scope, which would collide with the same call in
# tests/tools/storm_schema/CMakeLists.txt — see tests/tools/CMakeLists.txt for
# the ENABLE_BENCH gate that prevents both callers from running together.
include(${CMAKE_SOURCE_DIR}/cmake/storm_migrations.cmake)
storm_enable_migrations(
  NAMESPACE
  "bench_dashboard"
  MODELS_HEADER
  "${CMAKE_CURRENT_SOURCE_DIR}/models.hpp"
  MIGRATION_DIR
  "benchmarks/dashboard/migrations"
  TARGET_NAME
  "storm_bench_dashboard_schema")

message(STATUS "Benchmark executable: storm_bench_dashboard")
