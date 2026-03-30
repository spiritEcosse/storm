# Database Migrations with Atlas

Storm integrates [Atlas](https://atlasgo.io) for database schema management. Models are auto-discovered via C++26 namespace reflection — any struct with `[[= FieldAttr::primary]]` in the target namespace is picked up automatically.

## Architecture

```
namespace schema { struct User { ... }; struct Post { ... }; }
        |
        v  compile-time reflection (std::meta::members_of)
auto-discover all structs with primary key
        |
        v
storm_enable_migrations(NAMESPACE "schema")  -->  schema binary + targets
        |
        v
cmake --build . --target makemigrations   -->  migrations/ (versioned SQL)
        |
        v
STORM_DB_URL="..." cmake --build . --target migrate  -->  live database
```

## Prerequisites

- **Storm library** linked to your project
- **Atlas CLI**: `curl -sSf https://atlasgo.sh | sh`

## Quick Start

### 1. Define Models in a Namespace

```cpp
// schema/models.h (or any file — auto-detected)
#pragma once
#include <optional>
#include <string>

namespace schema {

struct User {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::unique]] std::string email;
    std::string name;
    std::optional<std::string> bio;
};

struct Post {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string title;
    std::string body;
    [[= storm::meta::FieldAttr::fk]] User author;
};

// Add 1000 more models — all auto-discovered, no registration needed.

} // namespace schema
```

### 2. One Line of CMake

```cmake
include(cmake/storm_migrations.cmake)

storm_enable_migrations(NAMESPACE "schema")
```

That's it. Storm:
- Auto-detects the header containing `namespace schema {`
- Generates a schema binary using compile-time reflection
- Creates `makemigrations`, `migrate`, `migrate-validate`, `migrate-status`, and `migrate-hash` targets

### 3. Generate and Apply Migrations

```bash
# Generate a migration from model changes
cmake --build . --target makemigrations

# Apply to database
STORM_DB_URL="sqlite://myapp.db" cmake --build . --target migrate

# Validate migration directory integrity (checksums, order)
cmake --build . --target migrate-validate

# Show which migrations are applied vs pending
STORM_DB_URL="sqlite://myapp.db" cmake --build . --target migrate-status

# Recalculate atlas.sum after manual edits to migration files
cmake --build . --target migrate-hash
```

## How Auto-Discovery Works

At compile time, the generated schema binary:

1. Calls `std::meta::members_of(^^schema)` to enumerate all types in the namespace
2. Filters to class types only (`is_class_type`) — skips enums, aliases, etc.
3. Checks each class for a `[[= FieldAttr::primary]]` annotation
4. Emits `CREATE TABLE` + `CREATE INDEX` SQL for each model found

No manual registration, no model list, no macros.

## CMake Function Reference

```cmake
storm_enable_migrations(
  NAMESPACE     <name>          # Required: namespace containing model structs
  MODELS_HEADER <path>          # Optional: explicit header (skips auto-detection)
  MIGRATION_DIR <path>          # Optional: migration directory (default: "migrations")
  DIALECT       <dialect>       # Optional: "sqlite" or "postgresql" (default: "sqlite")
  TARGET_NAME   <name>          # Optional: schema binary name (default: "storm_schema")
)
```

## Manual Schema Export

The generated schema binary supports direct use:

```bash
# Print SQLite schema to stdout
./build/debug/storm_schema

# PostgreSQL dialect
./build/debug/storm_schema --dialect postgresql

# Write to file
./build/debug/storm_schema --output schema.sql
```

## Integration Tests

Storm dogfoods `storm_enable_migrations()` in `tests/tools/storm_schema/`. Run the test suite:

```bash
./scripts/test-atlas-migration.sh
```

## Supported Backends

| Backend    | Schema export | Declarative apply | Versioned migrations |
|-----------|--------------|-------------------|---------------------|
| SQLite     | Yes          | Yes               | Yes                 |
| PostgreSQL | Yes          | Yes               | Yes                 |
