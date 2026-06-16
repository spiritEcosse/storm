# Architecture Overview

Storm ORM is built on several key architectural principles that enable near-raw SQLite performance while providing a modern, type-safe C++26 interface.

## Core Systems

### 1. [C++26 Reflection](REFLECTION.md)
Uses `std::meta` for automatic struct-to-table mapping without macros.

**Key features**:
- Compile-time field discovery
- Automatic SQL generation
- Type-safe bindings
- Zero runtime reflection overhead

### 2. [Statement Caching](STATEMENT_CACHING.md)
3-level caching architecture achieving near-raw SQLite performance.

**Cache levels**:
- QuerySet → Statement instance
- Statement → Prepared statement
- Connection → Statement pool

**Performance impact**: 22.8x speedup for DELETE, 100x for UPDATE

### 3. [SQL Generation](SQL_GENERATION.md)
Compile-time SQL generation using ConstexprString.

**Benefits**:
- Zero runtime SQL construction
- Exact memory allocation
- Compile-time validation
- Cache-friendly

### 4. [Module System](MODULE_SYSTEM.md)
C++26 module structure with careful dependency management.

**Structure**:
```
storm (main) → db → orm → statements
```

**Challenges**:
- Circular dependency avoidance
- Custom Clang compiler requirements
- Module cache management

## Design Principles

### Performance-First

Storm prioritizes performance over code elegance:
- DRY/KISS principles **unless** they cost >10% performance
- Aggressive optimization (compile-time generation, caching, inlining)
- Target: ≥95% of raw SQLite performance

### Type Safety

Compile-time type checking throughout:
- Field types via `std::meta::type_of()`
- WHERE clause type validation
- Parameter binding type dispatch

### Zero Runtime Overhead

Extensive use of compile-time features:
- `constexpr`/`consteval` functions
- `if constexpr` type dispatch
- Template metaprogramming
- Index sequences and fold expressions

## Module Structure

```
src/
├── storm.cppm                      # Main module
├── db/
│   ├── concept.cppm                # Database concepts
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet interface
    ├── field_attr.cppm             # FieldAttr annotation enum (leaf module, #387)
    ├── utilities.cppm              # ConstexprString, SQLCache
    └── statements/
        ├── base.cppm               # BaseStatement utilities
        ├── extract.cppm            # ColumnExtractor — row→value converters (#434)
        ├── field_names.cppm        # FieldNameGrammar — column-list SQL builders (#434)
        ├── insert.cppm             # InsertStatement
        ├── select.cppm             # SelectStatement + JOIN
        ├── update.cppm             # UpdateStatement
        ├── update_grammar.cppm     # UpdateGrammar — UPDATE SQL builders (#434)
        ├── erase.cppm              # EraseStatement
        └── join.cppm               # JoinStatement (SQL builder)
```

## Cross-Module Dependencies

```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
├── storm_orm_field_attr
├── storm_orm_statements_base
├── storm_orm_utilities
├── storm_orm_statements_{insert,update,erase,select,join}
└── storm_orm_queryset
```

**Dependency rules**:
- Modules use underscores (compiler limitation)
- No circular dependencies
- BaseStatement provides shared utilities
- Concepts define database interfaces

## Key Optimizations

### 1. Index Sequence Optimization

Replace recursive templates with fold expressions:

```cpp
using field_indices_t = std::make_index_sequence<field_count_>;

template <typename ConnType, typename Statement, size_t... Is>
auto bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) {
    return (bind_field_at_index<ConnType, Is>(stmt, obj, Is + 1) && ...);
}
```

**Benefits**: Reduced template depth, faster compilation, better assembly

### 2. Thread-Local SQL Caching

8-entry cache for bulk INSERT SQL strings:

```cpp
struct BulkSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0;
};
thread_local BulkSQLCache bulk_sql_cache;
```

**Performance**: 94% improvement (0.253µs → 0.016µs)

### 3. Statement-Level Caching Pattern

Unified caching across UPDATE/DELETE/SELECT:

```cpp
template <typename T> class XStatement {
    Statement* cached_stmt_ = nullptr;

    auto execute_optimized(const T& obj) {
        if (!cached_stmt_) {
            cached_stmt_ = *conn_.prepare_cached(get_sql());
        }
        // Execute with cached statement
    }
};
```

**Performance**: 20x+ speedup for repeated operations

### 4. Optimized Row Extraction

Multiple optimizations in SelectStatement:
- resize() pre-allocation (1.7x faster than reserve())
- Direct string construction (2.2x faster, avoids strlen)
- Move semantics for optional fields
- Inline extraction with compiler hints

**Result**: 74% of raw SQLite performance (13.07M rows/sec)

## Concept-Based Database Abstraction

Database independence through concepts:

```cpp
concept DatabaseConnection = requires(T conn) {
    { conn.execute(std::string{}) } -> std::same_as<void>;
    { conn.prepare(std::string{}) } -> std::convertible_to<DatabaseStatement>;
    // ... more requirements
};

concept DatabaseStatement = requires(T stmt) {
    { stmt.step() } -> std::same_as<bool>;
    { stmt.reset() } -> std::same_as<void>;
    // ... more requirements
};
```

**Benefits**:
- Future PostgreSQL/MySQL support
- No ORM code changes needed
- Compile-time interface verification

## Thread Safety

**SQLite Level**: Thread-safe (SQLITE_OPEN_FULLMUTEX)

**Connection Management**: NOT thread-safe (compiler limitations with std::mutex in modules)

**SQL Caching**: Thread-local (zero synchronization overhead)

**Recommendation**: Per-thread connections or external mutex

## Testing Strategy

- **GoogleTest** with C++26 module support
- In-memory database (`:memory:`)
- Comprehensive sanitizer support (Address, Leak, Thread)
- ID validation for INSERT operations
- Performance regression tests

## See Also

- [C++26 Reflection](REFLECTION.md) - How reflection enables ORM
- [Statement Caching](STATEMENT_CACHING.md) - 3-level caching details
- [SQL Generation](SQL_GENERATION.md) - Compile-time SQL generation
- [Module System](MODULE_SYSTEM.md) - Module structure and dependencies
