project(storm_tests LANGUAGES CXX)

# ============================================================================
# YAML → JSON conversion for YAML-driven test cases Each YAML file is converted
# to JSON at configure time so the JSON can be embedded via C++26 #embed in
# test_parser.hpp.
# ============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(YAML_TO_JSON_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/yaml_to_json_tests.py")
set(TEST_CASES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test_cases")

set(YAML_TEST_FILES unified_cases)

set(GENERATED_JSON_FILES "")
foreach(STEM IN LISTS YAML_TEST_FILES)
  set(YAML_SRC "${TEST_CASES_DIR}/${STEM}.yaml")
  set(JSON_OUT "${TEST_CASES_DIR}/${STEM}.json")
  list(APPEND GENERATED_JSON_FILES "${JSON_OUT}")
  add_custom_command(
    OUTPUT "${JSON_OUT}"
    COMMAND ${Python3_EXECUTABLE} "${YAML_TO_JSON_SCRIPT}" "${YAML_SRC}"
            "${JSON_OUT}"
    DEPENDS "${YAML_SRC}" "${YAML_TO_JSON_SCRIPT}"
    COMMENT "Generating ${STEM}.json from YAML"
    VERBATIM)
endforeach()

add_custom_target(generate_test_json ALL DEPENDS ${GENERATED_JSON_FILES})

# Auto-discover all test source files (excluding mock_sqlite/mock_libpq
# directories)
file(
  GLOB
  TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/crud/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/query/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/errors/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/yaml/*.cpp")

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
# Increase constexpr steps limit — consteval JSON parser for 210+ YAML-driven
# test cases
target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-steps=4194304)
# Note: Custom libc++ linking now handled by global add_link_options() in main
# CMakeLists.txt

add_dependencies(${PROJECT_NAME} generate_test_json)

add_subdirectory(mock_sqlite)
add_subdirectory(mock_libpq)
