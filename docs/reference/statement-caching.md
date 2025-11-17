# Statement-Level Caching Pattern

Unified caching pattern across UPDATE/DELETE/SELECT operations achieves near-raw SQLite performance.

## Architecture Overview

### 3-Level Caching Architecture

1. **Level 1**: QuerySet caches Statement instance (`std::unique_ptr`)
2. **Level 2**: Statement caches prepared statement (`Statement* cached_stmt_`)
3. **Level 3**: Connection caches via `prepare_cached()`

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

## Performance Impact

| Operation | Without Caching | With Caching | Speedup |
|-----------|----------------|--------------|---------|
| DELETE (repeated) | 947K/sec | 21.6M/sec | 22.8x |
| UPDATE (repeated) | ~100K/sec | 12M/sec | ~100x |
| SELECT (repeated) | ~2M/sec | 13.07M/sec | ~6.5x |

## Statements Using This Pattern

### RemoveStatement

**SQL**: `DELETE FROM table WHERE id = ?`

**Key Features:**
- Deletes by primary key
- Eliminates hash map lookups
- Statement cached for repeated deletions

**Performance**: 21.6M/sec (73% of raw SQLite, 36x faster than sqlite_orm)

### UpdateStatement

**SQL**: `UPDATE table SET field1=?, field2=?, ... WHERE id=?`

**Key Features:**
- Updates all non-primary-key fields
- Compile-time field list generation
- Statement cached for repeated updates

**Performance**: 12M/sec peak (6x faster than sqlite_orm)

### SelectStatement

**SQL**: `SELECT * FROM table` (or with JOIN clauses)

**Key Features:**
- Optimized row extraction with resize() pre-allocation
- Direct string construction (`sqlite3_column_bytes()`)
- Compiler hints: `__attribute__((hot/flatten/always_inline))`
- Separate statement cache for JOIN vs simple SELECT

**Performance**: 13.07M/sec (74% of raw SQLite, 1.51x faster than sqlite_orm)

## Why InsertStatement is NOT Cached

**Reasons:**
1. INSERT operations are typically **one-time per object** (not repeated)
2. Already uses connection-level caching via `prepare_cached()`
3. Thread-local SQL caching provides sufficient optimization for bulk operations
4. Current performance (992K/sec single, 2.7M/sec batch) **exceeds sqlite_orm by 2-6x**

**Alternative Optimization**: Thread-local SQL caching for bulk INSERT (see below)

## Implementation Pattern

### Step 1: Add Cached Statement Member

```cpp
template <typename T, typename ConnType>
class YourStatement : private BaseStatement<T> {
private:
    mutable Statement* cached_stmt_ = nullptr;  // Level 2 cache
    ConnType conn_;
};
```

### Step 2: Implement execute_single_optimized()

```cpp
auto execute_single_optimized(const T& obj) -> std::expected<void, Error> {
    // Level 2: Check statement cache
    if (!cached_stmt_) {
        // Level 3: Use connection-level cache
        auto stmt_result = conn_.prepare_cached(get_sql());
        if (!stmt_result.has_value()) {
            return std::unexpected(stmt_result.error());
        }
        cached_stmt_ = *stmt_result;
    }

    // Bind parameters with compile-time type dispatch
    this->bind_all_fields<ConnType>(*cached_stmt_, obj);

    // Execute
    if (!cached_stmt_->execute()) {
        return std::unexpected(Error::ExecutionFailed);
    }

    // Reset for next use (important!)
    cached_stmt_->reset();

    return {};
}
```

### Step 3: Add QuerySet Integration

```cpp
template <class T> class QuerySet {
private:
    mutable std::unique_ptr<YourStatement<T, ConnType>> your_stmt_;  // Level 1 cache

public:
    auto get_your_statement() const -> YourStatement<T, ConnType>& {
        if (!your_stmt_) {
            your_stmt_ = std::make_unique<YourStatement<T, ConnType>>(conn_);
        }
        return *your_stmt_;
    }

    auto your_operation(const T& obj) -> std::expected<void, Error> {
        return get_your_statement().execute_single_optimized(obj);
    }
};
```

## Thread-Local SQL Caching (Alternative Pattern)

For operations like bulk INSERT where SQL varies by batch size, use thread-local caching:

