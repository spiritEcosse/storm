# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Storm is a modern C++26 ORM (Object-Relational Mapping) library for SQLite databases. It uses cutting-edge C++26 reflection features to automatically map C++ structs to database tables without macros.

## Build Commands

### Prerequisites
- Custom Clang with C++26 reflection support (located at `../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+
- Ninja build system

### Standard Development Build
```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug

# Run all tests
ctest --test-dir build/debug --output-on-failure

# Run a single test
cd build/debug && ./tests/storm_tests --gtest_filter="QuerySetRemoveTest.*"
```

### Release Build
```bash
cmake --preset ninja-release
cmake --build --preset ninja-release
```

### Code Quality
```bash
# Format code
cmake --build --preset ninja-debug --target format

# Check formatting (CI check)
cmake --build --preset ninja-debug --target format-check
```

### Sanitizer Builds
```bash
# Address + Leak sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug

# Thread sanitizer (separate build)
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread"
cmake --build --preset ninja-debug
```

### Benchmarking
```bash
# Build benchmarking infrastructure
cmake --preset ninja-debug -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-debug

# Build and run standalone benchmark (due to C++26 module conflicts)
cd benchmarks
/usr/bin/clang++ -std=c++20 -stdlib=libstdc++ -DENABLE_BENCH -I../build/debug/_deps/sqliteorm-src/include main.cpp sqlite_orm_wrapper.cpp -lsqlite3 -o main
./main
```

**Benchmark compares:**
- **Raw SQLite** (Storm's approach): Direct prepared statements
- **sqlite_orm**: Popular C++ ORM library

**Typical results show Storm's approach is ~4x faster for remove operations.**

## High-Level Architecture

### Module Structure (Simplified in Recent Refactor)
```
src/
├── storm.cppm           # Main module with meta functionality
├── db/
│   ├── concept.cppm     # Database concepts (DatabaseConnection, DatabaseStatement)
│   └── sqlite.cppm      # SQLite Connection and Statement implementation
└── orm/
    └── queryset.cppm    # QuerySet ORM interface and RemoveStatement
```

### Key Design Decisions

#### 1. **C++26 Reflection-Based ORM**
The system uses compile-time reflection (`std::meta`) to automatically:
- Find primary key fields marked with `[[=storm::meta::FieldAttr::primary]]`
- Generate SQL statements from struct definitions
- Bind struct fields to database columns

Example struct:
```cpp
struct Person {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};
```

#### 2. **Concept-Based Database Abstraction**
- `DatabaseConnection` concept defines interface for any database
- `DatabaseStatement` concept for prepared statements
- SQLite implementation satisfies these concepts
- Allows future PostgreSQL/MySQL support without changing ORM code

#### 3. **Connection Management**
- Default static connection for simple use cases
- Explicit connection passing for multi-database scenarios
- **Thread Safety**: SQLite opened with `SQLITE_OPEN_FULLMUTEX` for serialized mode
- **WARNING**: Connection management layer is NOT thread-safe due to compiler limitations with std::mutex in C++26 modules

#### 4. **Module Organization Changes**
Recent simplification (56% file reduction):
- Merged `meta` functionality into main `storm` module
- Consolidated `RemoveStatement` into `queryset` module
- Removed unnecessary `ConnectionAdapter` indirection
- Flattened directory structure (no single-file directories)

### Cross-Module Dependencies

The import hierarchy:
```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
│   └── storm_db_concept
└── storm_orm_queryset
    ├── storm_db_concept
    └── storm_db_sqlite
```

### Thread Safety Considerations

1. **SQLite Level**: Thread-safe with `SQLITE_OPEN_FULLMUTEX` flag
2. **Connection Management**: NOT thread-safe - requires external synchronization
3. **Recommended Patterns**:
   - Per-thread connections for best performance
   - External mutex around shared connection for simplicity
   - Future: Connection pooling implementation

### Compiler-Specific Requirements

This project requires the experimental Clang fork with C++26 reflection:
- Located at `../clang-p2996/`
- Custom libcxx with reflection support
- Module scanning with `clang-scan-deps`
- Reflection flags: `-freflection -fannotation-attributes`

### Testing Strategy

- **GoogleTest** with C++26 module support
- Tests in `tests/` directory
- Test database uses SQLite in-memory (`:memory:`)
- Each test creates fresh tables and data
- Comprehensive sanitizer support in CI

## Important Implementation Notes

- **No REFL-CPP**: Despite README mentioning it, the project now uses native C++26 reflection
- **Module Naming**: Uses underscores (`storm_db_sqlite`) not dots due to compiler limitations
- **Circular Dependencies**: Avoided by duplicating `FieldAttr` enum definition
- **Compiler Crashes**: std::mutex in modules causes segfaults in current Clang build
- **SQL Generation**: Runtime std::format (not constexpr yet)
- **Primary Key Access**: Uses reflection splice operator `obj.[:primary_key_:]`

## Common Development Tasks

### Adding a New Database Operation
1. Add method to `QuerySet` class
2. Create corresponding statement class in `queryset.cppm`
3. Use reflection to generate SQL and bind parameters
4. Add comprehensive tests in `tests/test_sqlite.cpp`

### Adding PostgreSQL Support
1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` to support multiple backends
4. Ensure concepts properly abstract differences