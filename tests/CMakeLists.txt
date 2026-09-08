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

# Single source of truth for the category split; mirrored by CATEGORIES in
# scripts/split_unified_cases.py and by the tests/yaml/test_unified_yaml_*.cpp
# TUs.
set(SPLIT_CATEGORIES select grouped aggregate crud)

set(SPLIT_JSON_FILES "")
foreach(CATEGORY IN LISTS SPLIT_CATEGORIES)
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

# The YAML corpus reaches these TUs through `#embed` (tests/test_parser.hpp),
# and `#embed` is invisible to the dependency scanner: clang does not list the
# embedded file in the depfile, so ninja records no edge from the object to the
# JSON (`ninja -t deps` showed none). Editing unified_cases.yaml therefore
# regenerated the JSON but left the objects stale — a green build still running
# the previous corpus. OBJECT_DEPENDS states the edge explicitly.
#
# set_property(SOURCE ... APPEND) rather than set_source_files_properties(): the
# latter silently does nothing when the path matches no target source, so a
# renamed category would quietly restore the stale-object bug instead of
# failing.
foreach(CATEGORY IN LISTS SPLIT_CATEGORIES)
  set_property(
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/yaml/test_unified_yaml_${CATEGORY}.cpp"
    APPEND
    PROPERTY OBJECT_DEPENDS "${TEST_CASES_DIR}/unified_cases_${CATEGORY}.json")
endforeach()

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

# ── Precompiled header ────────────────────────────────────────────────────────
# Measured floor per test TU (-ftime-trace on a TU whose only body is
# EXPECT_TRUE(true)): 2.2 s of <gtest/gtest.h>, 2.1 s of `import storm;`, 1.4 s
# of test_db_helpers.h + shared/models.h (the pre-#634 umbrella), 0.2 s of
# `import std;` — and Backend 0.03 s, i.e. the floor is entirely frontend.
# Precompiling gtest removes its share: measured -1.9 to -2.4 s per TU across
# four files (min of 3 runs each, ccache disabled), ~-236 s over the ~110 test
# TUs.
#
# UNITY_BUILD would amortise the whole floor rather than one slice, but CMake
# silently disables it whenever CXX_SCAN_FOR_MODULES is ON (verified: SCAN=OFF
# -> Unity edges appear, SCAN=ON -> none), and scanning cannot be turned off
# here because these TUs `import storm;` (verified: a TU with `import std;` and
# CXX_SCAN_FOR_MODULES OFF fails with "module 'std' not found"). PCH is the one
# mechanism of the two that works alongside module scanning.
#
# NOTE: scripts/run_clang_tidy.sh strips the resulting `-Xclang -include-pch`
# flags from the compile database before replaying commands. clang-tidy SIGSEGVs
# on them once any check is enabled, silently and with no diagnostic. That is
# unreachable today (tests/.clang-tidy sets `Checks: '-*'`, #590, and no src/
# entry gets the PCH), so the strip is defence in depth: it keeps re-enabling
# checks for tests/** from silently un-linting the tree. See COMPILER_ISSUES
# #13.
target_precompile_headers(${PROJECT_NAME} PRIVATE <gtest/gtest.h>)

# The module interface units in this target's FILE_SET must not receive the PCH.
# CMake would otherwise force gtest's entire textual libc++ surface in ahead of
# their `module;` declaration and bake that state into the BMIs every consuming
# TU imports. They include no gtest, so there is nothing to gain either.
set_property(
  SOURCE ${CMAKE_SOURCE_DIR}/benchmarks/schema.cppm
         ${CMAKE_SOURCE_DIR}/benchmarks/parser.cppm
         ${CMAKE_SOURCE_DIR}/benchmarks/dashboard/wire.cppm
         ${CMAKE_SOURCE_DIR}/benchmarks/dashboard/tui.cppm
  PROPERTY SKIP_PRECOMPILE_HEADERS ON)

# Two TUs opt out. Reaching libc++ through the PCH, completing an entity that
# lazily pulls in <__fwd/string.h> (std::filesystem::path, storm::UUID, ...)
# makes std::__1::string visible next to the `std` module's std::string, and
# EVERY LATER mention of a string type in that TU becomes ambiguous:
# test_bindable_concept.cpp:40: error: reference to 'string' is ambiguous It is
# order-dependent — naming the string type first compiles clean — so this is not
# confined to these two files or to compile-time-only TUs: any TU that names
# such a trigger before a string type can hit it. A textual <string> does NOT
# help; textually including the trigger's own header does. Excluding is
# preferred over sprinkling includes, which would cut against the `import std;`
# convention (#326). The failure is a hard compile error, never silent.
set_property(
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/schema/test_bindable_concept.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/schema/test_dialect_concepts.cpp"
  PROPERTY SKIP_PRECOMPILE_HEADERS ON)

# The bench_dashboard TUs import the module units excluded just above. With the
# consumer carrying the PCH and the module interface not, libc++'s textual
# std::basic_string and the module's copy fail to reconcile: string:1172: error:
# 'std::basic_string<char>::basic_string' from module '' is not present in
# definition of 'std::basic_string<char>' provided earlier Release-only, because
# the dashboard modules are not built in every preset. Opt the consumers out
# too, so both sides of that import agree.
file(GLOB STORM_PCH_SKIP_DASHBOARD
     "${CMAKE_CURRENT_SOURCE_DIR}/bench_dashboard/*.cpp")
set_property(SOURCE ${STORM_PCH_SKIP_DASHBOARD} PROPERTY SKIP_PRECOMPILE_HEADERS
                                                         ON)

# Third symptom of the same PCH/module split: ordinary code, no static_assert
# and no module import, just std::string used normally --
# test_sqlite_tuning.cpp:29: error: cannot initialize object parameter of type
# 'const std::__1::basic_string<char>' with an expression of type 'std::string'
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/db/test_sqlite_tuning.cpp"
             PROPERTY SKIP_PRECOMPILE_HEADERS ON)

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
#
# DISCOVERY_TIMEOUT raised from CMake's 5s default: with ~3000 registered
# TYPED_TESTs, --gtest_list_tests takes low single-digit seconds even under
# normal load, but on a busy shared/loaded machine that's enough to blow past 5s
# and fail the whole ctest run with "Process terminated due to timeout" before
# any test actually runs.
gtest_discover_tests(${PROJECT_NAME} DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT
                     120)

# Add Clang-specific flags (modules and reflection) to tests (same as Storm
# library)
apply_clang_flags(${PROJECT_NAME})
# Increase constexpr steps limit — consteval JSON parser for the YAML-driven
# test corpus. #561 split the 247 cases across four tests/yaml TUs, each of
# which #embeds and parses only its own category slice — no TU embeds the whole
# corpus any more (the three tests/crud TUs that used to include test_parser.hpp
# without using a symbol from it no longer do). The limit is still sized for the
# full corpus: the per-slice budgets have not been measured, so lowering it is a
# separate change.
target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-steps=4194304)
# Treat warnings as errors on Storm-owned test code (issue #317). Third-party
# gtest/gmock headers are silenced via SYSTEM includes in cmake/tests.cmake.
target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
# Note: Custom libc++ linking now handled by global add_link_options() in main
# CMakeLists.txt

add_dependencies(${PROJECT_NAME} generate_test_json)

add_subdirectory(mock_sqlite)
add_subdirectory(mock_libpq)
