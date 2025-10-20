# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Last Updated**: Storm ORM features **complete CRUD operations + JOIN support** maintaining consistent 1.5-6x performance advantage over sqlite_orm:
- **INSERT**: 992K/sec single, 2.7M/sec batch (2.0x faster than sqlite_orm)
- **SELECT**: 13.07M rows/sec (74% of raw SQLite, 1.51x faster than sqlite_orm)
- **UPDATE**: 2M/sec sustained, 12M peak (6x faster than sqlite_orm)
- **DELETE**: 21.6M/sec single, 3.9M/sec batch (73% of raw SQLite, 36x faster than sqlite_orm)
- **JOIN**: 4-6M rows/sec (77% average efficiency vs raw SQLite for INNER/LEFT/RIGHT JOINs)

Key innovations: compile-time SQL generation, statement-level caching, thread-local SQL caching, optimized row extraction (resize() pre-allocation 1.7x faster, direct string construction 2.2x faster), fully inlined field binding, abstract base class pattern for type-erased JOIN operations.

## Project Overview

Storm is a modern C++26 ORM library for SQLite using cutting-edge C++26 reflection to automatically map C++ structs to database tables without macros.

## Critical Safety Rules

**⚠️ IMPORTANT: These rules must NEVER be violated:**

1. **NEVER Delete .git Repository**
   - Do not run `rm -rf .git` or any command that deletes the `.git` directory
   - Do not run `git clean` with flags that could remove the repository
   - The `.git` directory contains all project history and must be preserved

2. **NEVER Push Without User Approval**
   - Do not run `git push` unless explicitly requested by the user
   - Always ask for permission before pushing to remote repository
   - When committing changes, wait for user confirmation before pushing
   - Exception: If user explicitly says "commit and push", then both operations are approved

3. **NEVER Use git reset Without User Approval**
   - Do not run `git reset --hard` or any destructive git reset command
   - Do not run `git checkout` to revert files without asking
   - Do not run any command that discards uncommitted changes without user permission
   - **ALWAYS ask the user first** if you want to reset or revert any files or commits
   - **Reason**: These commands can permanently delete work in progress
   - **Historical incident**: Accidentally used `git reset --hard HEAD` which removed an entire LIMIT/OFFSET feature implementation requiring complete re-implementation from scratch

## Build Commands

