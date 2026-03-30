include(${CMAKE_SOURCE_DIR}/cmake/storm_migrations.cmake)

storm_enable_migrations(NAMESPACE "schema" MIGRATION_DIR
                        "tests/tools/migrations")
