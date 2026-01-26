cmake_minimum_required(VERSION 3.30)
project(storm)

set(CMAKE_CXX_STANDARD 26)
message(STATUS "Using C++26 (standard build)")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Common compiler options - applied to all targets (Clang)
# Apply template backtrace limit globally for debug builds
add_compile_options($<$<CONFIG:Debug>:-ftemplate-backtrace-limit=0>)

# For Clang (including AppleClang) with CMAKE_CXX_STANDARD set to 26,
# C++ coroutines are typically enabled by the standard itself.
# The explicit -fcoroutines flag can cause errors with modern Clang.
add_compile_options(-ferror-limit=1) # Clang specific error limit

# Add standard libcxx flags for non-custom builds
if(NOT DEFINED LIBCXX_ROOT)
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

# Configure libcxx paths if LIBCXX_ROOT is provided
if(DEFINED LIBCXX_ROOT)
    set(LIBCXX_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/c++/v1")
    set(LIBCXX_BUILD_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/x86_64-unknown-linux-gnu/c++/v1")
    message(STATUS "Using custom libcxx from: ${LIBCXX_ROOT}")

    # Add global custom libc++ link options for all executables
    add_link_options(
        -nostdlib++
        -L${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
        -Wl,-rpath,${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
        -lc++
        -lc++abi
        -lunwind
    )
    message(STATUS "Global custom libc++ linking configured")
endif()

 # Build the core library (modules-first)
 add_library(${PROJECT_NAME} STATIC)
 
 # Collect sources automatically
# Collect all modules automatically from src subdirectories
file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
)
 
 # Declare C++26 module interface and partition sources (all *.cppm)
 
 target_sources(${PROJECT_NAME}
     PUBLIC
         FILE_SET cxx_modules TYPE CXX_MODULES
         FILES
             ${STORM_MODULE_UNITS}
 )
 

# Function to apply Clang-specific flags (modules and reflection) to a target
function(apply_clang_flags target_name)
    # Enable Clang modules and builtin module maps
    target_compile_options(${target_name} PRIVATE -fmodules -fbuiltin-module-map)
    message(STATUS "Module flags enabled for ${target_name}")

    # Add custom libcxx and reflection flags when using custom libcxx
    if(DEFINED LIBCXX_ROOT)
        target_compile_options(${target_name} PRIVATE
            -freflection
            -fannotation-attributes
            -fexpansion-statements
            -nostdinc++
            -I${LIBCXX_INCLUDE_DIR}
            -I${LIBCXX_BUILD_INCLUDE_DIR}
        )
        message(STATUS "Reflection flags enabled for ${target_name}")
    endif()
endfunction()

 # Add Clang-specific flags (modules and reflection) to Storm library
 apply_clang_flags(${PROJECT_NAME})
 
# Group sources by folder in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES
    ${STORM_MODULE_UNITS}
)
 
 # Includes for consumers and internal builds
 target_include_directories(${PROJECT_NAME}
     PUBLIC
         "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/src"
         "${CMAKE_CURRENT_SOURCE_DIR}/include"
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
     )

# Ensure compile_commands.json is generated
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/envs.cmake)

# Find PostgreSQL package
# find_package(PostgreSQL REQUIRED)

# Function to link SQLite to a target
function(link_sqlite target_name)
    if (SQLite3_FOUND)
        target_link_libraries(${target_name} PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(${target_name} PRIVATE sqlite3)
    endif()
endfunction()

# Find SQLite3 package and link it
find_package(SQLite3)
link_sqlite(${PROJECT_NAME})

# start CPM.cmake
set(CPM_DOWNLOAD_VERSION 0.40.5)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
            https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
            ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

# target_link_libraries(${PROJECT_NAME}
#     PRIVATE
#     PostgreSQL::PostgreSQL
# )

option(ENABLE_TESTS "Enable testing support" OFF)
option(ENABLE_BENCH "Enable benchmarking with sqlite_orm" OFF)
option(ENABLE_COVERAGE "Enable code coverage (requires LLVM tools)" OFF)

# Code coverage configuration
if(ENABLE_COVERAGE)
    message(STATUS "Code coverage is enabled")

    # Coverage requires debug info for meaningful reports
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING "Coverage enabled with Release build - consider using Debug for better source mapping")
    endif()

    # LLVM source-based coverage flags
    set(COVERAGE_COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping")
    set(COVERAGE_LINK_FLAGS "-fprofile-instr-generate")

    # Apply coverage flags globally
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_COMPILE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_LINK_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${COVERAGE_LINK_FLAGS}")

    # Find LLVM coverage tools (prefer from custom Clang build)
    if(DEFINED LIBCXX_ROOT)
        set(LLVM_PROFDATA "${LIBCXX_ROOT}/build/bin/llvm-profdata")
        set(LLVM_COV "${LIBCXX_ROOT}/build/bin/llvm-cov")
    else()
        find_program(LLVM_PROFDATA llvm-profdata REQUIRED)
        find_program(LLVM_COV llvm-cov REQUIRED)
    endif()

    if(EXISTS "${LLVM_PROFDATA}" AND EXISTS "${LLVM_COV}")
        message(STATUS "LLVM coverage tools found:")
        message(STATUS "  llvm-profdata: ${LLVM_PROFDATA}")
        message(STATUS "  llvm-cov: ${LLVM_COV}")
    else()
        message(FATAL_ERROR "LLVM coverage tools not found. Ensure llvm-profdata and llvm-cov are available.")
    endif()

    # Coverage output directory
    set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})

    message(STATUS "Coverage output directory: ${COVERAGE_OUTPUT_DIR}")
    message(STATUS "Coverage compile flags: ${COVERAGE_COMPILE_FLAGS}")
