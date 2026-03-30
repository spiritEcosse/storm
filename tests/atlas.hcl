// Atlas configuration for Storm ORM test models.
// Run from repo root: atlas migrate diff <name> -c "file://tests/atlas.hcl" --env local

data "external_schema" "storm_sqlite" {
  program = [
    "./build/debug/tests/tools/storm_schema/storm_schema",
    "--dialect", "sqlite",
  ]
}

data "external_schema" "storm_pg" {
  program = [
    "./build/debug/tests/tools/storm_schema/storm_schema",
    "--dialect", "postgresql",
  ]
}

env "local" {
  src = data.external_schema.storm_sqlite.url
  dev = "sqlite://dev?mode=memory"
  migration {
    dir = "file://tests/tools/migrations"
  }
}

env "pg" {
  src = data.external_schema.storm_pg.url
  dev = "postgres://localhost:5432/storm_dev?sslmode=disable"
  migration {
    dir = "file://tests/tools/migrations/pg"
  }
}
