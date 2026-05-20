if(ENABLE_COVERAGE)
  message(STATUS "Code coverage is enabled")

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(
      WARNING
        "Coverage enabled with Release build - consider using Debug for better source mapping"
    )
  endif()

  find_program(GCOV gcov REQUIRED)
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)

  # GCC's --coverage is shorthand for -fprofile-arcs -ftest-coverage at compile
  # and -lgcov at link. Use CMAKE_CXX_FLAGS (not add_compile_options) for the
  # same module-instrumentation reasons documented for the LLVM toolchain.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")

  set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
  file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})
else()
  message(STATUS "Code coverage is disabled")
endif()