else()
    message(STATUS "Code coverage is disabled")
endif()

if (ENABLE_TESTS)
    message(STATUS "Testing is enabled.")

    # Configure GoogleTest with custom libcxx if needed
    if(DEFINED LIBCXX_ROOT)
        set(CMAKE_CXX_FLAGS_BACKUP "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++ -I${LIBCXX_INCLUDE_DIR} -I${LIBCXX_BUILD_INCLUDE_DIR}")
        message(STATUS "Configuring GoogleTest with custom libcxx")
    endif()

    # Start adding gtest
    CPMAddPackage(
            NAME googletest
            GITHUB_REPOSITORY google/googletest
            GIT_TAG v1.15.2
            VERSION 1.15.2
            OPTIONS
            "INSTALL_GTEST OFF"
    )
    set(GTEST_PARALLEL TRUE)

    # Restore original CMAKE_CXX_FLAGS
    if(DEFINED LIBCXX_ROOT)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_BACKUP}")
    endif()

    # End adding gtest
    enable_testing()
    add_subdirectory(tests)
else ()
    message(STATUS "Testing is disabled.")
endif ()

# Add benchmarks subdirectory if enabled
if(ENABLE_BENCH)
    message(STATUS "Benchmarking is enabled")
    add_subdirectory(benchmarks)
else()
    message(STATUS "Benchmarking is disabled")
endif()


# Add cmake-scripts for sanitizers and other utilities
CPMAddPackage(
    NAME cmake-scripts
    GITHUB_REPOSITORY StableCoder/cmake-scripts
    GIT_TAG 24.08.1
)

# Add sanitizer option
option(USE_SANITIZER "Compile with sanitizer support. Options: address, leak, undefined, thread, memory, memorywithorigins, cfi" "")

# Configure sanitizer options for memory and memorywithorigins
# Memory sanitizer is only available in Clang

# Apply sanitizers if any are selected
if(USE_SANITIZER)
    message(STATUS "Building with sanitizer: ${USE_SANITIZER}")
    # Include sanitizer modules only when needed and compatible
    include(${cmake-scripts_SOURCE_DIR}/sanitizers.cmake)
    
    # Check if memory sanitizer is requested but not available
    if("${USE_SANITIZER}" STREQUAL "memory" OR "${USE_SANITIZER}" STREQUAL "memorywithorigins")
        message(STATUS "Memory sanitizer options configured")
        set_sanitizer_options(memory DEFAULT -fno-omit-frame-pointer)
        set_sanitizer_options(memorywithorigins DEFAULT
                            SANITIZER memory
                            -fno-omit-frame-pointer 
                            -fsanitize-memory-track-origins)
    else()
        # This will automatically check if the sanitizers are available and apply them
        add_sanitizer_support(${USE_SANITIZER})
        message(STATUS "Sanitizer options configured for ${USE_SANITIZER}")
    endif()
    
    # Make sure we have debug symbols for better error reporting
    add_compile_options(-g)