```cpp
struct BulkSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    struct CacheEntry { size_t key = 0; std::string sql; };
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0;  // Round-robin
};
thread_local BulkSQLCache bulk_sql_cache;

static auto get_cached_sql(size_t batch_size) -> std::string {
    // 1. Search cache for batch_size
    for (const auto& entry : bulk_sql_cache.entries) {
        if (entry.key == batch_size) {
            return entry.sql;  // Cache hit
        }
    }

    // 2. Generate SQL with pre-allocation
    std::string sql = generate_bulk_sql(batch_size);

    // 3. Store in cache (round-robin)
    bulk_sql_cache.entries[bulk_sql_cache.next_slot] = {batch_size, sql};
    bulk_sql_cache.next_slot = (bulk_sql_cache.next_slot + 1) % CACHE_SIZE;

    return sql;
}
```

**Performance**: 94% improvement (0.253µs → 0.016µs) for cached sizes, thread-safe, zero synchronization overhead.

## Parameter Binding Safety with Shared Cached Statements

### Why Shared Statement Caching is Safe

Multiple QuerySet instances can safely share the same cached prepared statement because **parameter binding happens atomically with execution** in the same method call. This section explains the safety mechanisms.

### Architecture Overview

When using statement caching (especially with DISTINCT/WHERE operations), you may have:
- **Shared**: `static thread_local DistinctQuerySet` object (one per thread + field combination)
- **Shared**: `cached_where_stmt_` pointer (points to same prepared statement)
- **NOT Shared**: `where_expr_` object (each QuerySet has its own)
- **NOT Shared**: Bound parameters (overwritten on each `bind()` call)

### What Gets Cached and Shared

| Component | Scope | Shared Across QuerySets? | Purpose |
|-----------|-------|-------------------------|---------|
| `DistinctQuerySet` object | `static thread_local` | ✅ YES (same thread + field combo) | Statement wrapper reuse |
| `cached_where_stmt_` pointer | Inside DistinctQuerySet | ✅ YES (points to same prepared statement) | Avoid `prepare_cached()` hash lookup |
| `cached_where_sql_` string | Inside DistinctQuerySet | ✅ YES (same SQL template) | Avoid `to_sql()` overhead |
| **`where_expr_` object** | **Per QuerySet instance** | **❌ NO** (each QuerySet has its own) | **WHERE clause parameters** |
| **Bound parameters** | **Temporary** | **❌ NO** (overwritten on each bind) | **Execution state** |

### Execution Flow Example

```cpp
// Two QuerySet instances on the same thread
QuerySet<Person> q1;
QuerySet<Person> q2;

// Scenario: Both execute DISTINCT on same field with different WHERE clauses
q1.where(age > 30).distinct<^^Person::name>().select();
q2.where(age > 50).distinct<^^Person::name>().select();
```

**Timeline:**

```cpp
// q1 execution:
// 1. Get shared DistinctQuerySet from thread_local cache
// 2. Check cached_where_stmt_ (may be null or reused)
// 3. Bind q1's WHERE expression parameters: age > 30
// 4. Execute query immediately
// 5. Return results
// 6. QuerySet q1 destroyed, WHERE expression freed

// q2 execution:
// 1. Get SAME shared DistinctQuerySet from thread_local cache
// 2. Use cached_where_stmt_ (points to same prepared statement)
// 3. Bind q2's WHERE expression parameters: age > 50
//    ⚠️ This OVERWRITES the "30" from q1 (but q1 already finished!)
// 4. Execute query immediately
// 5. Return results
```

### Why No Race Condition Occurs

**Key Safety Mechanism**: SQLite's parameter binding is **"last write wins"**:

```cpp
// Inside DistinctStatement::select_where()
if (cached_where_stmt_ == nullptr || cached_where_sql_ != current_sql) {
    cached_where_stmt_ = conn_.prepare_cached(cached_where_sql_).value();
}

// Bind THIS QuerySet's parameters (overwrites any previous bindings)
where_expr_->bind(*cached_where_stmt_, 1);  // Uses q1 or q2's OWN where_expr_

// Execute IMMEDIATELY (no window for another QuerySet to interfere)
auto results = conn_.execute_query<...>(*cached_where_stmt_);
```