### Prerequisites
- Custom Clang with C++26 reflection support (located at `../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+, Ninja build system

### Standard Development Build
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
cmake --build --preset ninja-debug --target format-check
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

### Benchmarking

**⚠️ IMPORTANT: Always use Release builds for accurate performance measurements!**

Debug builds are 2-3x slower than Release builds. The performance figures below are from Release builds.

```bash
# Python-based benchmark suite (RECOMMENDED - with auto-rebuild)
# Default: 1000 messages, 100 iterations
python3 bench.py --joins                # JOIN performance analysis
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --all                  # All microbenchmarks
python3 bench.py --compare              # Comprehensive Storm vs sqlite_orm vs Raw SQLite

# Direct benchmark execution (manual rebuild required)
cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# JOIN benchmarks with selective flags (defaults: sizes={100,1000,10000}, iterations=100)
./build/release/benchmarks/bench_join                                        # Run all benchmarks with defaults
./build/release/benchmarks/bench_join --size=10000 --iterations=100          # All benchmarks (custom size)
./build/release/benchmarks/bench_join --storm-join-1 --size=10000            # Storm single FK only
./build/release/benchmarks/bench_join --storm-join-multi --size=10000        # Storm multi FK only
./build/release/benchmarks/bench_join --raw-join-1 --size=10000              # Raw SQLite single FK
./build/release/benchmarks/bench_join --raw-join-multi --size=10000          # Raw SQLite multi FK
./build/release/benchmarks/bench_join --storm-join-1 --raw-join-1 --size=10000  # Compare single FK
./build/release/benchmarks/bench_join --help                                 # Show all options

# Other benchmarks
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_sqlite_orm
./build/release/benchmarks/bench_sqlite

# Legacy shell scripts
./performance_comparison_release.sh    # Builds and runs all benchmarks
./sql_generation_analysis.sh           # SQL generation analysis
```

**Python Benchmark Features:**
- ✅ **Auto-rebuild**: Detects source changes and rebuilds automatically
- ✅ **Formatted output**: Color-coded tables with efficiency percentages
- ✅ **Flexible**: Control dataset size and iteration count
- ✅ **Fast**: Skips rebuild when nothing changed

**Performance Results (10,000 operations, Release builds):**

| Operation | Storm ORM | Raw SQLite | sqlite_orm | Storm vs sqlite_orm |
|-----------|-----------|------------|------------|---------------------|
| INSERT (single) | 992K/sec | 49M/sec | 492K/sec | 2.0x faster |
| INSERT (batch) | 2.7M/sec | - | 422K/sec | 6.4x faster |
| SELECT (all rows) | 13.07M/sec | 17.67M/sec | 8.66M/sec | 1.51x faster |
| UPDATE (single) | 2M/sec (12M peak) | 1.09M/sec | 333K/sec | 6x faster |
| DELETE (single) | 21.6M/sec | 29.4M/sec | 589K/sec | 36.6x faster |
| DELETE (batch) | 3.9M/sec | - | - | - |

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

#### 2. **Supported Field Types**
Storm ORM supports all standard SQLite types through compile-time type dispatch in `BaseStatement::bind_value_by_type()` (src/orm/statements/base.cppm) and `SelectStatement::extract_column_inline_fast()` (src/orm/statements/select.cppm):

**Integer types:**
- `int` - Bound using `bind_int()`
- `int64_t`, `long`, `long long` - Bound using `bind_int64()`
- `uint64_t`, `unsigned long`, `unsigned long long` - Bound using `bind_int64()` (cast)
- `short`, `unsigned short`, `unsigned int` - Bound using `bind_int()` (cast)

**Floating point types:**
- `double` - Bound using `bind_double()`
- `float` - Bound using `bind_double()` (cast)

**Boolean type:**
- `bool` - Stored as INTEGER (0/1), bound using `bind_int()`

**String types:**
- `std::string` - Bound using `bind_text()`
- `const char*` - Bound using `bind_text()`
- `std::string_view` - Bound using `bind_text()`
- Any type convertible to `std::string_view`

**Optional types (NULL support):**
- `std::optional<T>` for any supported type T - Binds NULL when std::nullopt, otherwise recursively binds the contained value
- Examples: `std::optional<int>`, `std::optional<std::string>`, etc.

**BLOB types (binary data):**
- `std::vector<uint8_t>` - Bound using `bind_blob()`
- `std::vector<unsigned char>` - Bound using `bind_blob()`

**SQLite Type Mapping:**
| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| int, short, unsigned | INTEGER | bind_int() | extract_int() |
| int64_t, long, uint64_t | INTEGER | bind_int64() | extract_int64() |
| float, double | REAL | bind_double() | extract_double() / extract_float() |
| bool | INTEGER | bind_int() | extract_bool() |
| std::string, const char* | TEXT | bind_text() | extract_text_ptr() |
| std::optional<T> | NULL / T's type | bind_null() / recursive | is_null() check |
| std::vector<uint8_t> | BLOB | bind_blob() | extract_blob() |

The binding uses compile-time `if constexpr` type dispatch to select the appropriate SQLite binding function with zero runtime overhead.

#### 3. **Concept-Based Database Abstraction**
- `DatabaseConnection` concept defines interface for any database
- `DatabaseStatement` concept for prepared statements
- SQLite implementation satisfies these concepts
- Allows future PostgreSQL/MySQL support without changing ORM code

#### 4. **Connection Management**
- Default static connection for simple use cases
- Explicit connection passing for multi-database scenarios
- **Thread Safety**: SQLite opened with `SQLITE_OPEN_FULLMUTEX`
- **WARNING**: Connection management layer NOT thread-safe due to compiler limitations with std::mutex in C++26 modules

#### 5. **Statement Architecture & Optimization**
- **BaseStatement**: Shared utilities for transaction management, SQL execution patterns, compile-time field binding
- **Index Sequence Optimization**: `std::index_sequence` and fold expressions replace recursive templates
- **Statement Separation**: Individual modules with specialized optimizations
- **Compile-Time SQL Generation**: Pre-computed SQL using ConstexprString and consteval functions
- **Performance Optimization**: Smart thresholds for bulk vs individual operations (SQLite variable limit = 999)
- **Thread-Local SQL Caching**: 8-entry cache for bulk INSERT SQL strings
- **Statement-Level Caching**: RemoveStatement, SelectStatement, UpdateStatement use statement caching pattern

#### 6. **Compile-Time Index Sequence Optimization**
Major performance optimization using modern C++ compile-time features:

**Key Implementation:**
- `std::make_index_sequence<field_count_>` creates compile-time indices
- Fold expressions replace recursive template instantiation
- Pre-computed metadata cached in static constexpr variables
- Uses `obj.[:member:]` reflection splice operator

```cpp
using field_indices_t = std::make_index_sequence<field_count_>;

template <typename ConnType, typename Statement, size_t... Is>
auto bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) {
    auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, Is + 1) && ...);
}
```

**Benefits**: Reduced template depth, better assembly, faster compilation, zero runtime overhead.

#### 7. **Thread-Local SQL Caching**
Runtime optimization for bulk INSERT performance:

```cpp
struct BulkSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0; // Round-robin
};
thread_local BulkSQLCache bulk_sql_cache;
```

**Performance**: 94% improvement (0.253µs → 0.016µs) for cached sizes, thread-safe, zero synchronization overhead.

#### 8. **Compile-Time SQL Generation with ConstexprString**
Moves SQL generation from runtime to compile-time:

```cpp
static consteval auto build_insert_sql_array() {
    ConstexprString<sql_size> result;
    result.append("INSERT INTO ");
    result.append(Base::table_name_);
    result.append(" (").append(field_names_).append(") VALUES (");
    result.append(placeholders_).append(")");
    return result;
}
```

**Benefits**: Zero runtime SQL generation, exact memory allocation, compile-time validation, cache-friendly.

#### 9. **Batch Operations Architecture**
Two optimized batch operation strategies:

**InsertStatement**:
- Returns `std::expected<std::vector<int64_t>, Error>` with generated IDs
- Bulk INSERT with multiple VALUES for ≤50 objects
- Individual statements with transactions for >50 objects
- Thread-local SQL caching with compile-time prefix optimization

**RemoveStatement**:
- Bulk DELETE with IN clause: `DELETE FROM table WHERE id IN (?,?,?)`
- Same smart thresholds as InsertStatement

**SelectStatement**:
- `execute_optimized()` returns all rows
- Statement-level caching for repeated SELECT calls
- **Unified resize() pre-allocation** (simplified from dual-path implementation):
  - Single code path using resize() for all LIMIT sizes (50% fewer lines)
  - Smart initial sizing: min(LIMIT, 10000) or 1000 for no LIMIT
  - Exponential growth overflow handling for result sets exceeding initial size
  - Performance: 90-2000% of raw SQLite efficiency (avg 98.24%)
  - Best for small LIMITs (1-100 rows): 106-2000% efficiency
  - Excellent for large LIMITs (1000-5000 rows): 90-95% efficiency
- Optimized string extraction using `sqlite3_column_bytes()` (avoids strlen)
- Move semantics for optional field assignments (avoids copy)
- Inline column extraction with compiler hints (`__attribute__((hot/flatten/always_inline))`)

#### 10. **Auto-Generated ID Support**
Returns generated IDs from insert operations:
- Single insert: `std::expected<int64_t, Error>`
- Batch insert: `std::expected<std::vector<int64_t>, Error>`
- Uses SQLite's `AUTOINCREMENT` and `sqlite3_last_insert_rowid()`
- For bulk INSERT, calculates sequential IDs from last ID

**Table creation requires AUTOINCREMENT:**
```cpp
conn.execute("CREATE TABLE Person ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL, age INTEGER NOT NULL)");
```

#### 11. **Statement-Level Caching Pattern**
Unified caching pattern across UPDATE/DELETE/SELECT operations achieves near-raw SQLite performance:

**Architecture (applies to RemoveStatement, UpdateStatement, SelectStatement):**
```cpp
// QuerySet maintains cached statement instance
template <class T> class QuerySet {
    mutable std::unique_ptr<XStatement<T, ConnType>> x_stmt_;

    auto get_x_statement() const -> XStatement<T, ConnType>& {
        if (!x_stmt_) {
            x_stmt_ = std::make_unique<XStatement<T, ConnType>>(conn_);
        }
        return *x_stmt_;
    }
};