endif()

 # Clang-Format integration
 find_program(CLANG_FORMAT_EXE NAMES clang-format)
 if(CLANG_FORMAT_EXE)
   # Collect files to format (modules, sources, headers, and tests)
   file(GLOB_RECURSE CLANG_FORMAT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ixx"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
   )

  # Use repo style if present, otherwise fall back to LLVM
  set(CLANG_FORMAT_STYLE "--style=file")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format" AND
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/_clang-format")
    set(CLANG_FORMAT_STYLE "--style=LLVM")
  endif()

   add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i ${CLANG_FORMAT_STYLE} ${CLANG_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running clang-format on source files"
    VERBATIM
  )

   add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXE}" ${CLANG_FORMAT_STYLE} --dry-run --Werror ${CLANG_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking code style with clang-format (fails on diffs)"
    VERBATIM
  )
 else()
   message(STATUS "clang-format not found; 'format' and 'format-check' targets are disabled")
 endif()

 # Print build configuration
 message(STATUS "=== Storm Build Configuration ===")
 message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
 message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
 message(STATUS "Testing: ${ENABLE_TESTS}")
 message(STATUS "Benchmarks: ${ENABLE_BENCH}")
 message(STATUS "Coverage: ${ENABLE_COVERAGE}")
 message(STATUS "Sanitizer: ${USE_SANITIZER}")
 message(STATUS "===================================")

