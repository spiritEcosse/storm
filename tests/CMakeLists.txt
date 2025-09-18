project(storm_tests LANGUAGES CXX)

# Only compile test sources; link against the storm library for implementation
set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
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

if (SQLite3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE SQLite::SQLite3)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3)
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# Use gtest_discover_tests instead of gtest_add_tests
gtest_discover_tests(${PROJECT_NAME})

# Ensure Clang can import standard library headers as header units in tests
target_compile_options(${PROJECT_NAME} PRIVATE -fmodules -fbuiltin-module-map)
