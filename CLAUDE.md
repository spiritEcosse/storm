# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Last Updated**: Recent optimization work added batch operations, BaseStatement utilities, and significant performance improvements.

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

# Run comprehensive performance comparison (recommended)
./performance_comparison.sh    # Builds and runs all benchmarks with formatted results

# Or run individual benchmarks
./build/debug/benchmarks/bench_storm              # Basic Storm benchmark
./build/debug/benchmarks/bench_storm_optimized    # Optimized version with batch operations
./build/debug/benchmarks/bench_sqlite_orm         # sqlite_orm comparison
./build/debug/benchmarks/bench_sqlite             # Raw SQLite baseline
```

**Performance Results (10,000 operations):**
- **Storm ORM**: 10.46ms (~0.0010ms per operation)
- **Storm ORM (Optimized)**: Mixed batch/individual operations with statement caching
- **sqlite_orm**: 19.64ms (~0.0020ms per operation)

**Performance Gain: ~88% faster than sqlite_orm** (nearly 2x improvement)

## High-Level Architecture

### Module Structure (Optimized with Statement Separation)
```
src/
├── storm.cppm                      # Main module with meta functionality
├── db/
│   ├── concept.cppm                # Database concepts (DatabaseConnection, DatabaseStatement)
│   └── sqlite.cppm                 # SQLite Connection and Statement implementation
└── orm/
    ├── queryset.cppm               # QuerySet ORM interface
    └── statements/
        ├── base.cppm               # BaseStatement shared utilities and transaction management
        ├── insert.cppm             # InsertStatement with batch support
        └── remove.cppm             # RemoveStatement with bulk operations
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

#### 4. **Statement Architecture & Optimization**
Recent statement refactoring and optimization:
- **BaseStatement**: Shared utilities for transaction management, SQL execution patterns, and binding
- **Statement Separation**: Individual modules for InsertStatement and RemoveStatement with specialized optimizations
- **Batch Operations**: InsertStatement now supports `std::span<const T>` with bulk INSERT using multiple VALUES clauses
- **Performance Optimization**: Smart thresholds for bulk vs individual operations based on SQLite variable limits
- **Code Consolidation**: ~60% reduction in duplicated execution logic through BaseStatement utilities

#### 5. **Batch Operations Architecture**
The system provides two optimized batch operation strategies:

**InsertStatement Batch Support:**
- `execute(std::span<const T> objects)` for bulk insertions
- Bulk INSERT with multiple VALUES: `INSERT INTO table VALUES (...), (...), (...)`
- Smart threshold: ≤50 objects use bulk INSERT, >50 use individual statements with transactions
- Automatic transaction wrapping for multi-object operations

**RemoveStatement Batch Support:**
- `execute(std::span<const T> objects)` for bulk deletions
- Bulk DELETE with IN clause: `DELETE FROM table WHERE id IN (?,?,?)`
- Same smart thresholds as InsertStatement
- Optimized for primary key operations using reflection

### Cross-Module Dependencies

The import hierarchy:
```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
│   └── storm_db_concept
├── storm_orm_statements_base
│   └── storm_db_concept
├── storm_orm_statements_insert
│   ├── storm_orm_statements_base
│   ├── storm_db_concept
│   └── storm_db_sqlite
├── storm_orm_statements_remove
│   ├── storm_orm_statements_base
│   ├── storm_db_concept
│   └── storm_db_sqlite
└── storm_orm_queryset
    ├── storm_orm_statements_base
    ├── storm_orm_statements_insert
    ├── storm_orm_statements_remove
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
1. Create new statement class in `src/orm/statements/` inheriting from `BaseStatement<T>`
2. Implement both single-object and batch operations (`std::span<const T>`) if applicable
3. Use BaseStatement utilities for transaction management and common execution patterns
4. Add method to `QuerySet` class that delegates to the statement
5. Add comprehensive tests in `tests/test_sqlite.cpp`

### Working with Batch Operations
**InsertStatement batch operations:**
```cpp
std::vector<Person> people = {{1, "Alice", 25}, {2, "Bob", 30}};
auto result = queryset.insert(std::span<const Person>(people));
```

**RemoveStatement batch operations:**
```cpp
std::vector<Person> people_to_remove = {...};
auto result = queryset.remove(std::span<const Person>(people_to_remove));
```

### Optimizing Statement Performance
1. **Use BaseStatement utilities**: Leverage `execute_with_transaction()` and shared binding methods
2. **Implement smart thresholds**: Consider SQLite variable limits (`SQLITE_MAX_VARIABLE_NUMBER = 999`)
3. **Cache SQL strings**: Use static methods for SQL generation like `get_insert_sql()`
4. **Batch operations**: Support both bulk SQL (IN clauses, multiple VALUES) and individual statements with transactions

### Adding PostgreSQL Support
1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` to support multiple backends
4. Ensure concepts properly abstract differences