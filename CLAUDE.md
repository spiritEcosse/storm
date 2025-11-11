# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**📚 Full Documentation**: See [docs/README.md](docs/README.md) for comprehensive feature documentation, architecture guides, and development workflows.

## Project Overview

Storm is a modern C++26 ORM library for SQLite using cutting-edge C++26 reflection to automatically map C++ structs to database tables without macros.

**Performance**: Storm achieves **1.5-6x performance advantage** over sqlite_orm, maintaining **70-90% efficiency** compared to raw SQLite.

| Operation | Storm ORM | Raw SQLite | Storm vs sqlite_orm |
|-----------|-----------|------------|---------------------|
| INSERT (single) | 992K/sec | 49M/sec | 2.0x faster |
| INSERT (batch) | 2.7M/sec | - | 6.4x faster |
| SELECT | 13.07M rows/sec | 17.67M rows/sec | 1.51x faster |
| UPDATE | 2M/sec (12M peak) | 1.09M/sec | 6x faster |
| DELETE | 21.6M/sec | 29.4M/sec | 36.6x faster |
| JOIN | 4-6M rows/sec | 5-7.4M rows/sec | 77% avg efficiency |
| **WHERE (detailed)** | | | |
| - int comparison | 8.88M rows/sec | 10.02M rows/sec | 88.6% efficiency |
| - bool comparison | 9.04M rows/sec | 9.74M rows/sec | 92.8% efficiency |
| - string/LIKE pattern | 2.79M rows/sec | 2.78M rows/sec | 100% efficiency |
| - BETWEEN range | 4.91M rows/sec | 4.93M rows/sec | 99.6% efficiency |
| - IN (3 values) | 2.14M rows/sec | 1.12M rows/sec | ~100% efficiency* |
| - IN (10 values) | 3.02M rows/sec | 4.83M rows/sec | 69.5% efficiency |
| - Simple (2 AND) | 7.03M rows/sec | 7.73M rows/sec | 90.9% efficiency |
| - Medium (4 cond) | 1.32M rows/sec | 1.39M rows/sec | 95.6% efficiency |
| - Complex (8+ cond) | 0.73M rows/sec | 0.71M rows/sec | 102% efficiency |

*Note: IN (3 values) shows >100% in some runs due to measurement variance on tiny result sets (high per-query overhead relative to SQLite execution time)

**Key Innovations**: Compile-time SQL generation, 3-level statement caching, thread-local SQL caching, optimized row extraction, abstract base class pattern for type-erased JOINs, pure C++26 reflection for WHERE clauses.

## Critical Safety Rules

**⚠️ IMPORTANT: These rules must NEVER be violated:**

1. **NEVER Delete .git Repository**
   - Do not run `rm -rf .git` or any command that deletes the `.git` directory
   - The `.git` directory contains all project history and must be preserved

2. **NEVER Push Without User Approval**
   - Do not run `git push` unless explicitly requested by the user
   - Always ask for permission before pushing to remote repository
   - When committing changes, wait for user confirmation before pushing
   - Exception: If user explicitly says "commit and push", then both operations are approved

3. **MANDATORY: Benchmark After ANY Code Changes**
   - **After suggesting/implementing ANY improvement, IMMEDIATELY run benchmarks**
   - This applies to ALL changes, even "zero overhead" or "refactoring only" changes
   - **If benchmarks show ANY slowdown (even 1-2%), REVERT IMMEDIATELY**
   - Try alternative approach if available, or keep original code
   - **Never declare success without benchmark confirmation**
   - Remember: Performance > Code Cleanliness for ORMs

   **Why This Matters:**
   - Binary layout changes affect instruction cache unpredictably
   - Even removing dead code can change memory layout
   - Template/lambda changes can affect inlining decisions
   - "Unrelated" code can regress due to code placement
   - Benchmarks are the only source of truth

   **Mandatory Workflow:**
   ```bash
   # 1. Implement change
   # 2. Build release
   cmake --build --preset ninja-release

   # 3. RUN BENCHMARKS (for affected code paths)
   ./build/release/benchmarks/bench_join --size=10000 --all
   ./build/release/benchmarks/bench_where --benchmark_min_time=2s
   ./build/release/benchmarks/bench_storm --mode=select-only --test-size=10000

   # 4. Compare with baseline - if ANY regression:
   git stash  # or git checkout -- <files>

   # 5. Only after confirming zero regression:
   # Proceed with commit
   ```

