# Per-target reflection / modules flags for GCC.
# GCC 16+ bundles annotation parsing and expansion statements into
# `-freflection`, so no separate `-fannotation-attributes` /
# `-fexpansion-statements` flags are needed (those were clang-p2996 fork flags).
function(apply_cxx_flags target_name)
  target_compile_options(${target_name} PRIVATE -fmodules -freflection)
  # GCC has no built-in std module map. Opt in to CMake's experimental
  # CXX_MODULE_STD so it auto-builds libstdc++.modules.json before consumers
  # see `import std;`.
  set_target_properties(${target_name} PROPERTIES CXX_MODULE_STD ON)
endfunction()
