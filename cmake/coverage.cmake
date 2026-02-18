if(ENABLE_COVERAGE)
    message(STATUS "Code coverage is enabled")

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING "Coverage enabled with Release build - consider using Debug for better source mapping")
    endif()

    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)

    set(LLVM_PROFDATA "${LIBCXX_ROOT}/build/bin/llvm-profdata")
    set(LLVM_COV "${LIBCXX_ROOT}/build/bin/llvm-cov")

    if(NOT (EXISTS "${LLVM_PROFDATA}" AND EXISTS "${LLVM_COV}"))
        message(FATAL_ERROR "LLVM coverage tools not found. Ensure llvm-profdata and llvm-cov are available.")
    endif()

    set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})
else()
    message(STATUS "Code coverage is disabled")
endif()
