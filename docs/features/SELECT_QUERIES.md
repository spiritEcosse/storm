# SELECT Queries

Storm ORM provides highly optimized SELECT operations achieving **74% of raw SQLite performance** (13.07M rows/sec) through statement-level caching and optimized row extraction.

## Overview

Key optimizations:
- **Statement-level caching** - Reusable prepared statements
- **resize() pre-allocation** - 1.7x faster than reserve() + push_back()
- **Optimized string extraction** - Direct construction using sqlite3_column_bytes()
- **Move semantics** - For optional field assignments
- **Inline extraction** - Compiler hints for hot paths
- **74% efficiency** - 13.07M rows/sec (vs 17.67M raw SQLite)

## Basic Usage

### Select All Rows

```cpp
struct Person {
    [[=storm::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

storm::orm::QuerySet<Person> queryset(conn);

// Returns all rows from the table
auto result = queryset.select();
if (result) {
    for (const auto& person : result.value()) {
        std::cout << person.id << ": " << person.name
                  << " (" << person.age << ")" << std::endl;
    }
}
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person
```

**Performance**: 13.07M rows/sec (74% of raw SQLite, 1.51x faster than sqlite_orm)

### Select with WHERE

```cpp
using namespace storm::orm::where;

// Filter results
auto result = queryset.where(f<^^Person::age>() > 25).select();

// Complex conditions
auto result = queryset.where(f<^^Person::age>() > 25 and
                              f<^^Person::age>() < 50).select();
```

See [WHERE Clauses](WHERE_CLAUSES.md) for detailed WHERE syntax.

### Select with JOIN

```cpp
// Single FK JOIN
auto result = message_qs.join<^^Message::sender>().select();

// Multi-FK JOIN
auto result = message_qs.join<^^Message::sender, ^^Message::receiver>().select();

// JOIN with WHERE
auto result = message_qs.join<^^Message::sender>()
                        .where(f<^^User::level>() > 5)
                        .select();
```

See [JOIN Operations](JOIN_OPERATIONS.md) for detailed JOIN syntax.

## Statement-Level Caching

SelectStatement uses a 3-level caching architecture for optimal performance:

### Caching Architecture

```cpp
// Level 1: QuerySet caches Statement instance
template <class T> class QuerySet {
    mutable std::unique_ptr<SelectStatement<T, ConnType>> select_stmt_;

    auto get_select_statement() const -> SelectStatement<T, ConnType>& {
        if (!select_stmt_) {
            select_stmt_ = std::make_unique<SelectStatement<T, ConnType>>(conn_);
        }
        return *select_stmt_;
    }
};

// Level 2: Statement caches prepared statement
template <typename T> class SelectStatement {
    Statement* cached_stmt_ = nullptr;

    auto execute_optimized() {
        if (!cached_stmt_) {
            // Level 3: Connection-level caching
            cached_stmt_ = *conn_.prepare_cached(get_sql());
        }
        // Execute and extract rows
        cached_stmt_->reset();
    }
};
```

### Benefits of 3-Level Caching

1. **Level 1** (QuerySet → Statement)
   - Avoids recreating Statement objects
   - Preserves optimization state

2. **Level 2** (Statement → PreparedStatement)
   - Avoids SQL parsing
   - Reuses compiled SQL

3. **Level 3** (Connection cache)
   - Shared across all QuerySets
   - Automatic statement pooling

**Performance impact**: Achieves 74% of raw SQLite (13.07M rows/sec)

### Repeated SELECT Example

```cpp
// First call - creates and caches statement
auto result1 = queryset.select();  // Slower (cache miss)

// Subsequent calls - reuses cached statement
for (int i = 0; i < 100; ++i) {
    auto result = queryset.select();  // Optimal performance (cache hit)
}
```

**Cache hit performance**: Near-identical to raw SQLite

### Separate Caching for JOIN

SELECT with JOIN uses a separate statement cache:

```cpp
template <typename JoinStmt = void>
auto execute_optimized(JoinStmt* join_stmt = nullptr) {
    if constexpr (!std::is_void_v<JoinStmt>) {
        // Use separate cached_join_stmt_ for JOIN queries
        if (!cached_join_stmt_) {
            // Build JOIN SQL and cache
        }
    } else {
        // Use cached_stmt_ for simple SELECT
    }
}
```

**Benefit**: Simple SELECT and JOIN SELECT don't interfere with each other's caches

## Row Extraction Optimizations

### 1. resize() Pre-allocation (1.7x Speedup)

Instead of `reserve() + push_back()`, use `resize()` for direct construction:

```cpp
auto execute_optimized() -> std::expected<std::vector<T>, Error> {
    std::vector<T> result;

    // Count rows first
    size_t row_count = 0;
    while (stmt->step()) { ++row_count; }
    stmt->reset();

    // Pre-allocate with resize()
    result.resize(row_count);

    // Extract directly into pre-allocated slots
    size_t idx = 0;
    while (stmt->step()) {
        extract_row_inline_fast(stmt->handle(), result[idx++]);
    }

    return result;
}
```

**Performance**:
- `reserve() + push_back()`: 7.67M rows/sec
- `resize() + direct construction`: 13.07M rows/sec
- **Speedup**: 1.7x faster

**Why it's faster**:
- Eliminates move operations from `push_back()`
- Better memory locality
- No vector reallocation during extraction

### 2. Optimized String Extraction (2.2x Speedup)

Direct string construction using `sqlite3_column_bytes()`:

```cpp
template <size_t Idx>
static auto extract_column_inline_fast(void* stmt_handle, T& obj) noexcept {
    using FieldType = std::meta::type_of(all_members_[Idx]);
    constexpr auto member = all_members_[Idx];

    if constexpr (std::is_same_v<FieldType, std::string>) {
        // Get pointer and size directly (avoids strlen)
        const char* text = sqlite3_column_text(stmt_handle, Idx);
        int size = sqlite3_column_bytes(stmt_handle, Idx);

        // Direct construction with known size
        obj.[:member:] = std::string(text, size);
    }
    // ... other types
}
```

**Performance**:
- `std::string(sqlite3_column_text())`: 5.94M rows/sec (calls strlen)
- `std::string(text, size)`: 13.07M rows/sec (known size)
- **Speedup**: 2.2x faster

**Why it's faster**:
- Avoids `strlen()` call
- Single allocation with exact size
- No null-terminator scanning

### 3. Move Semantics for Optional Fields

For `std::optional<T>` fields, use move semantics:

```cpp
if constexpr (std::is_same_v<FieldType, std::optional<SomeType>>) {
    if (sqlite3_column_type(stmt_handle, Idx) == SQLITE_NULL) {
        obj.[:member:] = std::nullopt;
    } else {
        SomeType value = /* extract value */;
        obj.[:member:] = std::move(value);  // Move, don't copy
    }
}
```

**Benefit**: Eliminates unnecessary copies for optional fields

### 4. Inline Extraction with Compiler Hints

Mark hot paths with compiler attributes:

```cpp
template <size_t Idx>
__attribute__((hot, flatten, always_inline))
static auto extract_column_inline_fast(void* stmt_handle, T& obj) noexcept {
    // Extraction code
}
```

**Benefits**:
- `hot`: Optimize for frequently executed code
- `flatten`: Inline all function calls within this function
- `always_inline`: Force inlining even at -O2

### 5. Compile-Time Type Dispatch

Use `if constexpr` for zero runtime overhead:

```cpp
if constexpr (std::is_same_v<FieldType, int>) {
    obj.[:member:] = sqlite3_column_int(stmt_handle, Idx);
} else if constexpr (std::is_same_v<FieldType, int64_t>) {
    obj.[:member:] = sqlite3_column_int64(stmt_handle, Idx);
} else if constexpr (std::is_same_v<FieldType, double>) {
    obj.[:member:] = sqlite3_column_double(stmt_handle, Idx);
} else if constexpr (std::is_same_v<FieldType, std::string>) {
    // Optimized string extraction
}
```

**Benefit**: All type checks happen at compile-time, zero runtime cost

## Supported Field Types

All SQLite types are supported with optimized extraction:

### Integer Types
```cpp
int id = sqlite3_column_int(stmt, idx);
int64_t big_num = sqlite3_column_int64(stmt, idx);
```

### Floating Point Types
```cpp
double price = sqlite3_column_double(stmt, idx);
float ratio = static_cast<float>(sqlite3_column_double(stmt, idx));
```

### Boolean Type
```cpp
bool active = sqlite3_column_int(stmt, idx) != 0;
```

### String Types
```cpp
const char* text = sqlite3_column_text(stmt, idx);
int size = sqlite3_column_bytes(stmt, idx);
std::string str(text, size);  // Optimized: known size
```

### Optional Types
```cpp
if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    obj.field = std::nullopt;
} else {
    obj.field = /* extract value */;
}
```

### BLOB Types
```cpp
const void* blob = sqlite3_column_blob(stmt, idx);
int size = sqlite3_column_bytes(stmt, idx);
std::vector<uint8_t> data(static_cast<const uint8_t*>(blob),
                           static_cast<const uint8_t*>(blob) + size);
```