**Critical Properties:**
1. **Atomic execution**: `bind()` → `execute()` happens in same method call
2. **No suspension**: No async/await or yield points between bind and execute
3. **Instance-specific state**: Each QuerySet has its own `where_expr_` object
4. **Overwrite semantics**: Each bind overwrites previous parameters completely

### When It WOULD Break (Already Documented as Unsafe)

```cpp
// ❌ UNSAFE: Concurrent use of shared QuerySet instance
QuerySet<Person> qs;

std::thread t1([&qs]() {
    qs.where(age > 30).distinct<^^Person::name>().select();
});
std::thread t2([&qs]() {
    qs.where(age > 50).distinct<^^Person::name>().select();  // RACE!
});
```

**Race Conditions:**
- Both threads modify `qs.where_expr_` concurrently
- Both threads call `bind()` on same statement at same time
- Undefined behavior: mixed parameters, crashes, or wrong results

**Solution**: Use per-thread QuerySet instances (documented safe pattern).

### Safe Pattern (Production Use)

```cpp
void worker_thread() {
    // Thread-local connection (required by SQLite)
    auto conn = db::sqlite::Connection::create("database.db").value();

    // Thread-local QuerySet instance
    QuerySet<Person> qs{conn};

    // Safe - all caching is thread-local, bind+execute is atomic
    for (int i = 0; i < 1000; i++) {
        auto results = qs.where(age > 30).distinct<^^Person::name>().select();
        // Process results...
    }
}

std::thread t1(worker_thread);
std::thread t2(worker_thread);
t1.join();
t2.join();
```

**Why This Works:**
- Each thread has its own `Connection` instance (SQLite requirement)
- Each thread has its own `QuerySet` instance (Storm requirement)
- `static thread_local` caching provides isolated storage per thread
- No shared mutable state between threads
- Binding and execution are atomic within each thread

### Performance Benefits of Shared Caching

Despite sharing cached statements safely, we still get massive performance gains:

| Benefit | Mechanism | Performance Impact |
|---------|-----------|-------------------|
| **Avoid prepare overhead** | Reuse cached `Statement*` pointer | 20-100x speedup |
| **Avoid hash lookup** | Direct pointer dereference vs `prepare_cached()` map lookup | 2-3x speedup |
| **Avoid SQL generation** | Cache `to_sql()` result string | 94% improvement for bulk ops |
| **Avoid allocations** | Reuse prepared statement buffers | Reduced memory churn |

**Key Insight**: We get the best of both worlds - **shared caching for performance** + **instance-specific binding for safety**.

## Best Practices

### DO:
- ✅ Cache statements for **repeated operations** (SELECT, UPDATE, DELETE)
- ✅ Use `cached_stmt_->reset()` after execution to reuse statement
- ✅ Mark cache members as `mutable` for const-correctness
- ✅ Use `prepare_cached()` for connection-level caching
- ✅ Implement lazy initialization (check `nullptr` before use)

### DON'T:
- ❌ Cache statements for **one-time operations** (typically INSERT)
- ❌ Forget to reset statement after use (causes "statement busy" errors)
- ❌ Share cached statements across threads (use per-thread connections)
- ❌ Cache statements with varying SQL (use thread-local caching instead)

## Debugging Tips

### Statement Not Cached
**Symptom**: Performance slower than expected
**Check**: Is `cached_stmt_` being set? Add logging to verify.

### "Statement Busy" Errors
**Symptom**: SQLite errors about busy statements
**Fix**: Ensure `cached_stmt_->reset()` is called after each execution.

### Memory Leaks
**Symptom**: Statements not freed
**Fix**: Use `std::unique_ptr` for Level 1 cache, raw pointer for Level 2 (connection owns it).

## Performance Expectations

With proper statement caching:
- **DELETE**: 70-75% of raw SQLite efficiency
- **UPDATE**: 100x improvement for repeated operations
- **SELECT**: 70-75% of raw SQLite efficiency
- **Cache hit rate**: >90% for typical workloads

## Related Patterns

- [Compile-Time SQL Generation](../architecture/design-decisions.md#8-compile-time-sql-generation-with-constexprstring)
- [Thread-Local SQL Caching](../architecture/design-decisions.md#7-thread-local-sql-caching)
- [Index Sequence Optimization](../architecture/design-decisions.md#6-compile-time-index-sequence-optimization)