# Coverage report generation targets (only when coverage is enabled)
if(ENABLE_COVERAGE AND ENABLE_TESTS)
    # Custom target to generate coverage report
    # Usage:
    #   1. Build with coverage: cmake --preset ninja-debug -DENABLE_COVERAGE=ON -DENABLE_TESTS=ON -B build/debug-coverage
    #   2. Build: cmake --build build/debug-coverage
    #   3. Run coverage: cmake --build build/debug-coverage --target coverage
    #   OR run steps separately:
    #   3a. Run tests: cmake --build build/debug-coverage --target coverage-run
    #   3b. Generate report: cmake --build build/debug-coverage --target coverage-report
    #
    # Coverage combines data from TWO test binaries:
    #   - storm_tests: Main tests with real SQLite (covers normal paths)
    #   - storm_orm_mock_tests: Mock tests with LD_PRELOAD (covers [[unlikely]] error paths)
    #
    # Both binaries link against the same libstorm, so coverage data merges correctly.

    # Run main tests with coverage instrumentation
    add_custom_target(coverage-run-main
        COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/main.profraw
            $<TARGET_FILE:storm_tests>
        DEPENDS storm_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running main tests with coverage instrumentation"
        VERBATIM
    )

    # Run mock tests with LD_PRELOAD to cover [[unlikely]] error paths
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

    # Combined target to run both test suites
    add_custom_target(coverage-run
        DEPENDS coverage-run-main coverage-run-mock
        COMMENT "All coverage tests completed"
    )

    # Merge coverage data from both test runs
    add_custom_target(coverage-merge
        COMMAND ${LLVM_PROFDATA} merge
            -sparse
            -o ${COVERAGE_OUTPUT_DIR}/coverage.profdata
            ${CMAKE_BINARY_DIR}/main.profraw
            ${CMAKE_BINARY_DIR}/mock.profraw
        DEPENDS coverage-run
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Merging coverage data from main and mock tests"
        VERBATIM
    )

    # HTML coverage report (uses both binaries as objects for complete coverage)
    add_custom_target(coverage-report
        COMMAND ${LLVM_COV} show
            $<TARGET_FILE:storm_tests>
            -object=$<TARGET_FILE:storm_orm_mock_tests>
            -instr-profile=${COVERAGE_OUTPUT_DIR}/coverage.profdata
            -format=html
            -output-dir=${COVERAGE_OUTPUT_DIR}/html
            -show-line-counts-or-regions
            -show-instantiations=false
            -ignore-filename-regex="third_party|googletest|build|mock_sqlite"
            ${CMAKE_SOURCE_DIR}/src
        DEPENDS coverage-merge
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating HTML coverage report in ${COVERAGE_OUTPUT_DIR}/html"
        VERBATIM
    )

    # Text summary report
    add_custom_target(coverage-summary
        COMMAND ${LLVM_COV} report
            $<TARGET_FILE:storm_tests>
            -object=$<TARGET_FILE:storm_orm_mock_tests>
            -instr-profile=${COVERAGE_OUTPUT_DIR}/coverage.profdata
            -ignore-filename-regex="third_party|googletest|build|mock_sqlite"
            ${CMAKE_SOURCE_DIR}/src
        DEPENDS coverage-merge
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Coverage summary:"
        VERBATIM
    )

    # Export coverage data in LCOV format (for CI integration)
    add_custom_target(coverage-lcov
        COMMAND ${LLVM_COV} export
            $<TARGET_FILE:storm_tests>
            -object=$<TARGET_FILE:storm_orm_mock_tests>
            -instr-profile=${COVERAGE_OUTPUT_DIR}/coverage.profdata
            -format=lcov
            -ignore-filename-regex="third_party|googletest|build|mock_sqlite"
            ${CMAKE_SOURCE_DIR}/src
            > ${COVERAGE_OUTPUT_DIR}/coverage.lcov
        DEPENDS coverage-merge
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Exporting coverage data in LCOV format to ${COVERAGE_OUTPUT_DIR}/coverage.lcov"
        VERBATIM
    )

    # Find lcov tool for filtering LCOV_EXCL markers
    find_program(LCOV_TOOL lcov)
    find_program(GENHTML_TOOL genhtml)
    find_program(PYTHON3 python3)

    if(LCOV_TOOL)
        message(STATUS "lcov found: ${LCOV_TOOL}")

        # Filter coverage with LCOV_EXCL markers applied
        # Note: llvm-cov doesn't process LCOV_EXCL markers, so we use a custom script
        add_custom_target(coverage-filtered
            # Step 1: Remove third-party/test files with lcov
            COMMAND ${LCOV_TOOL}
                --rc branch_coverage=1
                --ignore-errors unused,deprecated
                --remove ${COVERAGE_OUTPUT_DIR}/coverage.lcov
                "*/third_party/*" "*/googletest/*" "*/build/*" "*/tests/*"
                --output-file ${COVERAGE_OUTPUT_DIR}/coverage-temp.lcov
            # Step 2: Apply LCOV_EXCL_START/STOP markers with our script
            COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/scripts/filter_lcov_excl.py
                ${COVERAGE_OUTPUT_DIR}/coverage-temp.lcov
                ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
            # Step 3: Show summary
            COMMAND ${LCOV_TOOL}
                --rc branch_coverage=1
                --ignore-errors deprecated
                --summary ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
            DEPENDS coverage-lcov
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Filtering coverage with LCOV_EXCL markers"
            VERBATIM
        )

        if(GENHTML_TOOL)
            message(STATUS "genhtml found: ${GENHTML_TOOL}")

            # Generate HTML report with exclusions applied
            # Note: 'category' errors occur because we remove DA: entries but FNL: references remain
            add_custom_target(coverage-filtered-html
                COMMAND ${GENHTML_TOOL}
                    --rc branch_coverage=1
                    --ignore-errors deprecated,range,category
                    --legend
                    --title "Storm ORM Coverage (filtered)"
                    --output-directory ${COVERAGE_OUTPUT_DIR}/html-filtered
                    ${COVERAGE_OUTPUT_DIR}/coverage-filtered.lcov
                DEPENDS coverage-filtered
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Generating filtered HTML coverage report in ${COVERAGE_OUTPUT_DIR}/html-filtered"
                VERBATIM
            )

            message(STATUS "  coverage-filtered      - Apply LCOV_EXCL markers and show summary")
            message(STATUS "  coverage-filtered-html - Generate HTML with LCOV_EXCL exclusions")
        else()
            message(STATUS "genhtml not found - coverage-filtered-html target disabled")
            message(STATUS "  Install lcov package for HTML reports with exclusions")
        endif()
    else()
        message(STATUS "lcov not found - coverage-filtered targets disabled")
        message(STATUS "  Install lcov package to enable LCOV_EXCL marker support")
        message(STATUS "  Manjaro/Arch: sudo pacman -S lcov")
        message(STATUS "  Ubuntu/Debian: sudo apt install lcov")
    endif()

    # Clean coverage data
    add_custom_target(coverage-clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            ${COVERAGE_OUTPUT_DIR}
            ${CMAKE_BINARY_DIR}/main.profraw
            ${CMAKE_BINARY_DIR}/mock.profraw
            ${CMAKE_BINARY_DIR}/*.profraw
        COMMENT "Cleaning coverage data"
        VERBATIM
    )

    # Combined target: run tests + generate HTML report
    add_custom_target(coverage
        DEPENDS coverage-report
        COMMENT "Coverage complete! Open ${COVERAGE_OUTPUT_DIR}/html/index.html in browser"
    )

    # Combined target: run tests + show summary
    add_custom_target(coverage-quick
        DEPENDS coverage-summary
        COMMENT "Quick coverage check complete"
    )

    message(STATUS "Coverage targets available:")
    message(STATUS "  coverage         - Run ALL tests and generate HTML report")
    message(STATUS "  coverage-quick   - Run ALL tests and show summary")
    message(STATUS "  coverage-run     - Run ALL tests (main + mock)")
    message(STATUS "  coverage-run-main  - Run main tests only")
    message(STATUS "  coverage-run-mock  - Run mock tests only (LD_PRELOAD)")
    message(STATUS "  coverage-report  - Generate HTML report (after coverage-run)")
    message(STATUS "  coverage-summary - Show text summary (after coverage-run)")
    message(STATUS "  coverage-lcov    - Export LCOV format (for CI)")
    message(STATUS "  coverage-clean   - Clean all coverage data")
endif()
