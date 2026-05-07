# storm_enable_migrations() — auto-discover models via namespace reflection.
#
# Usage: storm_enable_migrations(NAMESPACE "schema")
#
# Optional: MODELS_HEADER  "path/to/models.h"   # Explicit header (skips
# auto-detection) MIGRATION_DIR  "migrations"          # Where Atlas stores
# migrations (default) DIALECT        "sqlite"              # Default dialect
# (default: sqlite) TARGET_NAME    "storm_schema"        # Schema binary name
# (default)
#
# What it does: 1. Auto-detects the header containing `namespace <NAMESPACE> {`
# 2. Generates a schema binary that reflects over the namespace and discovers
# all structs with [[= FieldAttr::primary]] 3. Creates `makemigrations` and
# `migrate` CMake targets
#
# Requires: Atlas CLI (https://atlasgo.io), storm library linked.

function(storm_enable_migrations)
  cmake_parse_arguments(
    ARG "" "NAMESPACE;MODELS_HEADER;MIGRATION_DIR;DIALECT;TARGET_NAME" ""
    ${ARGN})

  # Defaults
  if(NOT ARG_NAMESPACE)
    message(FATAL_ERROR "storm_enable_migrations: NAMESPACE is required")
  endif()
  if(NOT ARG_TARGET_NAME)
    set(ARG_TARGET_NAME "storm_schema")
  endif()
  if(NOT ARG_MIGRATION_DIR)
    set(ARG_MIGRATION_DIR "migrations")
  endif()
  if(NOT ARG_DIALECT)
    set(ARG_DIALECT "sqlite")
  endif()

  # Auto-detect headers containing the namespace
  if(NOT ARG_MODELS_HEADER)
    file(GLOB_RECURSE _candidate_headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    set(_found_headers "")
    foreach(_hdr ${_candidate_headers})
      file(READ "${_hdr}" _content)
      string(FIND "${_content}" "namespace ${ARG_NAMESPACE}" _pos)
      if(NOT _pos EQUAL -1)
        list(APPEND _found_headers "${_hdr}")
      endif()
    endforeach()
    if(NOT _found_headers)
      message(
        FATAL_ERROR
          "storm_enable_migrations: could not find any header containing 'namespace ${ARG_NAMESPACE}' "
          "under ${CMAKE_CURRENT_SOURCE_DIR}. Set MODELS_HEADER explicitly.")
    endif()
    # Build #include lines for all detected headers
    set(STORM_MODELS_INCLUDES "")
    foreach(_hdr ${_found_headers})
      string(APPEND STORM_MODELS_INCLUDES "#include \"${_hdr}\"\n")
      message(STATUS "storm_enable_migrations: auto-detected: ${_hdr}")
    endforeach()
  else()
    # Resolve relative path
    if(NOT IS_ABSOLUTE "${ARG_MODELS_HEADER}")
      set(_abs_header "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MODELS_HEADER}")
    else()
      set(_abs_header "${ARG_MODELS_HEADER}")
    endif()
    set(STORM_MODELS_INCLUDES "#include \"${_abs_header}\"\n")
  endif()

  set(STORM_MODELS_NAMESPACE "${ARG_NAMESPACE}")
  set(STORM_SCHEMA_TARGET "${ARG_TARGET_NAME}")

  # Generate main.cpp from template
  set(_generated_main "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET_NAME}_main.cpp")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/storm_schema_main.cpp.in"
                 "${_generated_main}" @ONLY)

  # Build schema binary
  add_executable(${ARG_TARGET_NAME} "${_generated_main}")
  apply_clang_flags(${ARG_TARGET_NAME})
  target_link_libraries(${ARG_TARGET_NAME} PRIVATE storm)
  link_sqlite(${ARG_TARGET_NAME})
  link_postgresql(${ARG_TARGET_NAME})
  target_include_directories(
    ${ARG_TARGET_NAME}
    PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src"
            "${CMAKE_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}")

  # Absolute paths for targets
  set(_schema_bin "$<TARGET_FILE:${ARG_TARGET_NAME}>")
  set(_migration_dir "${CMAKE_SOURCE_DIR}/${ARG_MIGRATION_DIR}")

  # makemigrations target
  add_custom_target(
    makemigrations
    COMMAND "${_schema_bin}" --dialect "${ARG_DIALECT}" --output
            "${CMAKE_CURRENT_BINARY_DIR}/_storm_schema.sql"
    COMMAND
      atlas migrate diff --dir "file://${_migration_dir}" --dev-url
      "sqlite://dev?mode=memory" --to
      "file://${CMAKE_CURRENT_BINARY_DIR}/_storm_schema.sql"
    DEPENDS ${ARG_TARGET_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT
      "Generating Atlas migration from ${ARG_TARGET_NAME} (namespace: ${ARG_NAMESPACE}, dialect: ${ARG_DIALECT})"
    VERBATIM)

  # migrate target
  add_custom_target(
    migrate
    COMMAND atlas migrate apply --dir "file://${_migration_dir}" --url
            "$ENV{STORM_DB_URL}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT
      "Applying migrations from ${ARG_MIGRATION_DIR} (set STORM_DB_URL env var)"
    VERBATIM)

  # migrate-validate target
  add_custom_target(
    migrate-validate
    COMMAND atlas migrate validate --dir "file://${_migration_dir}" --dev-url
            "sqlite://dev?mode=memory"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Validating migration directory integrity (checksums, order)"
    VERBATIM)

  # migrate-status target
  add_custom_target(
    migrate-status
    COMMAND atlas migrate status --dir "file://${_migration_dir}" --url
            "$ENV{STORM_DB_URL}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT
      "Showing migration status (applied vs pending, set STORM_DB_URL env var)"
    VERBATIM)

  # migrate-hash target
  add_custom_target(
    migrate-hash
    COMMAND atlas migrate hash --dir "file://${_migration_dir}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Recalculating atlas.sum checksums in ${ARG_MIGRATION_DIR}"
    VERBATIM)

  # validate-schema target
  add_custom_target(
    validate-schema
    COMMAND "${_schema_bin}" --dialect "${ARG_DIALECT}" --output
            "${CMAKE_CURRENT_BINARY_DIR}/_storm_schema.sql"
    COMMAND
      atlas schema diff --from "$ENV{STORM_DB_URL}" --to
      "file://${CMAKE_CURRENT_BINARY_DIR}/_storm_schema.sql" --dev-url
      "sqlite://dev?mode=memory"
    DEPENDS ${ARG_TARGET_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Validating schema matches database (set STORM_DB_URL env var)"
    VERBATIM)

endfunction()