// Statement handles caching and execution
template <typename T> class XStatement {
    mutable Statement* cached_stmt_ = nullptr;

    auto execute_single_optimized(const T& obj) {
        if (!cached_stmt_) {
            cached_stmt_ = *conn_.prepare_cached(get_sql());
        }
        // Inline bind with compile-time type dispatch
        cached_stmt_->execute();
        cached_stmt_->reset();
    }
};
```

**3-Level Caching Architecture:**
- **Level 1**: QuerySet caches Statement instance (`std::unique_ptr`)
- **Level 2**: Statement caches prepared statement (`Statement* cached_stmt_`)
- **Level 3**: Connection caches via `prepare_cached()`

**Performance Impact Examples:**
- **DELETE**: 947K → 21.6M ops/sec (22.8x speedup, 73% of raw SQLite)
- **UPDATE**: 100x improvement for repeated operations (12M peak)
- **SELECT**: 13.07M rows/sec (74% of raw SQLite)

**Statement-Specific Details:**
- **UpdateStatement**: Updates all non-PK fields, SQL: `UPDATE table SET field1=?, field2=? WHERE id=?`
- **RemoveStatement**: Deletes by primary key, eliminates hash map lookups
- **SelectStatement**: Optimized row extraction with resize() pre-allocation, direct string construction, `__attribute__((hot/flatten/always_inline))` hints

**Why InsertStatement is NOT cached:**
- INSERT operations are typically one-time per object (not repeated)
- Already uses connection-level caching via `prepare_cached()`
- Thread-local SQL caching provides sufficient optimization
- Current performance (992K/sec single, 2.7M/sec batch) exceeds sqlite_orm by 2-6x

#### 12. **JOIN Architecture (Type-Erased SQL Builder Pattern)**
Enables single and multi-FK JOIN operations without `std::function` (which causes linker issues with custom libc++):

**Architecture:**
```cpp
// Abstract base for type erasure
class IJoinStatement {
    virtual std::string to_sql() const = 0;
    virtual std::string build_qualified_select_fields() const = 0;
    virtual void extract_row(void* stmt, void* obj) const = 0;
};

