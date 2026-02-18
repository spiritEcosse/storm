# Coverage targets — must be included after test targets are defined (after tests.cmake).
#
# Combines data from three test binaries:
#   - storm_tests:             main tests (real SQLite/PostgreSQL)
#   - storm_orm_mock_tests:    SQLite mock tests via LD_PRELOAD (error paths)
#   - storm_orm_pg_mock_tests: PG mock tests via LD_PRELOAD (PG error paths)
if(ENABLE_COVERAGE AND ENABLE_TESTS)

    add_custom_target(coverage-run-main
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/coverage-run-batched.sh ${CMAKE_BINARY_DIR}
        DEPENDS storm_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running main tests with coverage instrumentation (batched)"
        VERBATIM
    )

    add_custom_target(coverage-run-mock
        COMMAND ${CMAKE_COMMAND} -E env
            LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/mock.profraw
            LD_PRELOAD=$<TARGET_FILE:mock_sqlite3>
            $<TARGET_FILE:storm_orm_mock_tests>
        DEPENDS storm_orm_mock_tests mock_sqlite3
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running mock tests with coverage instrumentation (LD_PRELOAD)"
        VERBATIM
    )

    add_custom_target(coverage-run-pg-mock
        COMMAND ${CMAKE_COMMAND} -E env
            LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/pg_mock.profraw
            LD_PRELOAD=$<TARGET_FILE:mock_libpq>
            $<TARGET_FILE:storm_orm_pg_mock_tests>
        DEPENDS storm_orm_pg_mock_tests mock_libpq
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running PG mock tests with coverage instrumentation (LD_PRELOAD)"
        VERBATIM
    )

    add_custom_target(coverage-run
        DEPENDS coverage-run-main coverage-run-mock coverage-run-pg-mock
        COMMENT "All coverage tests completed"
    )

    add_custom_target(coverage-merge
        COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
        COMMAND bash -c "${LLVM_PROFDATA} merge -sparse ${CMAKE_BINARY_DIR}/batch_*.profraw ${CMAKE_BINARY_DIR}/mock.profraw ${CMAKE_BINARY_DIR}/pg_mock.profraw -o ${COVERAGE_OUTPUT_DIR}/coverage.profdata"
        DEPENDS coverage-run
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Merging coverage data from batched main tests, mock tests, and PG mock tests"
        VERBATIM
    )

    add_custom_target(coverage-lcov
        COMMAND ${LLVM_COV} export
            $<TARGET_FILE:storm_tests>
            -object=$<TARGET_FILE:storm_orm_mock_tests>
            -object=$<TARGET_FILE:storm_orm_pg_mock_tests>
            -instr-profile=${COVERAGE_OUTPUT_DIR}/coverage.profdata
            -format=lcov
            -ignore-filename-regex="third_party|googletest|build|mock_sqlite|mock_libpq"
            ${CMAKE_SOURCE_DIR}/src
            > ${COVERAGE_OUTPUT_DIR}/coverage.lcov
        DEPENDS coverage-merge
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Exporting coverage data in LCOV format to ${COVERAGE_OUTPUT_DIR}/coverage.lcov"
        VERBATIM
    )

    set(LCOV_C_EXTENSIONS "c,h,i,C,H,I,icc,cpp,cc,cxx,hh,hpp,hxx,cppm")
    find_program(LCOV_TOOL lcov)
    find_program(GENHTML_TOOL genhtml)

    if(LCOV_TOOL)
        message(STATUS "lcov found: ${LCOV_TOOL}")

        add_custom_target(coverage
            COMMAND ${LCOV_TOOL}
                --rc branch_coverage=1
                --rc c_file_extensions=${LCOV_C_EXTENSIONS}
                --ignore-errors unused,deprecated,unsupported,inconsistent,range
                --filter region,branch_region
                --remove ${COVERAGE_OUTPUT_DIR}/coverage.lcov
                "*/third_party/*" "*/googletest/*" "*/build/*" "*/tests/*"
                --output-file ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
            COMMAND ${LCOV_TOOL}
                --rc branch_coverage=1
                --ignore-errors deprecated,inconsistent
                --summary ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
            DEPENDS coverage-lcov
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Filtering coverage with LCOV_EXCL markers"
            VERBATIM
        )

        if(GENHTML_TOOL)
            message(STATUS "genhtml found: ${GENHTML_TOOL}")

            add_custom_target(coverage-html
                COMMAND ${GENHTML_TOOL}
                    --rc branch_coverage=1
                    --rc c_file_extensions=${LCOV_C_EXTENSIONS}
                    --ignore-errors deprecated,range,inconsistent
                    --legend
                    --title "Storm ORM Coverage (filtered)"
                    --output-directory ${COVERAGE_OUTPUT_DIR}/html-filtered
                    ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
                DEPENDS coverage
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Generating filtered HTML coverage report in ${COVERAGE_OUTPUT_DIR}/html-filtered"
                VERBATIM
            )

            message(STATUS "  coverage      - Run tests + show filtered text summary")
            message(STATUS "  coverage-html - Run tests + generate filtered HTML report")
        else()
            message(STATUS "genhtml not found - coverage-html target disabled")
        endif()
    else()
        message(STATUS "lcov not found - coverage/coverage-html targets disabled")
        message(STATUS "  Manjaro/Arch: sudo pacman -S lcov")
        message(STATUS "  Ubuntu/Debian: sudo apt install lcov")
    endif()

    add_custom_target(coverage-clean
        COMMAND bash -c "rm -rf ${COVERAGE_OUTPUT_DIR} ${CMAKE_BINARY_DIR}/batch_*.profraw ${CMAKE_BINARY_DIR}/mock.profraw ${CMAKE_BINARY_DIR}/pg_mock.profraw"
        COMMENT "Cleaning coverage data"
        VERBATIM
    )

    message(STATUS "Coverage targets available:")
    message(STATUS "  coverage       - Run tests + show filtered text summary")
    message(STATUS "  coverage-html  - Run tests + generate filtered HTML report")
    message(STATUS "  coverage-clean - Clean all coverage data")
endif()
