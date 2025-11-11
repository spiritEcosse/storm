# Statement Caching Architecture

Storm ORM achieves near-raw SQLite performance through a 3-level caching architecture that reuses prepared statements across operations.

## 3-Level Caching Architecture

### Level 1: QuerySet → Statement Instance

QuerySet maintains cached statement instances:

```cpp
template <class T> class QuerySet {
    mutable std::unique_ptr<SelectStatement<T, ConnType>> select_stmt_;
    mutable std::unique_ptr<UpdateStatement<T, ConnType>> update_stmt_;
    mutable std::unique_ptr<RemoveStatement<T, ConnType>> remove_stmt_;

    auto get_select_statement() const -> SelectStatement<T, ConnType>& {
        if (!select_stmt_) {
            select_stmt_ = std::make_unique<SelectStatement<T, ConnType>>(conn_);
        }
        return *select_stmt_;
    }
};
```

**Benefits**:
- Avoids recreating statement objects
- Preserves optimization state
- Lazy initialization

### Level 2: Statement → Prepared Statement

Statement caches the prepared SQLite statement:

```cpp
template <typename T> class SelectStatement {
    mutable Statement* cached_stmt_ = nullptr;

    auto execute_optimized() {
        if (!cached_stmt_) {
            cached_stmt_ = *conn_.prepare_cached(get_sql());
        }

        // Execute query
        while (cached_stmt_->step()) {
            // Extract rows
        }

        cached_stmt_->reset();  // Ready for reuse
    }
};
```

**Benefits**:
- Avoids SQL parsing
- Reuses compiled SQL execution plan
- reset() instead of finalize()

### Level 3: Connection → Statement Pool

Connection maintains a pool of prepared statements:

```cpp
auto prepare_cached(const std::string& sql) -> std::expected<Statement*, Error> {
    // Check if already prepared
    if (auto it = stmt_cache_.find(sql); it != stmt_cache_.end()) {
        return &it->second;  // Cache hit!
    }

    // Prepare new statement
    auto stmt = prepare(sql);
    stmt_cache_[sql] = std::move(*stmt);
    return &stmt_cache_[sql];
}
```

**Benefits**:
- Shared across all QuerySets
- Automatic statement pooling
- LRU eviction (future enhancement)

## Performance Impact

### UPDATE Operations

**Before caching**: 100K operations/sec
**After caching**: 12M peak operations/sec
**Speedup**: 100x+ improvement

### DELETE Operations

**Before caching**: 947K operations/sec
**After caching**: 21.6M operations/sec (73% of raw SQLite)
**Speedup**: 22.8x improvement

### SELECT Operations

**With caching**: 13.07M rows/sec (74% of raw SQLite)
**Cache hit rate**: Near 100% for repeated queries

## Why InsertStatement is NOT Cached

INSERT operations typically don't benefit from statement-level caching:

1. **One-time per object** - Objects are usually inserted once
2. **Connection-level caching** - Already uses `prepare_cached()`
3. **Thread-local SQL caching** - Optimizes bulk INSERT SQL generation
4. **Current performance** - 992K/sec single, 2.7M/sec batch (2-6x faster than sqlite_orm)

Adding statement-level caching to INSERT would add complexity without meaningful benefit.

## Separate Caching for JOIN

SelectStatement uses separate caches for simple SELECT vs JOIN queries:

```cpp
template <typename T> class SelectStatement {
    mutable Statement* cached_stmt_ = nullptr;       // Simple SELECT
    mutable Statement* cached_join_stmt_ = nullptr;  // JOIN SELECT

    template <typename JoinStmt = void>
    auto execute_optimized(JoinStmt* join_stmt = nullptr) {
        if constexpr (!std::is_void_v<JoinStmt>) {
            // Use JOIN cache
            if (!cached_join_stmt_) { ... }
        } else {
            // Use simple cache
            if (!cached_stmt_) { ... }
        }
    }
};
```

**Benefit**: Simple SELECT and JOIN SELECT don't interfere with each other

## Cache Lifetime

**Statement instance**: Lives as long as QuerySet
**Prepared statement**: Lives as long as Statement instance
**Connection pool**: Lives as long as Connection

**Typical pattern**:
```cpp
// Create QuerySet once
QuerySet<Person> qs(conn);

// Reuse for multiple operations
for (int i = 0; i < 1000; ++i) {
    qs.select();   // Cached statement reused
}
```

## Thread Safety

**Thread-local caching**: Each thread has separate SQL cache (zero synchronization)

**Connection-level caching**: NOT thread-safe (use per-thread connections)

**Recommendation**: Per-thread QuerySet + Connection instances

## See Also

- [SELECT Queries](../features/SELECT_QUERIES.md) - How SELECT uses caching
- [CRUD Operations](../features/CRUD_OPERATIONS.md) - UPDATE/DELETE caching
- [SQL Generation](SQL_GENERATION.md) - What gets cached