// Unified variadic template (single + multi FK)
template <typename T, ConnType, auto... FKFieldPtrs>
class JoinStatement : public IJoinStatement {
    // Pure SQL builder - no execute(), no caching
    // Generates: " INNER JOIN table t2 ON t2.id = t1.fk_id"
};
```

**QuerySet Storage:**
```cpp
template <class T> class QuerySet {
    mutable std::unique_ptr<IJoinStatement> join_stmt_;  // Type-erased storage

    template <auto... FKFieldPtrs>
    auto&& join(this auto&& self) {
        self.join_stmt_ = std::make_unique<JoinStatement<T, ConnType, FKFieldPtrs...>>();
        return self;
    }

    auto select() {
        if (join_stmt_) {
            return get_select_statement().execute_optimized(join_stmt_.get());
        }
        return get_select_statement().execute_optimized();
    }
};
```

**SelectStatement Integration:**
```cpp
template <typename JoinStmt = void>
auto execute_optimized(JoinStmt* join_stmt = nullptr) {
    if constexpr (!std::is_void_v<JoinStmt>) {
        // Build: SELECT t1.*, t2.* FROM table t1 + join_stmt->to_sql()
        // Extract: join_stmt->extract_row(stmt, obj)
        // Separate statement cache for JOIN queries
    } else {
        // Simple SELECT without JOIN
    }
}
```

**Usage:**
```cpp
// Single FK - populates sender fully
auto result = message_qs.join<&Message::sender>().select();

// Multi FK - populates both sender and receiver
auto result = message_qs.join<&Message::sender, &Message::receiver>().select();
```

**Key Benefits:**
- ✅ No `std::function` - avoids custom libc++ linker errors
- ✅ Single variadic template (not separate Single/Multi classes)
- ✅ Abstract base class for type erasure
- ✅ Compile-time SQL generation with fold expressions
- ✅ Zero runtime overhead with `if constexpr` dispatch
- ✅ Separate statement caching for JOIN vs simple SELECT

#### 13. **JOIN Performance Analysis & Results**

**Performance Results (10,000 rows, 100 iterations, Release build):**

| JOIN Operation | Storm ORM | Raw SQLite | Efficiency |
|----------------|-----------|------------|------------|
| Simple SELECT (no JOIN) | 8.4M rows/sec | - | - |
| RIGHT JOIN (single FK) | 4.5M rows/sec | 5.0M rows/sec | 90% |
| RIGHT JOIN (multi FK) | 2.7M rows/sec | 3.2M rows/sec | 84% |
| LEFT JOIN (single FK) | 6.1M rows/sec | 6.8M rows/sec | 90% |
| LEFT JOIN (multi FK) | 3.9M rows/sec | 5.2M rows/sec | 75% |
| INNER JOIN (single FK) | 4.4M rows/sec | 7.4M rows/sec | 59% |
| INNER JOIN (multi FK) | 3.7M rows/sec | 6.0M rows/sec | 62% |

**Average Efficiency: ~77%** - Storm ORM achieves 77% of raw SQLite performance for JOIN operations.

**Key Observations:**
- **LEFT/RIGHT JOINs**: 75-90% efficiency (excellent)
- **INNER JOINs**: 59-62% efficiency (good, lower due to more complex object construction)

**Optimization Attempt: Template-Based Compile-Time JOIN**

Attempted to eliminate function pointer overhead by using templates to preserve FK field pointer information through the entire call chain, allowing perfect inlining:

```cpp
// Attempted approach: Template parameters preserve FK info
template <JoinType Type, auto... FKFieldPtrs>
auto execute_optimized_join() noexcept -> std::expected<std::vector<T>, Error> {
    using JS = JoinStatement<T, ConnType, Type, FKFieldPtrs...>;
    // Direct static call: JS::extract_joined_row(stmt, obj)
    // Should enable perfect inlining without function pointers
}
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

### Cross-Module Dependencies
```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
├── storm_orm_statements_base
├── storm_orm_utilities
├── storm_orm_statements_{insert,update,remove,select,join}
└── storm_orm_queryset
```

### Thread Safety
1. **SQLite Level**: Thread-safe with `SQLITE_OPEN_FULLMUTEX`
2. **Connection Management**: NOT thread-safe - requires external synchronization
3. **SQL Caching**: Thread-local storage eliminates synchronization overhead
4. **Recommended**: Per-thread connections or external mutex

### Compiler Requirements
Experimental Clang fork with C++26 reflection:
- Located at `../clang-p2996/`
- Custom libcxx with reflection support
- Module scanning with `clang-scan-deps`
- Reflection flags: `-freflection -fannotation-attributes`

**Known Compiler Issues & Workarounds:**

