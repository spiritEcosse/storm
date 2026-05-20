# Per-compiler reflection / modules wiring.
#
# Storm builds today with clang-p2996 (custom Clang fork shipping P2996
# reflection) against a custom libc++ at LIBCXX_ROOT. Stock GCC 16+ also
# implements P2996 against libstdc++. This file dispatches the right per- target
# flags for whichever compiler the build is configured against.
#
# Header-unit caveat: stock GCC 16's libstdc++ does NOT yet build cleanly as
# C++26 header units across the surface Storm uses (e.g. `<string>` and
# `<functional>` redeclare `std::erase` across their respective BMIs). So
# building Storm under GCC is gated on upstream libstdc++ modularisation landing
# first. The CMake wiring below is ready when that happens; until then,
# configure under Clang as before. See Issue #226 for the POC results.

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(NOT DEFINED LIBCXX_ROOT)
    message(
      FATAL_ERROR
        "LIBCXX_ROOT is required for Clang builds. Use a CMake preset or set -DLIBCXX_ROOT=<path>."
    )
  endif()

  set(LIBCXX_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/c++/v1")
  set(LIBCXX_BUILD_INCLUDE_DIR
      "${LIBCXX_ROOT}/build/include/x86_64-unknown-linux-gnu/c++/v1")
  message(STATUS "Using custom libcxx from: ${LIBCXX_ROOT}")

  add_compile_options(-nostdinc++ -I${LIBCXX_INCLUDE_DIR}
                      -I${LIBCXX_BUILD_INCLUDE_DIR})

  add_link_options(
    -nostdlib++ -L${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
    -Wl,-rpath,${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu -lc++ -lc++abi
    -lunwind)

  function(apply_cxx_flags target_name)
    target_compile_options(
      ${target_name} PRIVATE -fmodules -fbuiltin-module-map -freflection
                             -fannotation-attributes -fexpansion-statements)
  endfunction()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(STATUS "Using system libstdc++ (compiler: GNU)")

  function(apply_cxx_flags target_name)
    target_compile_options(${target_name} PRIVATE -fmodules -freflection)
    # GCC 16+ bundles annotation parsing and expansion statements into
    # `-freflection`, so no separate `-fannotation-attributes` /
    # `-fexpansion-statements` are needed (those are clang-p2996 fork flags).
  endfunction()

endif()
