project(storm_tests LANGUAGES CXX)
file(GLOB_RECURSE RESOURCES CONFIGURE_DEPENDS ../src/*.cpp ../src/*.h *.cpp *.h)

# Remove ../src/main.cpp from RESOURCES to avoid multiple definition of main()
list(REMOVE_ITEM RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp)

add_executable(${PROJECT_NAME} ${RESOURCES})
include(${CMAKE_CURRENT_SOURCE_DIR}/../envs.cmake)
include(GoogleTest)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    gtest
    gtest_main
    gmock
    gmock_main
    pthread
    fmt::fmt
    refl-cpp
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
)

# Use gtest_discover_tests instead of gtest_add_tests
gtest_discover_tests(${PROJECT_NAME})