1. **Module Cache Corruption (clang-p2996)**
   - **Symptom**: Build fails with error: `module '_Builtin_stdint' is defined in both [same_path] and [same_path]`
   - **Cause**: Module cache corruption in experimental P2996 compiler with C++26 modules + GoogleTest + custom libc++
   - **Workaround**: **Simply run the build command again** - second attempt usually succeeds
   - **Why it works**: First build attempt populates module cache, second build uses it correctly
   - **When it happens**: Most commonly when building tests after clean or cache clear
   - **Quick fix**:
     ```bash
     # If build fails with module cache error:
     ninja storm_tests  # Will fail
     ninja storm_tests  # Will succeed on second try
     ```
   - **Nuclear option**: Clear cache completely if repeated attempts fail:
     ```bash
     rm -rf ~/.cache/clang/ModuleCache
     ninja storm_tests  # Then retry
     ```

2. **std::mutex Segfaults**
   - Using `std::mutex` in C++26 modules causes compiler crashes
   - Workaround: Avoid mutex in module code, use external synchronization

3. **std::inplace_vector Not Available**
   - **Symptom**: Build fails with error: `header file <inplace_vector> cannot be imported`
   - **Cause**: C++26 `std::inplace_vector` not implemented in custom libc++ yet
   - **Workaround**: Use `std::array` instead for compile-time fixed-size storage
   - **Example**: Replace `std::inplace_vector<T, N>` with `std::array<T, N>` and manual index tracking

4. **C Headers Cannot Be Imported as Modules**
   - **Symptom**: Build fails with error: `header file <cassert> (aka '...cassert') cannot be imported because it is not known to be a header unit`
   - **Cause**: C headers like `<cassert>`, `<cstring>`, etc. cannot be imported with `import` in modules
   - **Workaround**: Include C headers in the module preamble (before `export module`) using `#include`
   - **Example**: Move `#include <cassert>` before `export module` instead of using `import <cassert>`

5. **Most Vexing Parse with ConstexprString**
   - **Symptom**: Build fails with error: `type 'const std::array<char, N>' does not provide a call operator`
   - **Cause**: Parentheses initialization `std::string str(array.data(), array.size())` interpreted as function declaration
   - **Workaround**: Use braced initialization `std::string str{array.data(), array.size()}` or direct member access
   - **ConstexprString specific**: Access string data via `.data.data()` and `.len` members

6. **Missing Statement Methods**
   - **Symptom**: Build fails with error: `no member named 'column_count' in 'storm::db::sqlite::Statement'`
   - **Cause**: Custom Statement wrapper doesn't expose all SQLite functions
   - **Workaround**: Use raw SQLite handle via `stmt->handle()` and call SQLite C API directly
   - **Example**: `sqlite3_column_count(stmt->handle())` instead of `stmt->column_count()`

### Testing Strategy
- **GoogleTest** with C++26 module support
- Tests in `tests/` directory, in-memory database (`:memory:`)
- Comprehensive sanitizer support
- **ID Validation**: Tests verify returned auto-generated IDs
- **SELECT Testing**: Empty table, single/multiple rows, field types, large datasets, statement caching, integration tests
- **JOIN Testing**: Single FK and multi-FK JOINs with full object population verification (`tests/test_fk_fields.cpp`)
- **FK Field Testing**: INSERT/UPDATE/DELETE with FK fields, batch operations with FKs

## Important Implementation Notes

- **No REFL-CPP**: Project uses native C++26 reflection
- **Index Sequence Optimization**: Uses `std::index_sequence` and fold expressions
- **Pre-computed Metadata**: Field information cached in static constexpr variables
- **Module Naming**: Uses underscores (`storm_db_sqlite`) due to compiler limitations
- **Circular Dependencies**: Avoided by duplicating `FieldAttr` enum
- **Compiler Crashes**: std::mutex in modules causes segfaults
- **std::function Linker Errors**: Avoid `std::function` with custom libc++ - use abstract base classes instead (see JOIN architecture)
- **Primary Key Access**: Uses reflection splice operator `obj.[:primary_key_:]`
- **Bulk INSERT Caching**: Thread-local 8-entry cache with optimized string pre-allocation
- **JOIN Implementation**: Abstract base class (`IJoinStatement`) + variadic template for type erasure without `std::function`
- **INSERT Return Types (Breaking Change)**:
  - Single: `std::expected<int64_t, Error>` (was void)
  - Batch: `std::expected<std::vector<int64_t>, Error>` (was void)
  - All tables must use `AUTOINCREMENT`

## Performance-Driven Development Guidelines

### Evaluating Implementation Quality

When deciding which implementation approach is "good", **performance testing is mandatory**. Storm ORM's primary goal is achieving near-raw SQLite performance, so all implementation decisions must be validated with benchmarks.

**Performance Evaluation Requirements:**

1. **Benchmark Before Accepting**
   - Every new feature (WHERE clauses, LIMIT, ORDER BY, etc.) requires performance testing
   - Compare Storm ORM against raw SQLite to measure efficiency percentage
   - Target: ≥70% of raw SQLite performance for acceptable implementations
   - Document results in CLAUDE.md with operation details and efficiency metrics

