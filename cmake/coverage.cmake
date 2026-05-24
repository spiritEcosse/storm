if(ENABLE_COVERAGE)
  message(STATUS "Code coverage is enabled")

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(
      WARNING
        "Coverage enabled with Release build - consider using Debug for better source mapping"
    )
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Use CMAKE_CXX_FLAGS (not add_compile_options) — required for correct C++26
    # module instrumentation. add_compile_options interacts differently with
    # module compilation, generating more phantom coverage regions that survive
    # the --filter region,branch_region lcov pass, causing false uncovered
    # lines.
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-generate")
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -fprofile-instr-generate")

    set(LLVM_PROFDATA "${LIBCXX_ROOT}/build/bin/llvm-profdata")
    set(LLVM_COV "${LIBCXX_ROOT}/build/bin/llvm-cov")

    if(NOT (EXISTS "${LLVM_PROFDATA}" AND EXISTS "${LLVM_COV}"))
      message(
        FATAL_ERROR
          "LLVM coverage tools not found. Ensure llvm-profdata and llvm-cov are available."
      )
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_program(GCOV gcov REQUIRED)
    find_program(LCOV lcov REQUIRED)
    find_program(GENHTML genhtml REQUIRED)

    # GCC's --coverage is shorthand for -fprofile-arcs -ftest-coverage at
    # compile and -lgcov at link. Use CMAKE_CXX_FLAGS (not add_compile_options)
    # for the same module-instrumentation reasons documented in the LLVM arm.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
  endif()

  set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
  file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})
else()
  message(STATUS "Code coverage is disabled")
endif()
