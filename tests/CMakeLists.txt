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

# Issue #561: the unified corpus is additionally split by category, one JSON per
# test TU, so its ~190 ms/case of template instantiation compiles in parallel
# (53 s -> 24 s wall). The split files are generated from the same YAML source
# as the combined one, never hand-maintained. The combined unified_cases.json
# above stays generated: it is the #ifndef default in test_parser.hpp.
set(SPLIT_CASES_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/split_unified_cases.py")
set(UNIFIED_CASES_YAML "${TEST_CASES_DIR}/unified_cases.yaml")

set(SPLIT_JSON_FILES "")
foreach(CATEGORY IN ITEMS select grouped aggregate crud)
  list(APPEND SPLIT_JSON_FILES
       "${TEST_CASES_DIR}/unified_cases_${CATEGORY}.json")
endforeach()

add_custom_command(
  OUTPUT ${SPLIT_JSON_FILES}
  COMMAND ${Python3_EXECUTABLE} "${SPLIT_CASES_SCRIPT}" "${UNIFIED_CASES_YAML}"
          "${TEST_CASES_DIR}"
  DEPENDS "${UNIFIED_CASES_YAML}" "${SPLIT_CASES_SCRIPT}"
  COMMENT "Splitting unified_cases.yaml into per-category JSON"
  VERBATIM)

add_custom_target(generate_test_json ALL DEPENDS ${GENERATED_JSON_FILES}
                                                 ${SPLIT_JSON_FILES})

# These are generated into the SOURCE tree (#embed resolves relative to the
# including file, so they cannot live under the build dir). Register them for
# `clean` so a renamed category does not leave a stale JSON behind that keeps
# satisfying an #embed — which would pass on a warm tree and fail on a cold one.
set_property(
  TARGET generate_test_json
  APPEND
  PROPERTY ADDITIONAL_CLEAN_FILES ${GENERATED_JSON_FILES} ${SPLIT_JSON_FILES})

# Auto-discover all test source files (excluding mock_sqlite/mock_libpq
# directories)
file(
  GLOB
  TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/crud/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/query/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/db/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/errors/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/yaml/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_dashboard/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SOURCES})

target_sources(
  ${PROJECT_NAME}
  PRIVATE FILE_SET
          cxx_modules
          TYPE
          CXX_MODULES
          BASE_DIRS
          ${CMAKE_SOURCE_DIR}/benchmarks
          FILES
          ${CMAKE_SOURCE_DIR}/benchmarks/schema.cppm
          ${CMAKE_SOURCE_DIR}/benchmarks/parser.cppm
          ${CMAKE_SOURCE_DIR}/benchmarks/dashboard/wire.cppm
          ${CMAKE_SOURCE_DIR}/benchmarks/dashboard/tui.cppm)

include(GoogleTest)

target_link_libraries(${PROJECT_NAME} PRIVATE storm gtest gtest_main gmock
                                              gmock_main pthread)

link_sqlite(${PROJECT_NAME})
link_postgresql(${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE ${GTEST_INCLUDE_DIRS} ${GMOCK_INCLUDE_DIRS}
          "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/benchmarks/dashboard")

# Use gtest_discover_tests instead of gtest_add_tests DISCOVERY_MODE PRE_TEST:
# defer test listing to ctest time (not build time) so MSAN_OPTIONS from
# CMakePresets.json applies when the binary runs. Without this, cmake --build
# runs the binary with no MSAN env → abort.
gtest_discover_tests(${PROJECT_NAME} DISCOVERY_MODE PRE_TEST)

# Add Clang-specific flags (modules and reflection) to tests (same as Storm
# library)
apply_clang_flags(${PROJECT_NAME})
# Increase constexpr steps limit — consteval JSON parser for the YAML-driven
# test corpus. Sized for the full 247 cases: #561 split those across four TUs,
# but the three tests/crud TUs including test_parser.hpp still embed the whole
# corpus.
target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-steps=4194304)
# Treat warnings as errors on Storm-owned test code (issue #317). Third-party
# gtest/gmock headers are silenced via SYSTEM includes in cmake/tests.cmake.
target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
# Note: Custom libc++ linking now handled by global add_link_options() in main
# CMakeLists.txt

add_dependencies(${PROJECT_NAME} generate_test_json)

add_subdirectory(mock_sqlite)
add_subdirectory(mock_libpq)
