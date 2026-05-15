# Statement Caching Architecture

Storm ORM achieves near-raw SQLite performance through a 3-level caching architecture that reuses prepared statements across operations.

## 3-Level Caching Architecture

### Level 1: QuerySet → Statement Instance

QuerySet maintains cached statement instances:

```cpp
template <class T> class QuerySet {
    mutable std::unique_ptr<SelectStatement<T, ConnType>> select_stmt_;
    mutable std::unique_ptr<UpdateStatement<T, ConnType>> update_stmt_;
    mutable std::unique_ptr<EraseStatement<T, ConnType>> erase_stmt_;

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

## Cache Invalidation

Cached statements pin the *compiled* form of a SQL string to a specific schema.
If the underlying schema changes (`ALTER TABLE`, `DROP TABLE`, table rename),
the cached prepared statements become stale. Storm exposes three explicit hooks
to drop them; Storm does **not** auto-detect DDL (see Issue #215 follow-ups).

| API | Scope | When to call |
|-----|-------|--------------|
| `Connection::clear_statement_cache()` | Whole connection | After a broad schema rebuild or to free memory |
| `Connection::clear_statement_cache(table)` | Entries referencing `table` | After targeted DDL — e.g., `ALTER TABLE persons ADD COLUMN …` |
| `QuerySet::reset()` | This QuerySet's Level 1 + 2 caches **and** WHERE/JOIN/LIMIT state | When reusing a QuerySet with a fresh filter chain |
| `QuerySet::invalidate_cache()` | This QuerySet's Level 1 + 2 caches only | After targeted DDL when you want to keep the filter chain |

### Correct invalidation order

```cpp
// 1. Targeted DDL through the raw connection
conn->execute("ALTER TABLE persons ADD COLUMN nickname TEXT");

// 2. Drop Level 1+2 caches that hold pointers into Level 3
qs.invalidate_cache();          // or qs.reset()

// 3. Drop the matching Level 3 entries
conn->clear_statement_cache("persons");

// 4. Next operation re-prepares against the new schema
qs.select().execute();
```

The order matters: clearing Level 3 first would leave Insert/Update/Erase/Select
holding pointers into freed prepared statements. The Level 2 invalidation must
happen *before* the connection cache is cleared.

### Pointer stability

Level 3 stores statements as `unordered_map<string, unique_ptr<Statement>>`
(not `unordered_map<string, Statement>`). The `unique_ptr` keeps the Statement
pinned in memory across cache growth, so the raw `Statement*` pointers held at
Level 2 stay valid as long as the underlying entry is not erased. This removes
the hidden capacity contract that pre-Issue-#215 code relied on (reserve 32
slots and hope no one grows past that).

## Thread Safety

**Thread-local caching**: Each thread has separate SQL cache (zero synchronization)

**Connection-level caching**: NOT thread-safe (use per-thread connections)

**Recommendation**: Per-thread QuerySet + Connection instances

## See Also

- [SELECT Queries](../features/SELECT_QUERIES.md) - How SELECT uses caching
- [CRUD Operations](../features/CRUD_OPERATIONS.md) - UPDATE/DELETE caching
- [SQL Generation](SQL_GENERATION.md) - What gets cached
