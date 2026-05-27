if(NOT DEFINED LIBCXX_ROOT)
  message(
    FATAL_ERROR
      "LIBCXX_ROOT is required. Use a CMake preset or set -DLIBCXX_ROOT=<path>."
  )
endif()

set(LIBCXX_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/c++/v1")
set(LIBCXX_BUILD_INCLUDE_DIR
    "${LIBCXX_ROOT}/build/include/x86_64-unknown-linux-gnu/c++/v1")
message(STATUS "Using custom libcxx from: ${LIBCXX_ROOT}")

add_compile_options(-nostdinc++ -I${LIBCXX_INCLUDE_DIR}
                    -I${LIBCXX_BUILD_INCLUDE_DIR})

# Reflection flags must be GLOBAL, not per-target. Clang hashes compile flags
# into the module-cache key and also stamps them into every PCM. If the flags
# differ between the producer of a PCM and a consumer that imports it, clang
# refuses to load the PCM with a "configuration mismatch / Reflection was
# disabled in precompiled file" diagnostic.
#
# Putting reflection flags inside apply_clang_flags() (target_compile_options)
# meant they didn't propagate to every PCM producer — most notably any
# auto-generated CMake-internal targets that build .pcm files. Promoting them
# here via add_compile_options() guarantees uniform reflection state across
# every TU and every PCM in the project.
add_compile_options(-freflection -fannotation-attributes -fexpansion-statements)

add_link_options(
  -nostdlib++ -L${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
  -Wl,-rpath,${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu -lc++ -lc++abi
  -lunwind)

function(apply_clang_flags target_name)
  # Clang-modules (header-unit) support — required for any TU containing `import
  # <header>;` lines. Kept per-target rather than global because a future
  # migration may want to turn the Clang-modules path off for individual targets
  # without touching the whole tree. The reflection flags this helper used to
  # also add are now global (see above).
  target_compile_options(${target_name} PRIVATE -fmodules -fbuiltin-module-map)
endfunction()
