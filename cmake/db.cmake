# ── Database backends ─────────────────────────────────────────────────────────

find_package(SQLite3 3.35.0 REQUIRED)
find_package(PostgreSQL REQUIRED)

# ── SQLite version-aware feature detection ───────────────────────────────────
# Minimum: 3.35.0 (RETURNING clause — enforced above) Compute
# STORM_SQLITE_VERSION_NUMBER (matches SQLite's encoding: major*1000000 +
# minor*1000 + patch)

string(REPLACE "." ";" _sqlite_ver_parts "${SQLite3_VERSION}")
list(GET _sqlite_ver_parts 0 _sqlite_major)
list(GET _sqlite_ver_parts 1 _sqlite_minor)
list(GET _sqlite_ver_parts 2 _sqlite_patch)
math(EXPR STORM_SQLITE_VERSION_NUMBER
     "${_sqlite_major} * 1000000 + ${_sqlite_minor} * 1000 + ${_sqlite_patch}")

message(
  STATUS "SQLite version: ${SQLite3_VERSION} (${STORM_SQLITE_VERSION_NUMBER})")

# STRICT tables (SQLite 3.37.0+)
if(STORM_SQLITE_VERSION_NUMBER GREATER_EQUAL 3037000)
  set(STORM_SQLITE_STRICT_TABLES ON)
endif()

# RIGHT and FULL OUTER JOIN (SQLite 3.39.0+)
if(STORM_SQLITE_VERSION_NUMBER GREATER_EQUAL 3039000)
  set(STORM_SQLITE_RIGHT_JOIN ON)
endif()

unset(_sqlite_ver_parts)
unset(_sqlite_major)
unset(_sqlite_minor)
unset(_sqlite_patch)

function(link_sqlite target_name)
  target_link_libraries(${target_name} PRIVATE SQLite::SQLite3)

  target_compile_definitions(
    ${target_name}
    PRIVATE STORM_SQLITE_VERSION_NUMBER=${STORM_SQLITE_VERSION_NUMBER})

  if(STORM_SQLITE_STRICT_TABLES)
    target_compile_definitions(${target_name}
                               PRIVATE STORM_SQLITE_STRICT_TABLES=1)
  endif()
  if(STORM_SQLITE_RIGHT_JOIN)
    target_compile_definitions(${target_name} PRIVATE STORM_SQLITE_RIGHT_JOIN=1)
  endif()
endfunction()

function(link_postgresql target_name)
  target_link_libraries(${target_name} PRIVATE PostgreSQL::PostgreSQL)
endfunction()
