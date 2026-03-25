option(ENABLE_PYTHON "Build Python bindings via nanobind" OFF)

if(ENABLE_PYTHON)
  add_subdirectory(python)
endif()
