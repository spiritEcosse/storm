if(ENABLE_TESTS)
  message(STATUS "Testing is enabled.")

  cpmaddpackage(
    NAME
    googletest
    GITHUB_REPOSITORY
    google/googletest
    GIT_TAG
    v1.15.2
    VERSION
    1.15.2
    OPTIONS
    "INSTALL_GTEST OFF")
  set(GTEST_PARALLEL TRUE)

  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Testing is disabled.")
endif()
