project(storm_tests LANGUAGES CXX)
file(GLOB_RECURSE RESOURCES CONFIGURE_DEPENDS ../src/*.*  *.cpp *.h)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(${PROJECT_NAME} ${RESOURCES})
include(${CMAKE_CURRENT_SOURCE_DIR}/../envs.cmake)
include(GoogleTest)
gtest_add_tests(
        TARGET ${PROJECT_NAME}
        TEST_LIST myTests_targets
)

target_link_libraries(${PROJECT_NAME} PRIVATE gtest
        gtest_main
        gmock
        gmock_main
        pthread
        fmt::fmt
)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${GTEST_INCLUDE_DIRS}
        ${GMOCK_INCLUDE_DIRS}
)
#
#execute_process(
#        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/helper_tests.sh
#        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#        RESULT_VARIABLE TEST_RESULT
#        OUTPUT_VARIABLE TEST_OUTPUT
#        ERROR_VARIABLE TEST_ERROR
#)
#
#if (TEST_RESULT EQUAL 0)
#    message(STATUS "Helper tests completed successfully")
#    message(STATUS "Test output: ${TEST_OUTPUT}")
#else ()
#    message(FATAL_ERROR "Helper tests failed with error code ${TEST_RESULT}\n"
#            "Output: ${TEST_OUTPUT}\n"
#            "Error: ${TEST_ERROR}")
#endif ()
