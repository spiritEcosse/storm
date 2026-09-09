set(CLANG_FORMAT_EXE
    "${CMAKE_SOURCE_DIR}/../clang-p2996/build/bin/clang-format"
    CACHE FILEPATH "Path to clang-format" FORCE)

include(${cmake-scripts_SOURCE_DIR}/formatting.cmake)

# CONFIGURE_DEPENDS is intentionally omitted from both globs below. Using it
# causes an infinite CMake re-run loop: the globs match *.cmake files inside
# .cache/CPM/, which CPM populates during configure. Each new package adds files
# there, making the glob result change on every run and triggering another
# re-configure indefinitely. Without CONFIGURE_DEPENDS, adding new source files
# after configure requires a manual cmake re-run to appear in the format targets
# — an acceptable trade-off.

# C++ sources — grab everything, exclude build/third_party
file(GLOB_RECURSE FORMAT_CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
list(FILTER FORMAT_CPP_SOURCES EXCLUDE REGEX
     ".*/build/.*|.*/third_party/.*|.*/.cache/.*")

# CMake files — grab everything, exclude build/third_party/.cache
file(GLOB_RECURSE FORMAT_CMAKE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake")
list(FILTER FORMAT_CMAKE_FILES EXCLUDE REGEX
     ".*/build/.*|.*/third_party/.*|.*/.cache/.*")

clang_format(storm-clang-format ${FORMAT_CPP_SOURCES})
cmake_format(storm-cmake-format ${FORMAT_CMAKE_FILES})

# cmake_format() above is a no-op when cmake-format is absent: it creates no
# target, so commit.sh's cmake-format step fails with `ninja: error: unknown
# target 'cmake-format'`, naming the ninja symptom rather than the missing tool
# (#643). Upstream already reports this, but only as a STATUS line lost in
# configure output. Say it once, loudly, at the place that can be acted on.
# CMAKE_FORMAT_EXE is set by find_program() inside cmake-scripts'
# formatting.cmake.
if(NOT CMAKE_FORMAT_EXE)
  message(
    WARNING
      "cmake-format not found — the 'cmake-format' build target will not exist, "
      "so commit.sh's cmake-format step cannot run. Install cmakelang at the "
      "version docker/ci/Dockerfile pins, then re-run cmake. The docker/ci "
      "image ships it — see CLAUDE.md Prerequisites.")
endif()