## Quick Start

### Prerequisites
- Custom Clang with C++26 reflection support (located at `../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+, Ninja build system

### Build Commands

```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release

# Code formatting
cmake --build --preset ninja-debug --target format
```

### Sanitizer Builds

```bash
# Address + Leak sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug

# Thread sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread"
cmake --build --preset ninja-debug
```

## Benchmarking

**⚠️ IMPORTANT: Always use Release builds for accurate performance measurements!**

```bash
# Python benchmark suite (RECOMMENDED - with auto-rebuild)
python3 bench.py --all                   # All microbenchmarks
python3 bench.py --joins                 # JOIN performance
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --compare               # Full Storm vs sqlite_orm vs Raw SQLite

# Direct C++ benchmark execution (manual rebuild required)
cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Individual benchmarks
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_join --size=10000 --storm-join-1
./build/release/benchmarks/bench_sqlite_orm
./build/release/benchmarks/bench_sqlite
```

**Python Benchmark Features**: Auto-rebuild, formatted output, flexible dataset control, cache-aware.

## Basic Usage Examples

## High-Level Architecture

### Module Structure
```
src/
├── storm.cppm                      # Main module with meta functionality
├── db/
│   ├── concept.cppm                # Database concepts
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet ORM interface
    ├── utilities.cppm              # ConstexprString, SQLCache templates
    └── statements/
        ├── base.cppm               # BaseStatement utilities
        ├── insert.cppm             # InsertStatement
        ├── select.cppm             # SelectStatement (with JOIN support)
        ├── distinct.cppm           # DistinctStatement (DISTINCT queries)
        ├── update.cppm             # UpdateStatement
        ├── remove.cppm             # RemoveStatement
        └── join.cppm               # JoinStatement (SQL builder for FK JOINs)
```

### Key Design Decisions

#### 1. **C++26 Reflection-Based ORM**
Uses compile-time reflection (`std::meta`) to automatically:
- Find primary key fields marked with `[[=storm::meta::FieldAttr::primary]]`
- Generate SQL statements from struct definitions
- Bind struct fields to database columns

```cpp
struct Person {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};
```

### CRUD Operations

```cpp
storm::orm::QuerySet<Person> queryset(conn);

// INSERT - returns auto-generated ID
auto result = queryset.insert(Person{0, "Alice", 25});
int64_t id = result.value();

// SELECT
auto result = queryset.select();
for (const auto& person : result.value()) { /* ... */ }

// UPDATE
queryset.update(Person{1, "Alice", 26});

// DELETE
queryset.remove(Person{1, "Alice", 26});
```

See [docs/features/CRUD_OPERATIONS.md](docs/features/CRUD_OPERATIONS.md) for complete CRUD documentation.

### WHERE Clauses

```cpp
using namespace storm::orm::where;

// Single condition (pure C++26 reflection - no macro!)
auto result = queryset.where(field<^^Person::age>() > 30).select();

// Multiple conditions with natural operators
auto result = queryset.where(field<^^Person::age>() > 25 and
                              field<^^Person::age>() < 50).select();

// Special methods
auto result = queryset.where(field<^^Person::name>().like("A%")).select();
auto result = queryset.where(field<^^Person::age>().between(28, 35)).select();

// IN clause - compile-time (high performance, executes immediately, no .select() needed)
auto result = queryset.where(field<^^Person::id>().in(1, 2, 3));
```

See [docs/features/WHERE_CLAUSES.md](docs/features/WHERE_CLAUSES.md) for complete WHERE documentation.

### JOIN Operations

```cpp
// Single FK JOIN
auto result = message_qs.join<&Message::sender>().select();

// Multi-FK JOIN
auto result = message_qs.join<&Message::sender, &Message::receiver>().select();

// JOIN with WHERE
auto result = message_qs.join<&Message::sender>()
                        .where(field<^^User::level>() > 5)
                        .select();
```

See [docs/features/JOIN_OPERATIONS.md](docs/features/JOIN_OPERATIONS.md) for complete JOIN documentation.

### Batch Operations

```cpp
// Batch INSERT - returns all generated IDs
std::vector<Person> people = {{0, "Alice", 25}, {0, "Bob", 30}};
auto result = queryset.insert(std::span<const Person>(people));
const auto& ids = result.value();

// Batch UPDATE/DELETE
queryset.update(std::span<const Person>(people));
queryset.remove(std::span<const Person>(people));
```

See [docs/features/BATCH_OPERATIONS.md](docs/features/BATCH_OPERATIONS.md) for complete batch operation documentation.

## Documentation Index

### Features
- **[CRUD Operations](docs/features/CRUD_OPERATIONS.md)** - INSERT, UPDATE, DELETE with auto-generated IDs
- **[SELECT Queries](docs/features/SELECT_QUERIES.md)** - Optimized row extraction and statement caching
- **[WHERE Clauses](docs/features/WHERE_CLAUSES.md)** - Type-safe filtering with pure C++26 reflection
- **[JOIN Operations](docs/features/JOIN_OPERATIONS.md)** - Single and multi-FK JOINs with type erasure
- **[Batch Operations](docs/features/BATCH_OPERATIONS.md)** - Bulk INSERT/UPDATE/DELETE with smart thresholds

### Architecture
- **[Overview](docs/architecture/OVERVIEW.md)** - High-level architecture and design principles
- **[C++26 Reflection](docs/architecture/REFLECTION.md)** - How Storm uses std::meta for ORM mapping
- **[Statement Caching](docs/architecture/STATEMENT_CACHING.md)** - 3-level caching achieving near-raw SQLite performance
- **[SQL Generation](docs/architecture/SQL_GENERATION.md)** - Compile-time SQL generation with ConstexprString
- **[Module System](docs/architecture/MODULE_SYSTEM.md)** - C++26 module structure and dependencies

### Development
- **[Compiler Attributes](docs/development/COMPILER_ATTRIBUTES.md)** - Guide for using hot, flatten, and always_inline attributes
- **[Compiler Issues](docs/development/COMPILER_ISSUES.md)** - Known workarounds for clang-p2996
- **[Performance Testing](docs/development/PERFORMANCE_TESTING.md)** - Benchmarking guidelines and workflow
- **[Adding Features](docs/development/ADDING_FEATURES.md)** - How to add new database operations

## Common Development Tasks

### Running Tests

```bash
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure
```

**Results: Templates Made It WORSE**
- **Function pointer approach (current)**: 6.9M rows/sec (70% of raw) ✅ **BEST**
- **Template approach (attempted)**: 4.9M rows/sec (49% of raw) ❌ **28% SLOWER**

**Why Templates Failed:**
1. **Code bloat**: Template instantiation for each JOIN configuration created more code → worse instruction cache locality
2. **Compiler optimization surprise**: Modern compilers (Clang 21) optimize indirect function calls better than expected with profile-guided optimization
3. **Inlining limits**: Even with templates, deep call chains with complex reflection operations hit compiler inlining budget limits
4. **Register pressure**: Fully inlined template code increased register spilling in the hot loop

**Real Performance Bottlenecks (NOT Function Pointers!):**
1. **String allocations**: Each `std::string` field requires heap allocation (~30-40% of runtime)
2. **Object construction**: Creating and populating complex objects with multiple fields
3. **Vector management**: Resizing, copying, moving objects in result vectors
4. **Multi-column extraction**: 5-8 columns for JOIN vs 2-3 for simple SELECT
5. **Type dispatch overhead**: Runtime `if constexpr` type checks for each field

**Measured Overhead Breakdown (profiling data):**
- SQL execution: ~20% (unavoidable, same as raw SQLite)
- Row stepping: ~5% (minimal overhead)
- Column extraction: ~35% (type checks + conversion)
- String allocation: ~30% (heap allocations for TEXT fields)
- Object construction: ~10% (calling constructors, field assignment)

**Recommendations for Future Optimization:**

Current 50-70% of raw SQLite performance is **respectable for a full ORM** with reflection-based mapping. Further gains require architectural changes:

1. **String handling** (biggest potential win):
   - Use `std::string_view` for read-only operations (eliminate allocation)
   - Implement move semantics throughout extraction chain
   - Consider string interning for repeated values
   - Arena allocator for temporary strings

2. **Memory management**:
   - Object pooling to reuse allocated objects
   - Custom allocator optimized for ORM access patterns
   - Better size estimation (currently pre-allocates 10K, may overshoot)

3. **Column extraction optimization**:
   - Batch extraction by type (extract all ints, then all strings)
   - SIMD for type checking and conversion
   - Specialized fast paths for common type combinations

4. **Caching improvements**:
   - Cache field offset calculations
   - Reuse extraction buffers across queries
   - Pre-build type dispatch tables at compile-time

**Key Lesson**: Don't assume eliminating indirect calls will improve performance. Profile first, optimize second. The current function-pointer based implementation strikes a good balance between flexibility and performance.

#### 14. **DISTINCT Query Support (Single & Multi-Field)**

Storm ORM supports `DISTINCT` queries on one or more fields with compile-time type safety and near-raw SQLite performance.

**Architecture:**
```cpp
// DistinctStatement (in src/orm/statements/distinct.cppm)
//    - Inherits from BaseStatement<T>
//    - Executes SELECT DISTINCT with field extraction
//    - Variadic template requiring 1+ fields (enforced via requires clause)
//    - Fields specified at construction as template parameters
//    - Always generates DISTINCT queries (for aggregates, use separate AggregateStatement)
template <typename T, ConnType, auto... FieldPtrs>
    requires (sizeof...(FieldPtrs) > 0)
class DistinctStatement : private BaseStatement<T> {
    static constexpr size_t NumFields = sizeof...(FieldPtrs);
    static constexpr auto member_infos_ = std::array{get_member_info<FieldPtrs>()...};

    // Return type: single field → std::vector<FieldType>
    //              multiple fields → std::vector<std::tuple<Type1, Type2, ...>>
    using ResultType = std::conditional_t<
        NumFields == 1,
        std::vector<std::tuple_element_t<0, FieldTypesTuple>>,
        std::vector<FieldTypesTuple>
    >;

    auto execute() -> std::expected<ResultType, Error>;
    auto select() -> std::expected<ResultType, Error>;  // Alias for execute()
};
```

**Usage:**
```cpp
QuerySet<Person> qs;

// Single field DISTINCT (backward compatible)
auto names = qs.distinct<&Person::name>().select();
// Returns: std::vector<std::string>

// Multiple field DISTINCT
auto pairs = qs.distinct<&Person::name, &Person::age>().select();
// Returns: std::vector<std::tuple<std::string, int>>

// Default to primary key
auto ids = qs.distinct().select();
// Returns: std::vector<int>
```

**SQL Generation:**
- Single field: `SELECT DISTINCT name FROM Person`
- Multiple fields: `SELECT DISTINCT name, age FROM Person`
- Compile-time field list construction using fold expressions with index sequences
- FK fields automatically use column name (e.g., `sender_id` instead of `sender`)

**Performance Results (10,000 rows, 100 iterations, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency |
|-----------|-----------|------------|------------|
| DISTINCT name (string) | 140,507 rows/sec | 147,083 rows/sec | 95% |
| DISTINCT age (int) | 148,841 rows/sec | 141,179 rows/sec | 105% |
| DISTINCT id/PK (int) | 35.8M rows/sec | 36.2M rows/sec | 99% |
| DISTINCT (name, age) | 132,336 rows/sec | 131,125 rows/sec | 101% |

**Average Efficiency: ~100%** - Storm ORM achieves parity with raw SQLite for DISTINCT operations!

**Key Features:**
- ✅ **Type Safety**: Return type automatically deduced at compile-time
- ✅ **Zero Overhead**: Compile-time SQL generation, no runtime string building
- ✅ **Multiple Fields**: Full support for DISTINCT on 1+ fields (SQLite supports 2000 columns)
- ✅ **Backward Compatible**: Single-field API unchanged from original implementation

**Implementation Highlights:**
1. **Simplified Architecture**: Direct statement executor (no wrapper layer), QuerySet handles convenience
2. **Architectural Consistency**: Follows same pattern as `InsertStatement`, `UpdateStatement`, etc. in `src/orm/statements/`
3. **Requires Clause**: Template constraint `requires (sizeof...(FieldPtrs) > 0)` enforces at least one field
4. **Compile-Time Field List**: Uses fold expressions with lambda templates to build comma-separated field list
5. **Type Deduction**: Automatically determines return type based on number and types of fields
6. **Index Sequences**: Leverages `std::index_sequence` for compile-time iteration
7. **Tuple Extraction**: Multi-field results extracted into `std::tuple` with perfect type matching
8. **Statement Caching**: Prepared statement cached for repeated DISTINCT queries on same fields

**Known Limitations:**
- Type-based field matching can be ambiguous when multiple fields have the same type (e.g., `&Person::id` and `&Person::age` are both `int`)
- Workaround: Use fields with unique types or access via different field orderings
- Aggregate functions (COUNT, SUM, AVG) require separate implementation (different return types)

**Testing:**
- 29 comprehensive unit tests in `tests/test_distinct.cpp` covering:
  - Single-field and multi-field DISTINCT operations (17 tests)
  - Edge cases: duplicate fields, type safety, cross-struct prevention (6 tests)
  - JOIN exploration: limitations and workarounds (6 tests)
- Tests include: empty table, single row, large datasets (10K rows), duplicate handling, type verification, FK field behavior
- All tests pass with 100% success rate

### Cross-Module Dependencies
```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
├── storm_orm_statements_base
├── storm_orm_utilities
├── storm_orm_statements_{insert,update,remove,select,distinct,join}
└── storm_orm_queryset
```

### Performance Testing

```bash
# Recommended workflow
python3 bench.py --compare  # Compare all implementations

# Check for regressions before commit
python3 bench.py --all
```

See [docs/development/PERFORMANCE_TESTING.md](docs/development/PERFORMANCE_TESTING.md) for detailed benchmarking guidelines.

### Adding New Features

1. Implement in `src/orm/statements/`
2. Add tests in `tests/test_*.cpp`
3. Create benchmark in `benchmarks/bench_*.cpp`
4. Run benchmark and measure efficiency (target: ≥70% of raw SQLite)
5. Update documentation
6. Commit with performance metrics

See [docs/development/ADDING_FEATURES.md](docs/development/ADDING_FEATURES.md) for detailed implementation guide.

### Git Workflow

```bash
# Update feature branch
git fetch origin && git merge origin/develop

# Test before pushing
cmake --build --preset ninja-debug && ctest --test-dir build/debug
git push
```

**Best practices**: Clean working directory, short-lived branches, thorough testing, descriptive commit messages.

## Module Structure

Storm uses C++26 modules organized into:
- **storm.cppm** - Main module exporting all public APIs
- **db/** - Database abstraction (concepts + SQLite implementation)
- **orm/** - ORM layer (QuerySet, WHERE expressions, utilities, statements)

See **[docs/architecture/MODULE_SYSTEM.md](docs/architecture/MODULE_SYSTEM.md)** for the complete module structure, dependency graph, and circular dependency avoidance strategies.

## Compiler Requirements

Experimental Clang fork with C++26 reflection:
- Located at `../clang-p2996/`
- Custom libcxx with reflection support
- Module scanning with `clang-scan-deps`
- Reflection flags: `-freflection -fannotation-attributes`

### Known Compiler Issues

1. **Module Cache Corruption** - Simply run build command again if it fails
2. **std::mutex Segfaults** - Avoid mutex in module code
3. **std::inplace_vector Not Available** - Use `std::array` instead
4. **C Headers Cannot Be Imported** - Include in module preamble with `#include`
5. **Most Vexing Parse** - Use braced initialization
6. **Missing Statement Methods** - Use `stmt->handle()` for raw SQLite API

See [docs/development/COMPILER_ISSUES.md](docs/development/COMPILER_ISSUES.md) for detailed workarounds.

## Performance-Driven Development

Storm ORM prioritizes performance over code elegance when there's a conflict:

### Key Principles

1. **Benchmark Before Accepting** - Every new feature requires performance testing
2. **Target**: ≥70% of raw SQLite performance
3. **DRY/KISS Principle** - Follow unless it costs >10% performance
4. **Document Tradeoffs** - Explain when breaking DRY/KISS for performance

### Performance Testing Workflow

```bash
# 1. Implement feature
# 2. Create benchmark
touch benchmarks/bench_<feature>.cpp

# 3. Run benchmark
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_<feature> --size=10000

# 4. Calculate efficiency
# Efficiency = (Storm_ORM_perf / Raw_SQLite_perf) * 100%

# 5. Document results in relevant docs
# 6. Commit with performance metrics
git commit -m "feat: add FEATURE (XX% of raw SQLite)"
```

See [docs/development/PERFORMANCE_TESTING.md](docs/development/PERFORMANCE_TESTING.md) for comprehensive guidelines.

## Important Implementation Notes

- **No REFL-CPP**: Project uses native C++26 reflection
- **Index Sequence Optimization**: Uses `std::index_sequence` and fold expressions
- **Pre-computed Metadata**: Field information cached in static constexpr variables
- **Module Naming**: Uses underscores (`storm_db_sqlite`) due to compiler limitations
- **Circular Dependencies**: Avoided by duplicating `FieldAttr` enum
- **std::function Avoided**: Use abstract base classes instead (see JOIN architecture)
- **Primary Key Access**: Uses reflection splice operator `obj.[:primary_key_:]`
- **WHERE Implementation**: Pure C++26 reflection using `field<^^T::member>()` - no macro needed
- **String Concatenation**: Use `operator+=` for string building - benchmarked 11.8% faster than `.append()` in hot paths (WHERE clause SQL generation). While both compile to similar code, `operator+=` produces better instruction layout in practice.
- **Compiler Attributes**: Use `__attribute__((hot)) __attribute__((flatten))` on main execution paths, `__attribute__((always_inline))` on small helpers. See [docs/development/COMPILER_ATTRIBUTES.md](docs/development/COMPILER_ATTRIBUTES.md) for complete rules and when to use each attribute.

## Thread Safety

1. **SQLite Level**: Thread-safe with `SQLITE_OPEN_FULLMUTEX`
2. **Connection Management**: NOT thread-safe - requires external synchronization
3. **SQL Caching**: Thread-local storage eliminates synchronization overhead
4. **Recommended**: Per-thread connections or external mutex

## Testing Strategy

- **GoogleTest** with C++26 module support
- Tests in `tests/` directory, in-memory database (`:memory:`)
- Comprehensive sanitizer support
- ID validation for INSERT operations
- SELECT caching verification
- JOIN object population verification
- FK field testing (INSERT/UPDATE/DELETE with FKs)

## Help and Support

For questions or issues:
- Check [docs/README.md](docs/README.md) for comprehensive documentation
- See [docs/development/COMPILER_ISSUES.md](docs/development/COMPILER_ISSUES.md) for known workarounds
- Browse [docs/features/](docs/features/) for feature-specific usage
- Consult [docs/architecture/](docs/architecture/) for implementation details
### Adding PostgreSQL Support
1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` for multiple backends
4. Ensure concepts properly abstract differences

# Core Concepts of QuerySet

This document outlines the fundamental principles and behaviors of the QuerySet system, designed for building and executing SQLite queries in a fluent, type-safe manner using C++. It emphasizes immutability, chaining, and modular modes for handling projections, aggregations, and more.

## Immutability and Chaining

- **Core Principle**: All non-terminal methods (e.g., `where()`, `join()`) return a **new query object** by copying or moving the internal state. This ensures thread-safety and allows for fluent, immutable chaining without modifying the original query.
- **Fluent API**: Enables building complex queries in a readable, chainable style:
  ```cpp
  auto results = QuerySet<Model>().where(...).order_by<...>().select();
  ```
- **Terminal Methods**: Methods like `select()` or standalone aggregates execute the query immediately and return results (e.g., vectors, tuples, or scalar values). No further chaining is possible after a terminal call.

## Clause Ordering

- **Flexible Chaining**: Users can chain methods in any order for convenience (e.g., `where()` before or after `join()`).
- **Internal Enforcement**: During SQL generation, clauses are reordered to match valid SQLite syntax:
   - `SELECT ... FROM ... JOIN ... WHERE ... GROUP BY ... HAVING ... ORDER BY ... LIMIT/OFFSET`
- **Validation**: Invalid combinations (e.g., selecting non-grouped columns alongside aggregates without `group_by<...>()`) trigger compile-time errors to prevent incorrect SQL.

## Projection and Result Types

- **Transformers**: There are `distinct<...>()` or `values<...>()`.
- **Without Transformers**: `select()` returns `std::vector<Model>`, representing full model objects fetched from the database.
- **With Transformers**: `select()` returns `std::vector<std::tuple<...>>` or `std::vector<type>`, where the types match the projected or aggregated columns.
- **Standalone Aggregates**: To return a single aggregate value, simply use `qs.min<...>()` and similar methods directly, which return scalar values (e.g., `int` for count, `double` for average).

## Available Methods in All Modes

These methods are accessible regardless of the current mode and build query state without altering the mode:

- `join<OtherModel>()`: Adds a JOIN clause to include related models.
- `where(Condition)`: Filters rows based on the provided condition.
- `order_by<Cols...>()`: Sorts results by the specified columns.
- `limit(int)`: Restricts the number of results returned.
- `offset(int)`: Skips a specified number of results.
- `group_by<Cols...>()`: Groups results by the given columns (especially useful in Aggregate Mode).
- `having(Condition)`: Filters groups based on aggregate conditions (references aggregates in Aggregate Mode).

## Modes and Transformers

QuerySet operates in different **modes** to handle various query types. Modes are entered via **transformers**, which return new objects while preserving prior chain state (e.g., existing `where()` or `join()` clauses).

### Default Mode (Object Mode)

- **Starting Point**: Begins with `QuerySet<Model>`, representing queries that fetch full rows without custom projections.
- **Behavior**: Focuses on retrieving complete model instances.
- **Terminals**: `select()` returns model vectors; aggregates can be used to shift to Aggregate Mode.
- **Transformers**: Use `distinct<Cols...>()` or `values<Cols...>()` to enter Tuple Mode, or aggregate methods to enter Aggregate Mode.

### Tuple Mode

- **Entry**: Via `distinct<Cols...>()` or `values<Cols...>()`.
   - Returns specialized objects like `DistinctQuerySet<Model, Cols...>` or `ValuesQuerySet<Model, Cols...>`.
- **Mirrors SQLite**:
   - `DISTINCT` applies to the entire result set after projection.
   - `VALUES` specifies explicit column projections.
- **Chaining Behavior**: If both `distinct<Cols...>()` and `values<Cols...>()` are used, the last one dictates the SQL clause, but the mode remains Tuple.
- **Column Specification**: `<Cols...>` can be omitted to infer all columns (e.g., `distinct<>()` or `values<>()`).
- **Additional Features**: Supports DISTINCT within aggregates (e.g., `distinct<true>()` for unique values).
- **Available Methods**: All base methods, plus aggregates (to enter Aggregate Mode), and terminals.
- **Results**:
   - `select()`: Yields tuples of distinct or projected values.
   - Aggregates: Applied over the distinct/projected rows.

### Aggregate Mode

- **Entry**: Via aggregate transformers like `min<Col>()`, `max<Col>()`, `sum<Col>()`, `avg<Col>()`, or `count<Col|*>()`.
   - Returns objects like `AggregateQuerySet<Model, Aggs...>`, accumulating aggregates in the projection.
- **Accumulation**: Chain multiple aggregates (e.g., `min<Col1>().max<Col2>()` generates `MIN(col1), MAX(col2)`).
- **Compatibility with Tuple Mode**: Aggregates can follow `distinct<Cols...>()` or `values<Cols...>()`, applying to projected/distinct rows.
- **SQLite Features**:
   - DISTINCT inside aggregates (e.g., `sum<Col, true>()` for `SUM(DISTINCT col)`).
   - FILTER clauses via optional parameters.
   - Ordering in functions like `GROUP_CONCAT`.
- **Available Methods**: All base methods, additional aggregates, transformers (e.g., `distinct<Cols...>()`, `values<Cols...>()` which may adjust projection), and terminals.
   - `group_by<Cols...>()` and `having()` are particularly useful here, as `having()` can reference aggregates.
- **Grouping Behavior**:
   - Without `group_by<Cols...>()`: Aggregates over the entire dataset (single result row).
   - With `group_by<Cols...>()`: One result per group.
- **Results**:
   - `select()`: Tuples of aggregate values (one or more rows).
   - Standalone terminals: Single scalar values if no grouping or accumulation.

## Mode Precedence and Combinations

- **Interleaving Transformers**: Modes can be combined by chaining transformers (e.g., `values<Cols...>().sum<Col>()` enters Tuple then Aggregate Mode).
- **Final Mode Determination**:
   - **Tuple Mode**: If `distinct<Cols...>()` or `values<Cols...>()` was ever called.
   - **Aggregate Mode**: If any aggregates are present.
   - Prior state always transfers to new objects.
- **Projection Overrides**: If a transformer like `values<Cols...>()` follows aggregates, aggregates are preserved only if compatible; otherwise, they may need re-adding.
- **Flexibility**: Ensures seamless transitions while maintaining query integrity and type safety.

