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

add_link_options(
  -nostdlib++ -L${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
  -Wl,-rpath,${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu -lc++ -lc++abi
  -lunwind)

function(apply_clang_flags target_name)
  target_compile_options(
    ${target_name} PRIVATE -fmodules -fbuiltin-module-map -freflection
                           -fannotation-attributes -fexpansion-statements)
endfunction()
