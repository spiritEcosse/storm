option(
  USE_SANITIZER
  "Compile with sanitizer support. Options: address, leak, undefined, thread, memory, memorywithorigins, cfi"
  "")

if(USE_SANITIZER)
  message(STATUS "Building with sanitizer: ${USE_SANITIZER}")

  include(${cmake-scripts_SOURCE_DIR}/sanitizers.cmake)

  if("${USE_SANITIZER}" STREQUAL "memory" OR "${USE_SANITIZER}" STREQUAL
                                             "memorywithorigins")
    message(STATUS "Memory sanitizer options configured")
    set_sanitizer_options(memory DEFAULT -fno-omit-frame-pointer)
    set_sanitizer_options(
      memorywithorigins DEFAULT SANITIZER memory -fno-omit-frame-pointer
      -fsanitize-memory-track-origins)
  else()
    add_sanitizer_support(${USE_SANITIZER})
    message(STATUS "Sanitizer options configured for ${USE_SANITIZER}")
  endif()

  # Better stack traces for all sanitizers
  add_compile_options(-g -fno-omit-frame-pointer)
endif()