2. **Use `common.py` for Test Infrastructure**
   - When adding new features (LIMIT, WHERE, ORDER BY, GROUP BY, etc.), create dedicated performance tests
   - Use `benchmarks/common.py` utilities for consistent test setup and data generation
   - If test infrastructure doesn't exist, create it following existing patterns (see `bench_join.cpp`)
   - Example structure:
     ```cpp
     // benchmarks/bench_limit.cpp
     #include "common.h"
     // Benchmark Storm ORM LIMIT vs Raw SQLite LIMIT
     ```

3. **Separate Performance Test Files**
   - Each major feature should have its own benchmark file for detailed analysis
   - Structure: `benchmarks/bench_<feature>.cpp` (e.g., `bench_limit.cpp`, `bench_where.cpp`)
   - Compare Storm ORM implementation against equivalent raw SQL
   - Use command-line flags for flexible testing (see `bench_join.cpp --help` as example)

4. **Raw SQL Comparison is Required**
   - Every benchmark must include raw SQLite baseline for efficiency calculation
   - Report efficiency percentage: `(Storm_ORM_performance / Raw_SQLite_performance) * 100`
   - Include in commit message: "feat: add LIMIT support (85% of raw SQLite efficiency)"

5. **Benchmark Model Complexity Principle**
   - **Simple models for simple features**: Use simple models (e.g., `User` with 3 fields) for testing basic functionality
   - **Complex models only when necessary**: Reserve complex models with FK relationships (e.g., `Message` with FK to `User`) for testing JOIN operations
   - **Rationale**: Testing basic LIMIT/OFFSET doesn't require FK complexity; simpler models provide clearer performance measurements
   - **Example structure**:
     ```cpp
     // Simple model for basic tests
     struct User {
         [[= storm::meta::FieldAttr::primary]] int id;
         std::string name;
         int age;
     };

     // Complex model for JOIN tests only
     struct Message {
         [[= storm::meta::FieldAttr::primary]] int id;
         [[= storm::meta::FieldAttr::fk]] User sender;
         [[= storm::meta::FieldAttr::fk]] User receiver;
         std::string text;
     };

     // Use User for: LIMIT, OFFSET, WHERE, ORDER BY, GROUP BY benchmarks
     // Use Message for: JOIN + LIMIT, JOIN + WHERE benchmarks
     ```
   - **Benefit**: Faster test execution, clearer performance bottleneck identification, reduced setup complexity
   - **See**: `benchmarks/bench_limit.cpp` uses `setup_simple_database()` for basic tests, `setup_join_database()` for JOIN tests

**Design Principles Balance:**

