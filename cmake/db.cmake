# ── Database backends ─────────────────────────────────────────────────────────

find_package(SQLite3 REQUIRED)
find_package(PostgreSQL REQUIRED)

function(link_sqlite target_name)
  target_link_libraries(${target_name} PRIVATE SQLite::SQLite3)
endfunction()

function(link_postgresql target_name)
  target_link_libraries(${target_name} PRIVATE PostgreSQL::PostgreSQL)
endfunction()
