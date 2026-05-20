# Coverage targets — must be included after test targets are defined (after
# tests.cmake).
#
# GCC writes .gcda profile data alongside .gcno notes during test execution.
# lcov walks the build tree, calls gcov per .gcda, and produces a single info
# file we filter and render.
#
# Target names match the pre-migration LLVM contract so commit.sh and CI do
# not need updates: coverage / coverage-html / coverage-clean.

if(ENABLE_COVERAGE AND ENABLE_TESTS)

  add_custom_target(
    coverage-run-main
    COMMAND
      ${CMAKE_COMMAND} -E env
      "STORM_PG_CONNSTR=host=/var/run/postgresql dbname=storm_db user=storm_db"
      $<TARGET_FILE:storm_tests>
    DEPENDS storm_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running main tests with coverage instrumentation"
    VERBATIM)

  add_custom_target(
    coverage-run-mock
    COMMAND
      ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:mock_sqlite3>
      $<TARGET_FILE:storm_orm_mock_tests>
    DEPENDS storm_orm_mock_tests mock_sqlite3
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running mock tests with coverage instrumentation (LD_PRELOAD)"
    VERBATIM)

  add_custom_target(
    coverage-run-pg-mock
    COMMAND
      ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:mock_libpq>
      $<TARGET_FILE:storm_orm_pg_mock_tests>
    DEPENDS storm_orm_pg_mock_tests mock_libpq
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running PG mock tests with coverage instrumentation (LD_PRELOAD)"
    VERBATIM)

  add_custom_target(
    coverage-run
    DEPENDS coverage-run-main coverage-run-mock coverage-run-pg-mock
    COMMENT "All coverage tests completed")

  set(LCOV_C_EXTENSIONS "c,h,i,C,H,I,icc,cpp,cc,cxx,hh,hpp,hxx,cppm")

  add_custom_target(
    coverage-capture
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
    COMMAND
      ${LCOV} --gcov-tool ${GCOV} --capture --directory ${CMAKE_BINARY_DIR}
      --rc branch_coverage=1 --rc c_file_extensions=${LCOV_C_EXTENSIONS}
      --ignore-errors mismatch,inconsistent,unsupported --output-file
      ${COVERAGE_OUTPUT_DIR}/coverage.lcov
    DEPENDS coverage-run
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Capturing gcov data into lcov info file"
    VERBATIM)

  add_custom_target(
    coverage
    COMMAND
      ${LCOV} --rc branch_coverage=1 --rc
      c_file_extensions=${LCOV_C_EXTENSIONS} --ignore-errors
      unused,deprecated,unsupported,inconsistent,range --remove
      ${COVERAGE_OUTPUT_DIR}/coverage.lcov "*/third_party/*" "*/googletest/*"
      "*/build/*" "*/tests/*" "*/src/orm/utilities.cppm" --output-file
      ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
    COMMAND
      ${LCOV} --rc branch_coverage=1 --ignore-errors deprecated,inconsistent
      --summary ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
    DEPENDS coverage-capture
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Filtering coverage with LCOV_EXCL markers"
    VERBATIM)

  add_custom_target(
    coverage-html
    COMMAND
      ${GENHTML} --rc branch_coverage=1 --rc
      c_file_extensions=${LCOV_C_EXTENSIONS} --ignore-errors
      deprecated,range,inconsistent,source --legend --title
      "Storm ORM Coverage (filtered)" --output-directory
      ${COVERAGE_OUTPUT_DIR}/html-filtered
      ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
    DEPENDS coverage
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT
      "Generating filtered HTML coverage report in ${COVERAGE_OUTPUT_DIR}/html-filtered"
    VERBATIM)

  add_custom_target(
    coverage-clean
    COMMAND
      bash -c
      "find ${CMAKE_BINARY_DIR} -name '*.gcda' -delete && rm -rf ${COVERAGE_OUTPUT_DIR}"
    COMMENT "Cleaning coverage data"
    VERBATIM)

  message(STATUS "Coverage targets available:")
  message(STATUS "  coverage       - Run tests + show filtered text summary")
  message(STATUS "  coverage-html  - Run tests + generate filtered HTML report")
  message(STATUS "  coverage-clean - Clean all coverage data")
endif()