Storm ORM follows **DRY (Don't Repeat Yourself)** and **KISS (Keep It Simple, Stupid)** principles, but performance takes precedence when there's a conflict:

1. **DRY Principle**
   - ✅ **Apply DRY**: Extract common patterns into BaseStatement utilities, use templates for reusable code
   - ✅ **Apply DRY**: Compile-time SQL generation with ConstexprString (eliminates runtime duplication)
   - ❌ **Break DRY if needed**: If abstraction costs >10% performance, duplicate code instead
   - **Example**: Statement-level caching duplicates caching logic across Update/Delete/Select, but achieves 20x+ speedup

2. **KISS Principle**
   - ✅ **Apply KISS**: Prefer simple, readable implementations when performance is equivalent
   - ✅ **Apply KISS**: Use standard library features (`std::index_sequence`) over complex custom solutions
   - ❌ **Break KISS if needed**: Complex optimizations justified if they improve performance significantly
   - **Example**: Thread-local SQL caching adds complexity but provides 94% performance improvement

**Decision Matrix:**

| Scenario | DRY/KISS Compliant? | Performance Impact | Decision |
|----------|---------------------|-------------------|----------|
| Clean abstraction, no perf cost | ✅ Yes | None | Use abstraction (follow DRY/KISS) |
| Clean abstraction, <5% perf cost | ✅ Yes | Minimal | Use abstraction (follow DRY/KISS) |
| Clean abstraction, 5-10% perf cost | ⚠️ Maybe | Moderate | Profile carefully, document tradeoff |
| Clean abstraction, >10% perf cost | ❌ No | Significant | Duplicate code, prioritize performance |
| Complex optimization, >20% perf gain | ❌ No | Major improvement | Accept complexity, document thoroughly |

**Case Study: LIMIT Result Allocation Simplification**

A perfect example of applying KISS while maintaining (and improving) performance:

**Before**: Dual-path implementation (~50 lines)
- Path 1: `reserve() + push_back()` for LIMIT ≤1000
- Path 2: `resize()` with exponential growth for LIMIT >1000 or no LIMIT
- Rationale: Assumed reserve() would be faster for small LIMITs

**After**: Unified single-path implementation (~30 lines)
- Single path: `resize()` with smart initial sizing for all cases
- Initial size: `min(LIMIT, 10000)` or 1000 for no LIMIT
- Exponential growth for overflow handling
- **50% fewer lines**, cleaner logic, easier to maintain

**Performance Results** (benchmarked before accepting):
| LIMIT Size | Storm ORM | Raw SQLite | Efficiency |
|------------|-----------|------------|------------|
| 1 row | 5.88 M/sec | 0.29 M/sec | **2011.76%** ✅ |
| 100 rows | 8.11 M/sec | 7.63 M/sec | **106.24%** ✅ |
| 1000 rows | 8.14 M/sec | 8.61 M/sec | **94.52%** ✅ |
| 5000 rows | 8.04 M/sec | 8.89 M/sec | **90.47%** ✅ |
| **Average** | - | - | **98.24%** ✅ |

**Key Insights:**
- ✅ Simplification **improved** performance for small LIMITs (106-2000% efficiency)
- ✅ Maintained excellent performance for large LIMITs (90-95% efficiency)
- ✅ All scenarios exceed 70% target by wide margin
- ✅ Demonstrates that CLAUDE.md's claim "resize() 1.7x faster than reserve()" applies universally
- ✅ Simpler code is often faster code (better instruction cache, compiler optimizations)

**Lesson**: Don't prematurely optimize with complexity. Trust documented optimizations (like resize() > reserve()), keep code simple, and **always benchmark** to verify.

**Performance Testing Workflow:**

```bash
# 1. Implement feature (e.g., LIMIT support)
# 2. Create benchmark file using common.py patterns
touch benchmarks/bench_limit.cpp

# 3. Build and run performance tests
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_limit --size=10000 --iterations=100

# 4. Compare with raw SQLite
# If Storm: 8.5M rows/sec, Raw: 10M rows/sec → 85% efficiency ✅ GOOD
# If Storm: 5M rows/sec, Raw: 10M rows/sec → 50% efficiency ❌ NEEDS WORK

# 5. Document in CLAUDE.md
# Add to performance results table with efficiency percentage

# 6. Commit with performance metrics
git add . && git commit -m "feat: add LIMIT support (85% of raw SQLite)"
```

**Mandatory Performance Documentation:**

When adding new features, update CLAUDE.md with:
1. **Performance Results Table**: Add row with Storm vs Raw SQLite comparison
2. **Key Design Decisions**: Document optimization techniques used
3. **Benchmark Commands**: Show how to run performance tests for this feature

**Example Feature Implementation Checklist:**

- [ ] Implement feature in `src/orm/statements/`
- [ ] Add comprehensive tests in `tests/test_<feature>.cpp`
- [ ] Create performance benchmark in `benchmarks/bench_<feature>.cpp`
- [ ] Run benchmark and measure efficiency vs raw SQLite
- [ ] If efficiency <70%, optimize or document reasons
- [ ] Update CLAUDE.md performance tables
- [ ] Document any DRY/KISS tradeoffs made for performance
- [ ] Commit with performance metrics in message

**Performance Regression Prevention:**

- All PRs with ORM changes must include benchmark results
- CI should fail if performance drops >5% without justification
- Use `python3 bench.py --compare` before committing to catch regressions

## Common Development Tasks

### Adding a New Database Operation
1. Create statement class in `src/orm/statements/` inheriting from `BaseStatement<T>`
2. Implement single-object and batch operations (`std::span<const T>`)
3. Choose return type:
   - INSERT: `std::expected<int64_t, Error>` or `std::expected<std::vector<int64_t>, Error>`
   - DELETE/UPDATE: `std::expected<void, Error>`
   - SELECT: `std::expected<std::vector<T>, Error>`
4. Consider statement caching pattern (see sections above):
   - Add `cached_stmt_` member, implement `execute_single_optimized()`
   - Add `std::unique_ptr<YourStatement>` in QuerySet with lazy initialization
5. Implement compile-time SQL generation using ConstexprString
6. Add method to `QuerySet` class that delegates to statement
7. Add comprehensive tests in `tests/test_*.cpp`
8. Consider optimizations: `if constexpr` type dispatch, pre-allocation, inline functions with compiler hints

### Working with Bulk Operations

**INSERT operations with auto-generated IDs:**
```cpp
// Single insert - returns ID
auto result = queryset.insert(Person{0, "Dave", 40});
int64_t id = result.value();

// Batch insert - returns all IDs
std::vector<Person> people = {{0, "Alice", 25}, {0, "Bob", 30}};
auto result = queryset.insert(std::span<const Person>(people));
const auto& ids = result.value();
```

**UPDATE/DELETE operations:**
```cpp
// Single update
Person person{1, "Alice", 26};
queryset.update(person);

// Batch update/delete
queryset.update(std::span<const Person>(people));
queryset.remove(std::span<const Person>(people));
```

**SELECT operations:**
```cpp
auto result = queryset.select();  // Returns all rows
const auto& people = result.value();

// Repeated calls use cached statement
for (int i = 0; i < 100; ++i) {
    auto result = queryset.select();  // Optimal performance
}
```

### Optimizing Statement Performance
1. Use BaseStatement utilities: `execute_with_transaction()`, shared binding
2. Implement compile-time field binding with index sequences and fold expressions
3. Smart thresholds: Consider SQLite variable limit (999)
4. Cache SQL strings: Static methods + thread-local caching
5. Pre-compute metadata: `constexpr`/`consteval` functions
6. Batch operations: Support bulk SQL (IN, multiple VALUES) + transactions
7. Optimize allocation: Pre-calculate sizes, reserve memory
8. Thread-local caching: 8-entry cache for frequent patterns

### Index Sequence Optimization Template
```cpp
// In BaseStatement-derived class
static constexpr auto field_count_ = get_field_count();
static constexpr auto all_members_ = get_all_field_members<field_count_>();
using field_indices_t = std::make_index_sequence<field_count_>;

auto bind_all_fields(Statement& stmt, const T& obj) {
    return Base::template bind_all_fields_impl<ConnType, Statement>(
        stmt, obj, typename Base::field_indices_t());
}
```

### Compile-Time SQL Generation Template
```cpp
class YourStatement : private BaseStatement<T> {
    static consteval auto build_sql_array() {
        constexpr size_t sql_size = calculate_sql_size() + 50;
        ConstexprString<sql_size> result;
        result.append("SELECT * FROM ").append(Base::table_name_);
        // ... build SQL ...
        return result;
    }
    static constexpr auto sql_array = build_sql_array();
    static inline const std::string sql_string = std::string(sql_array);
};
```

### Thread-Local Caching Template
```cpp
struct YourStatementSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    struct CacheEntry { size_t key = 0; std::string sql; };
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0;
};
thread_local YourStatementSQLCache your_cache;

static auto get_cached_sql(size_t key) -> std::string {
    // 1. Search cache, 2. Generate with pre-allocation, 3. Store round-robin
}
```

### Performance Testing
```bash
# Recommended: Python benchmark suite with auto-rebuild (default: 1000 messages, 100 iterations)
python3 bench.py --joins                 # JOIN performance (default: 1000 messages)
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --all                   # All microbenchmarks
python3 bench.py --compare               # Full comparison

# Manual: Build and run individual benchmarks
cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# C++ JOIN benchmark with selective flags (default: sizes={100,1000,10000}, iterations=100)
./build/release/benchmarks/bench_join                                        # Run all benchmarks with defaults
./build/release/benchmarks/bench_join --size=10000 --iterations=100          # All benchmarks (custom size)
./build/release/benchmarks/bench_join --storm-join-1 --size=10000            # Storm single FK
./build/release/benchmarks/bench_join --storm-join-multi --size=10000        # Storm multi FK
./build/release/benchmarks/bench_join --raw-join-1 --size=10000              # Raw single FK
./build/release/benchmarks/bench_join --raw-join-multi --size=10000          # Raw multi FK
./build/release/benchmarks/bench_join --storm-join-1 --raw-join-1 --size=10000  # Compare single
./build/release/benchmarks/bench_join --help                                 # Show all options

# Other benchmarks
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_sqlite_orm
./build/release/benchmarks/bench_sqlite

# Legacy shell scripts
./performance_comparison_release.sh    # Comprehensive comparison
./sql_generation_analysis.sh           # SQL generation analysis
```

**Performance Checklist:**
1. Compile-time generation: Move computation to compile-time
2. Memory pre-allocation: Calculate exact sizes upfront
3. Cache common patterns: Thread-local caches
4. Batch thresholds: Optimize batch sizes
5. Index sequences: Use fold expressions
6. Measurement: Always validate with realistic data

**Expected Performance (10,000 rows, Release build):**
- Single INSERT: ~992K/sec (2.0x vs sqlite_orm)
- Single DELETE: ~21.6M/sec (73% of raw SQLite, 36x vs sqlite_orm)
- SELECT: ~13.07M rows/sec (1.51x vs sqlite_orm, 74% of raw SQLite)
- Batch INSERT: ~2.7M/sec, Batch DELETE: ~3.9M/sec
- JOIN: 4-6M rows/sec (77% average efficiency vs raw SQLite)
- Cache hit rate: >90% for common sizes (1, 10, 25, 50)

### SQL Generation Analysis
```bash
./sql_generation_analysis.sh  # Detailed analysis with color-coded results
```

**Features**: Batch size performance, cache hit/miss visualization, 100-iteration tests with avg/min/max timing, optimization impact summary.

**Use cases**: Validate cache performance, test batch optimizations, debug bottlenecks, regression testing.

### Git Merge Workflow
```bash
# Update feature branch
git fetch origin && git merge origin/develop

# Resolve conflicts if needed
git add . && git commit

# Test and push
cmake --build --preset ninja-debug && ctest --test-dir build/debug
git push
```

**Best practices**: Clean working directory, short-lived branches, thorough testing, descriptive commit messages.

### Adding PostgreSQL Support
1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` for multiple backends
4. Ensure concepts properly abstract differences
