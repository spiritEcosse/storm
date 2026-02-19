project(storm_tests LANGUAGES CXX)

# Auto-discover all test source files (excluding mock_sqlite directory)
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SOURCES})
include(GoogleTest)

target_link_libraries(${PROJECT_NAME} PRIVATE storm gtest gtest_main gmock
                                              gmock_main pthread)

link_sqlite(${PROJECT_NAME})
link_postgresql(${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE ${GTEST_INCLUDE_DIRS} ${GMOCK_INCLUDE_DIRS}
          "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_SOURCE_DIR}")

# Use gtest_discover_tests instead of gtest_add_tests
gtest_discover_tests(${PROJECT_NAME})

# Add Clang-specific flags (modules and reflection) to tests (same as Storm
# library)
apply_clang_flags(${PROJECT_NAME})
# Note: Custom libc++ linking now handled by global add_link_options() in main
# CMakeLists.txt

add_subdirectory(mock_sqlite)
add_subdirectory(mock_libpq)