See [CRUD Operations](CRUD_OPERATIONS.md) for complete type mapping.

## Performance Analysis

### Benchmark Results

| Operation | Storm ORM | Raw SQLite | sqlite_orm | Efficiency |
|-----------|-----------|------------|------------|------------|
| SELECT (all rows) | 13.07M rows/sec | 17.67M rows/sec | 8.66M rows/sec | 74% |
| SELECT (no JOIN) | 12.81M rows/sec | - | - | - |
| SELECT + WHERE (single) | 11.88M rows/sec | 13.82M rows/sec | - | 86% |
| SELECT + WHERE (multiple) | 9.45M rows/sec | 10.57M rows/sec | - | 89.5% |
| SELECT + JOIN (single FK) | 4.4-6.1M rows/sec | 5.0-7.4M rows/sec | - | 59-90% |

### Performance Bottlenecks

**Overhead breakdown** (10,000 rows):
- SQLite row stepping: ~40% (unavoidable)
- String allocations: ~30% (heap allocations for TEXT fields)
- Column extraction: ~20% (type checks + conversion)
- Object construction: ~10% (calling constructors, field assignment)

**Why not 100% of raw SQLite?**
1. **Object construction overhead** - Raw SQLite just reads, doesn't construct objects
2. **String allocations** - Each string field requires heap allocation
3. **Type dispatch** - Even with `if constexpr`, some overhead exists
4. **Vector management** - Resizing, moving objects in result vector

**74% efficiency is excellent** for a full ORM with automatic object construction.

## Optimization Recommendations

### For Read-Heavy Workloads

1. **Reuse QuerySet instances** - Benefit from statement caching
2. **Pre-allocate vectors** - If you know approximate result size
3. **Use `std::string_view`** - For read-only string access (future optimization)
4. **Minimize string fields** - Each string requires heap allocation
5. **Use SELECT projection** - Only select fields you need (future feature)

### For Memory-Constrained Scenarios

```cpp
// Instead of loading all rows at once
auto result = queryset.select();  // Loads all rows into memory

// Consider streaming results (future feature)
// for (auto& person : queryset.stream()) { ... }
```

## Testing

Comprehensive SELECT tests in `tests/test_select.cpp`:
- Empty table scenarios
- Single row results
- Multiple rows results
- All field types
- Large datasets (10K+ rows)
- Statement caching verification
- Integration with WHERE and JOIN

## Benchmarking

Run SELECT performance benchmarks:

```bash
# Python benchmark suite (recommended)
python3 bench.py --all

# Direct C++ benchmark
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_sqlite  # Raw SQLite baseline
```

See `benchmarks/bench_storm.cpp` for SELECT benchmark implementation.

## Future Optimizations

Potential improvements for even better performance:

### 1. String Interning
Cache commonly occurring strings to reduce allocations:
```cpp
// For repeated values like "active", "pending"
std::string_view intern_string(const char* text, int size);
```

### 2. Custom Allocator
Arena allocator for temporary result vectors:
```cpp
using PersonVector = std::vector<Person, ArenaAllocator<Person>>;
```

### 3. SIMD Column Extraction
Batch extraction for multiple rows:
```cpp
// Extract 4 rows at once using SIMD
extract_four_rows_simd(stmt, result, idx);
```

### 4. Zero-Copy String Views
For read-only access, avoid string allocation:
```cpp
struct PersonView {
    int id;
    std::string_view name;  // Points to SQLite's internal buffer
    int age;
};
```

### 5. Streaming Results
Iterator-based interface for large result sets:
```cpp
for (auto& person : queryset.stream()) {
    // Process one at a time, no vector allocation
}
```

## Key Benefits

✅ **74% of raw SQLite** - Excellent ORM performance
✅ **1.51x faster than sqlite_orm** - Best-in-class ORM performance
✅ **3-level caching** - Optimal statement reuse
✅ **resize() pre-allocation** - 1.7x faster than reserve()
✅ **Optimized strings** - 2.2x faster with known size
✅ **Compile-time dispatch** - Zero runtime overhead
✅ **Inline extraction** - Compiler-guided optimization

## See Also

- [WHERE Clauses](WHERE_CLAUSES.md) - Filter SELECT results
- [JOIN Operations](JOIN_OPERATIONS.md) - SELECT with JOINs
- [Statement Caching](../architecture/STATEMENT_CACHING.md) - Caching architecture details
- [SQL Generation](../architecture/SQL_GENERATION.md) - Compile-time SQL generation
