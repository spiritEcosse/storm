set(CLANG_FORMAT_EXE
    "${CMAKE_SOURCE_DIR}/../clang-p2996/build/bin/clang-format"
    CACHE FILEPATH "Path to clang-format" FORCE)

include(${cmake-scripts_SOURCE_DIR}/formatting.cmake)

# C++ sources — grab everything, exclude build/third_party
file(
  GLOB_RECURSE
  FORMAT_CPP_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
list(FILTER FORMAT_CPP_SOURCES EXCLUDE REGEX ".*/build/.*|.*/third_party/.*")

# CMake files — grab everything, exclude build/third_party
file(GLOB_RECURSE FORMAT_CMAKE_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake")
list(FILTER FORMAT_CMAKE_FILES EXCLUDE REGEX ".*/build/.*|.*/third_party/.*")

clang_format(storm-clang-format ${FORMAT_CPP_SOURCES})
cmake_format(storm-cmake-format ${FORMAT_CMAKE_FILES})
