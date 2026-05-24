# Coverage targets — must be included after test targets are defined (after
# tests.cmake).
#
# Combines data from three test binaries: - storm_tests:             main tests
# (real SQLite/PostgreSQL) - storm_orm_mock_tests:    SQLite mock tests via
# LD_PRELOAD (error paths) - storm_orm_pg_mock_tests: PG mock tests via
# LD_PRELOAD (PG error paths)
#
# Clang path uses llvm-profdata merge + llvm-cov export. GCC path uses gcov +
# lcov. Both expose the same public targets (coverage, coverage-html,
# coverage-clean) so commit.sh and CI need no compiler-specific knowledge.

if(NOT (ENABLE_COVERAGE AND ENABLE_TESTS))
  return()
endif()

set(LCOV_C_EXTENSIONS "c,h,i,C,H,I,icc,cpp,cc,cxx,hh,hpp,hxx,cppm")

# ── Per-compiler test-runner env wrapping ─────────────────────────────────────
# Clang's LLVM profile runtime requires LLVM_PROFILE_FILE=… per binary run so
# mock-test profraw files don't collide with main-test profraw files. GCC's gcov
# writes per-source-file .gcda alongside .gcno — no env wrapping needed.

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(_MOCK_ENV LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/mock.profraw)
  set(_PG_MOCK_ENV LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/pg_mock.profraw)
  set(_MAIN_RUNNER ${CMAKE_SOURCE_DIR}/scripts/coverage-run-batched.sh
                   ${CMAKE_BINARY_DIR})
  set(_MAIN_COMMENT
      "Running main tests with coverage instrumentation (batched)")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(_MOCK_ENV "")
  set(_PG_MOCK_ENV "")
  set(_MAIN_RUNNER $<TARGET_FILE:storm_tests>)
  set(_MAIN_COMMENT "Running main tests with coverage instrumentation")
endif()

# ── Shared test-runner targets (same shape under both compilers) ──────────────
add_custom_target(
  coverage-run-main
  COMMAND
    ${CMAKE_COMMAND} -E env
    "STORM_PG_CONNSTR=host=/var/run/postgresql dbname=storm_db user=storm_db"
    ${_MAIN_RUNNER}
  DEPENDS storm_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "${_MAIN_COMMENT}"
  VERBATIM)

add_custom_target(
  coverage-run-mock
  COMMAND
    ${CMAKE_COMMAND} -E env ${_MOCK_ENV} LD_PRELOAD=$<TARGET_FILE:mock_sqlite3>
    $<TARGET_FILE:storm_orm_mock_tests>
  DEPENDS storm_orm_mock_tests mock_sqlite3
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running mock tests with coverage instrumentation (LD_PRELOAD)"
  VERBATIM)

add_custom_target(
  coverage-run-pg-mock
  COMMAND
    ${CMAKE_COMMAND} -E env ${_PG_MOCK_ENV} LD_PRELOAD=$<TARGET_FILE:mock_libpq>
    $<TARGET_FILE:storm_orm_pg_mock_tests>
  DEPENDS storm_orm_pg_mock_tests mock_libpq
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running PG mock tests with coverage instrumentation (LD_PRELOAD)"
  VERBATIM)

add_custom_target(
  coverage-run
  DEPENDS coverage-run-main coverage-run-mock coverage-run-pg-mock
  COMMENT "All coverage tests completed")

