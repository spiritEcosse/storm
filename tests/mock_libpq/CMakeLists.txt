# ============================================================================
# Mock libpq Library - SHARED library for LD_PRELOAD-based testing This allows
# testing error paths in the PostgreSQL backend by intercepting libpq calls at
# runtime via LD_PRELOAD.
# ============================================================================

# Create mock libpq as SHARED library for LD_PRELOAD usage
add_library(mock_libpq SHARED "${CMAKE_CURRENT_SOURCE_DIR}/mock_libpq.cpp")

target_include_directories(mock_libpq PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
  mock_libpq PROPERTIES POSITION_INDEPENDENT_CODE ON LIBRARY_OUTPUT_DIRECTORY
                                                     "${CMAKE_BINARY_DIR}/lib")

# ============================================================================
# Mock libpq Infrastructure Tests - Tests the mock library itself These tests
# verify that the mock libpq library works correctly.
# ============================================================================

add_executable(storm_pq_mock_tests
               "${CMAKE_CURRENT_SOURCE_DIR}/test_pq_mock.cpp")

target_link_libraries(storm_pq_mock_tests PRIVATE mock_libpq gtest gtest_main
                                                  pthread)

target_include_directories(
  storm_pq_mock_tests PRIVATE ${GTEST_INCLUDE_DIRS}
                              "${CMAKE_CURRENT_SOURCE_DIR}")

# Discover mock infrastructure tests (no LD_PRELOAD needed)
gtest_discover_tests(storm_pq_mock_tests DISCOVERY_MODE PRE_TEST)

# ============================================================================
# Storm ORM PG Mock Tests - Tests PostgreSQL error paths using LD_PRELOAD These
# tests link against the MAIN storm library and use LD_PRELOAD at runtime to
# intercept libpq calls.
#
# Usage: LD_PRELOAD=./lib/libmock_libpq.so ./tests/storm_orm_pg_mock_tests
# ============================================================================

add_executable(storm_orm_pg_mock_tests
               "${CMAKE_CURRENT_SOURCE_DIR}/test_orm_pg_mock_errors.cpp")

# Link against MAIN storm library - coverage will count! Also link mock_libpq
# for the MockPqConfig API
target_link_libraries(storm_orm_pg_mock_tests PRIVATE storm mock_libpq gtest
                                                      gtest_main pthread)

# Link real SQLite and PostgreSQL (mock_libpq will override via LD_PRELOAD at
# runtime)
link_sqlite(storm_orm_pg_mock_tests)
link_postgresql(storm_orm_pg_mock_tests)

target_include_directories(
  storm_orm_pg_mock_tests
  PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/src")

# Apply Clang flags for modules and reflection
apply_clang_flags(storm_orm_pg_mock_tests)

# Don't auto-discover these tests - they need LD_PRELOAD to work correctly
# gtest_discover_tests(storm_orm_pg_mock_tests)

# Create a convenience target for running PG mock ORM tests with LD_PRELOAD
add_custom_target(
  run_pg_mock_orm_tests
  COMMAND
    ${CMAKE_COMMAND} -E env
    "LD_PRELOAD=${CMAKE_BINARY_DIR}/lib/libmock_libpq.so"
    $<TARGET_FILE:storm_orm_pg_mock_tests>
  DEPENDS storm_orm_pg_mock_tests mock_libpq
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running PG ORM mock tests with LD_PRELOAD")
