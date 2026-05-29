# Isolated probe target for the `import std;` migration (issue #326, phase 2).
#
# Verifies the CMake plumbing end-to-end: * the
# CMAKE_EXPERIMENTAL_CXX_IMPORT_STD UUID gate is accepted, * libc++.modules.json
# is discoverable (depends on the symlink set up by cmake/libcxx.cmake), * a
# target with CXX_MODULE_STD ON builds and runs against the project's custom
# libc++.
#
# The probe deliberately does NOT call apply_clang_flags(), so the -fmodules /
# -fbuiltin-module-map flags that conflict with C++20 named modules are kept out
# of this TU. It also does NOT link libstorm — that isolation is the whole point
# of phase 2.

if(NOT ENABLE_IMPORT_STD_PROBE)
  return()
endif()

add_executable(storm_import_std_probe
               "${CMAKE_SOURCE_DIR}/tools/import_std_probe.cpp")

target_compile_features(storm_import_std_probe PRIVATE cxx_std_26)
set_target_properties(storm_import_std_probe PROPERTIES CXX_MODULE_STD ON)

message(STATUS "import-std probe enabled (storm_import_std_probe)")
