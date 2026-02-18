set(CLANG_FORMAT_EXE
    "${CMAKE_SOURCE_DIR}/../clang-p2996/build/bin/clang-format"
    CACHE FILEPATH "Path to clang-format" FORCE)

include(${cmake-scripts_SOURCE_DIR}/formatting.cmake)

file(
  GLOB_RECURSE
  FORMAT_CPP_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.hpp")

file(
  GLOB
  FORMAT_CMAKE_FILES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")

clang_format(storm-clang-format ${FORMAT_CPP_SOURCES})
cmake_format(storm-cmake-format ${FORMAT_CMAKE_FILES})