# ── Per-compiler capture → filtered lcov info file ────────────────────────────
# Under Clang we do: llvm-profdata merge → llvm-cov export -format=lcov. Under
# GCC we do:   lcov --capture (walks .gcda via gcov). Both produce
# ${COVERAGE_OUTPUT_DIR}/coverage.lcov, which the shared `coverage` target below
# filters into coverage-filtered.lcov.

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  find_program(LCOV_TOOL lcov)
  find_program(GENHTML_TOOL genhtml)
  if(NOT LCOV_TOOL)
    message(
      FATAL_ERROR
        "lcov not found but required for coverage builds.\n"
        "  Manjaro/Arch: sudo pacman -S lcov\n"
        "  Ubuntu/Debian: sudo apt install lcov")
  endif()
  if(NOT GENHTML_TOOL)
    message(
      FATAL_ERROR
        "genhtml not found but required for coverage builds.\n"
        "  Manjaro/Arch: sudo pacman -S lcov\n"
        "  Ubuntu/Debian: sudo apt install lcov")
  endif()
  message(STATUS "lcov found: ${LCOV_TOOL}")
  message(STATUS "genhtml found: ${GENHTML_TOOL}")

  add_custom_target(
    coverage-merge
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
    COMMAND
      bash -c
      "${LLVM_PROFDATA} merge -sparse ${CMAKE_BINARY_DIR}/batch_*.profraw ${CMAKE_BINARY_DIR}/mock.profraw ${CMAKE_BINARY_DIR}/pg_mock.profraw -o ${COVERAGE_OUTPUT_DIR}/coverage.profdata"
    DEPENDS coverage-run
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT
      "Merging coverage data from batched main tests, mock tests, and PG mock tests"
    VERBATIM)

  add_custom_target(
    coverage-capture
    COMMAND
      ${LLVM_COV} export $<TARGET_FILE:storm_tests>
      -object=$<TARGET_FILE:storm_orm_mock_tests>
      -object=$<TARGET_FILE:storm_orm_pg_mock_tests>
      -instr-profile=${COVERAGE_OUTPUT_DIR}/coverage.profdata -format=lcov
      -ignore-filename-regex="third_party|googletest|build|mock_sqlite|mock_libpq"
      ${CMAKE_SOURCE_DIR}/src > ${COVERAGE_OUTPUT_DIR}/coverage.lcov
    DEPENDS coverage-merge
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT
      "Exporting coverage data in LCOV format to ${COVERAGE_OUTPUT_DIR}/coverage.lcov"
    VERBATIM)

  set(_LCOV_BIN ${LCOV_TOOL})
  set(_GENHTML_BIN ${GENHTML_TOOL})
  set(_COVERAGE_FILTER_ARGS --filter region,branch_region)
  set(_COVERAGE_TOOLCHAIN "LLVM")
  set(_CLEAN_CMD
      "rm -rf ${COVERAGE_OUTPUT_DIR} ${CMAKE_BINARY_DIR}/batch_*.profraw ${CMAKE_BINARY_DIR}/mock.profraw ${CMAKE_BINARY_DIR}/pg_mock.profraw"
  )

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_custom_target(
    coverage-capture
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
    COMMAND
      ${LCOV} --gcov-tool ${GCOV} --capture --directory ${CMAKE_BINARY_DIR} --rc
      branch_coverage=1 --rc c_file_extensions=${LCOV_C_EXTENSIONS}
      --ignore-errors mismatch,inconsistent,unsupported --output-file
      ${COVERAGE_OUTPUT_DIR}/coverage.lcov
    DEPENDS coverage-run
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Capturing gcov data into lcov info file"
    VERBATIM)

  set(_LCOV_BIN ${LCOV})
  set(_GENHTML_BIN ${GENHTML})
  set(_COVERAGE_FILTER_ARGS "")
  set(_COVERAGE_TOOLCHAIN "gcov")
  set(_CLEAN_CMD
      "find ${CMAKE_BINARY_DIR} -name '*.gcda' -delete && rm -rf ${COVERAGE_OUTPUT_DIR}"
  )
endif()

# ── Shared filter / report / clean targets (use the per-compiler variables) ───
add_custom_target(
  coverage
  COMMAND
    ${_LCOV_BIN} --rc branch_coverage=1 --rc
    c_file_extensions=${LCOV_C_EXTENSIONS} --ignore-errors
    unused,deprecated,unsupported,inconsistent,range ${_COVERAGE_FILTER_ARGS}
    --remove ${COVERAGE_OUTPUT_DIR}/coverage.lcov "*/third_party/*"
    "*/googletest/*" "*/build/*" "*/tests/*" "*/src/orm/utilities.cppm"
    --output-file ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
  COMMAND
    ${_LCOV_BIN} --rc branch_coverage=1 --ignore-errors deprecated,inconsistent
    --summary ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
  DEPENDS coverage-capture
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Filtering coverage with LCOV_EXCL markers"
  VERBATIM)

add_custom_target(
  coverage-html
  COMMAND
    ${_GENHTML_BIN} --rc branch_coverage=1 --rc
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
  COMMAND bash -c "${_CLEAN_CMD}"
  COMMENT "Cleaning coverage data"
  VERBATIM)

message(STATUS "Coverage targets available (${_COVERAGE_TOOLCHAIN}):")
message(STATUS "  coverage       - Run tests + show filtered text summary")
message(STATUS "  coverage-html  - Run tests + generate filtered HTML report")
message(STATUS "  coverage-clean - Clean all coverage data")
