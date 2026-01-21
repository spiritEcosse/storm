project(storm_tests LANGUAGES CXX)

# Auto-discover all test source files (excluding mock_sqlite directory)
file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(${PROJECT_NAME} ${TEST_SOURCES})
include(GoogleTest)

target_link_libraries(${PROJECT_NAME} PRIVATE
    storm
    gtest
    gtest_main
    gmock
    gmock_main
    pthread
)

link_sqlite(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# Use gtest_discover_tests instead of gtest_add_tests
gtest_discover_tests(${PROJECT_NAME})

# Add Clang-specific flags (modules and reflection) to tests (same as Storm library)
apply_clang_flags(${PROJECT_NAME})
# Note: Custom libc++ linking now handled by global add_link_options() in main CMakeLists.txt

# ============================================================================
# Mock SQLite Tests - Tests for the mock SQLite infrastructure
# These tests verify that the mock SQLite library works correctly and can be
# used to test hard-to-trigger error paths (SQLITE_NOMEM, SQLITE_CORRUPT, etc.)
# ============================================================================

# Create mock sqlite library
add_library(mock_sqlite3 STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_sqlite/mock_sqlite3.cpp"
)

target_include_directories(mock_sqlite3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_sqlite"
)

# Apply custom libc++ flags to mock library (required for linking compatibility)
target_compile_options(mock_sqlite3 PRIVATE -stdlib=libc++)

# Mock SQLite test executable
add_executable(storm_mock_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_sqlite/test_sqlite_mock.cpp"
)

target_link_libraries(storm_mock_tests PRIVATE
    mock_sqlite3
    gtest
    gtest_main
    pthread
)

target_include_directories(storm_mock_tests PRIVATE
    ${GTEST_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_sqlite"
)

# Apply custom libc++ flags to mock tests (required for linking compatibility)
target_compile_options(storm_mock_tests PRIVATE -stdlib=libc++)

# Discover mock tests
gtest_discover_tests(storm_mock_tests)
