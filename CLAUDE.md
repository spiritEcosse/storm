# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Last Updated**: Recent optimization work added compile-time index sequence optimization, batch operations, BaseStatement utilities, and significant performance improvements. Latest optimization replaces recursive template approach with std::index_sequence and fold expressions for field binding.

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
- **Storm ORM**: 3.06ms (~0.0003ms per operation) - with index sequence optimization
- **Storm ORM (Optimized)**: Mixed batch/individual operations with statement caching and compile-time field binding
- **sqlite_orm**: 20.25ms (~0.0020ms per operation)

**Performance Gain: ~6.6x faster than sqlite_orm** with compile-time index sequence optimization

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
Recent statement refactoring and compile-time optimization:
- **BaseStatement**: Shared utilities for transaction management, SQL execution patterns, and compile-time field binding
- **Index Sequence Optimization**: Replaced recursive templates with `std::index_sequence` and fold expressions for field binding
- **Statement Separation**: Individual modules for InsertStatement and RemoveStatement with specialized optimizations
- **Batch Operations**: InsertStatement now supports `std::span<const T>` with bulk INSERT using multiple VALUES clauses
- **Performance Optimization**: Smart thresholds for bulk vs individual operations based on SQLite variable limits
- **Code Consolidation**: ~60% reduction in duplicated execution logic through BaseStatement utilities
- **Compile-Time Field Binding**: Pre-computed field metadata and index-based binding eliminates runtime reflection overhead

#### 5. **Compile-Time Index Sequence Optimization**
A major performance optimization using modern C++ compile-time features:

**Key Implementation Details:**
- **Index Sequence Generation**: `std::make_index_sequence<field_count_>` creates compile-time indices for all struct fields
- **Fold Expression Binding**: Replaces recursive template instantiation with C++17 fold expressions for field binding
- **Pre-computed Metadata**: Field information computed once per template instantiation and cached in static constexpr variables
- **Compile-Time Field Access**: Uses `obj.[:member:]` reflection splice operator with compile-time indices

**Technical Benefits:**
- **Reduced Template Depth**: Eliminates recursive template instantiation for field binding
- **Better Assembly Generation**: Fold expressions produce more optimized machine code than recursive templates
- **Faster Compilation**: Reduces template instantiation overhead during compilation
- **Runtime Performance**: Compile-time index computation eliminates runtime parameter tracking

**Implementation Example:**
```cpp
// Index sequence utility in BaseStatement
using field_indices_t = std::make_index_sequence<field_count_>;

// Compile-time field binding with fold expressions
template <typename ConnType, typename Statement, size_t... Is>
auto bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) -> std::expected<void, Error> {
    // Fold expression binds all fields at compile-time indices
    auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, Is + 1) && ...);
    // Error handling omitted for brevity
}
```

**Performance Impact:**
- **6.6x speedup** over sqlite_orm (3.06ms vs 20.25ms for 10,000 operations)
- Maintains Storm's performance advantage while improving code quality
- Enables more complex batch operations without performance penalty

#### 6. **Batch Operations Architecture**
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
- **Index Sequence Optimization**: Uses `std::index_sequence` and fold expressions for compile-time field binding instead of recursive templates
- **Pre-computed Metadata**: Field information cached in static constexpr variables for optimal runtime performance
- **Module Naming**: Uses underscores (`storm_db_sqlite`) not dots due to compiler limitations
- **Circular Dependencies**: Avoided by duplicating `FieldAttr` enum definition
- **Compiler Crashes**: std::mutex in modules causes segfaults in current Clang build
- **SQL Generation**: Runtime std::format (not constexpr yet)
- **Primary Key Access**: Uses reflection splice operator `obj.[:primary_key_:]`
- **Field Binding**: Compile-time index computation eliminates runtime parameter index tracking

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
2. **Implement compile-time field binding**: Use index sequences and fold expressions for optimal field binding performance
3. **Implement smart thresholds**: Consider SQLite variable limits (`SQLITE_MAX_VARIABLE_NUMBER = 999`)
4. **Cache SQL strings**: Use static methods for SQL generation like `get_insert_sql()`
5. **Pre-compute field metadata**: Use `constexpr` and `consteval` functions to compute field information at compile-time
6. **Batch operations**: Support both bulk SQL (IN clauses, multiple VALUES) and individual statements with transactions

### Index Sequence Optimization Implementation
When implementing field binding optimization in new statements:

```cpp
// In BaseStatement-derived class:
static constexpr auto field_count_ = get_field_count();
static constexpr auto all_members_ = get_all_field_members<field_count_>();
using field_indices_t = std::make_index_sequence<field_count_>;

// Bind fields using index sequence
auto bind_all_fields(Statement& stmt, const T& obj) -> std::expected<void, Error> {
    return Base::template bind_all_fields_impl<ConnType, Statement>(
        stmt, obj, typename Base::field_indices_t());
}

// For bulk operations
auto bind_all_objects_bulk(Statement& stmt, const Container& objects) -> std::expected<void, Error> {
    return Base::template bind_all_objects_bulk_impl<ConnType, Statement>(
        stmt, objects, typename Base::field_indices_t());
}
```

**Key Benefits of Index Sequence Approach:**
- Eliminates recursive template instantiation
- Reduces compilation time and memory usage
- Produces better optimized assembly code
- Enables more complex batch operations
- Maintains type safety with compile-time validation

### Adding PostgreSQL Support
1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` to support multiple backends
4. Ensure concepts properly abstract differences