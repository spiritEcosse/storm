# ============================================================================
# Mock SQLite Library - SHARED library for LD_PRELOAD-based testing This allows
# testing error paths in the main storm library by intercepting SQLite calls at
# runtime via LD_PRELOAD.
# ============================================================================

# Create mock sqlite as SHARED library for LD_PRELOAD usage
add_library(mock_sqlite3 SHARED "${CMAKE_CURRENT_SOURCE_DIR}/mock_sqlite3.cpp")

target_include_directories(mock_sqlite3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
  mock_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ============================================================================
# Mock SQLite Infrastructure Tests - Tests the mock library itself These tests
# verify that the mock SQLite library works correctly.
# ============================================================================

add_executable(storm_mock_tests
               "${CMAKE_CURRENT_SOURCE_DIR}/test_sqlite_mock.cpp")

target_link_libraries(storm_mock_tests PRIVATE mock_sqlite3 gtest gtest_main
                                               pthread)

target_include_directories(
  storm_mock_tests PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")

# Discover mock infrastructure tests
gtest_discover_tests(storm_mock_tests DISCOVERY_MODE PRE_TEST)

# ============================================================================
# Storm ORM Mock Tests - Tests Storm ORM error paths using LD_PRELOAD These
# tests link against the MAIN storm library (not a mock-linked copy) and use
# LD_PRELOAD at runtime to intercept SQLite calls. This allows coverage data to
# count toward the main library!
#
# Usage: LD_PRELOAD=./lib/libmock_sqlite3.so ./tests/storm_orm_mock_tests
# ============================================================================

add_executable(storm_orm_mock_tests
               "${CMAKE_CURRENT_SOURCE_DIR}/test_orm_mock_errors.cpp")

# Link against MAIN storm library (not storm_mock) - coverage will count! Also
# link mock_sqlite3 for the MockSqlite3Config API
target_link_libraries(storm_orm_mock_tests PRIVATE storm mock_sqlite3 gtest
                                                   gtest_main pthread)

# Also link real SQLite (mock_sqlite3 will override via LD_PRELOAD at runtime)
link_sqlite(storm_orm_mock_tests)
link_postgresql(storm_orm_mock_tests)

target_include_directories(
  storm_orm_mock_tests
  PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/src")

# Apply Clang flags for modules and reflection
apply_clang_flags(storm_orm_mock_tests)

# Don't auto-discover these tests - they need LD_PRELOAD to work correctly They
# will be run manually or via a custom target
# gtest_discover_tests(storm_orm_mock_tests)

# Create a convenience target for running mock ORM tests with LD_PRELOAD
add_custom_target(
  run_mock_orm_tests
  COMMAND
    ${CMAKE_COMMAND} -E env
    "LD_PRELOAD=${CMAKE_BINARY_DIR}/lib/libmock_sqlite3.so"
    $<TARGET_FILE:storm_orm_mock_tests>
  DEPENDS storm_orm_mock_tests mock_sqlite3
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running ORM mock tests with LD_PRELOAD")
