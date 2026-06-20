# Performance Tips

Practical optimization techniques for Storm ORM.

## Transaction Wrapping for Loop Inserts

### The Problem

When inserting objects in a loop without explicit transaction, each insert auto-commits:

```cpp
// SLOW: ~1.0M ops/sec - each insert triggers auto-commit
for (auto& obj : objects) {
    qs.insert(obj);
}
```

### The Solution

**Option 1: Use batch insert (recommended)**

```cpp
// FAST: ~1.9M ops/sec - handles transaction internally
qs.insert(objects);
```

**Option 2: Manual transaction wrapping**

```cpp
// FAST: ~1.7M ops/sec - single commit at end
auto conn = QuerySet<Person>::get_default_connection();
auto txn  = storm::begin(conn);          // RAII guard (#415)
for (auto& obj : objects) {
    qs.insert(obj);
}
(void)txn->commit();                       // single COMMIT; auto-ROLLBACK on early exit
```

Prefer `storm::begin(conn)` over raw `conn->execute("BEGIN TRANSACTION")`: the
guard rolls back on failure and cooperates with batch ops' inner transactions
(no nested-BEGIN collision, #9).

### Benchmark Results (10,000 inserts, in-memory SQLite)

| Method | Speed | Notes |
|--------|-------|-------|
| Loop without transaction | ~1.0 M ops/sec | Auto-commit overhead |
| Loop with transaction | ~1.7 M ops/sec | Single commit |
| Batch insert | ~1.9 M ops/sec | Optimized bulk SQL |

### Why This Matters

Each auto-commit triggers expensive operations:

| Database | Auto-commit Overhead |
|----------|---------------------|
| **SQLite** | Journal write + fsync |
| **PostgreSQL** | WAL flush + fsync |
| **MySQL (InnoDB)** | Redo log flush |
| **All databases** | Lock acquire/release |

Wrapping N inserts in one transaction amortizes this overhead:
- Without transaction: N x (insert + commit overhead)
- With transaction: N x insert + 1 x commit overhead

### Applies to All Databases

This optimization is **universal** - it works for PostgreSQL, MySQL, SQLite, and any ACID-compliant database. The principle is the same: reduce commit frequency to reduce I/O overhead.

### When to Use Each Approach

| Scenario | Recommendation |
|----------|----------------|
| Insert many objects at once | `qs.insert(objects)` - batch insert |
| Insert objects one at a time from stream | Manual transaction wrapping |
| Single insert per request (web app) | No transaction needed - auto-commit is fine |
| Mixed operations (insert + update + delete) | Manual transaction wrapping |

### Error Handling with Manual Transactions

```cpp
const auto& conn = QuerySet<Person>::get_default_connection();

if (auto result = conn->execute("BEGIN TRANSACTION"); !result) {
    // Handle error
    return;
}

bool success = true;
for (auto& obj : objects) {
    if (auto result = qs.insert(obj); !result) {
        success = false;
        break;
    }
}

if (success) {
    conn->execute("COMMIT");
} else {
    conn->execute("ROLLBACK");
}
```

## Flat Code vs Nested Lambdas

**For hot paths, prefer flat code over nested lambdas.** Benchmarks show ~3-4% improvement.

### The Problem

Nested lambdas add overhead from captures, indirect calls, and inlining barriers:

```cpp
// ❌ SLOW: Nested-lambda monadic wrappers (90% efficiency).
// (The old execute_with_transaction/execute_with_statement helpers that encoded
//  this shape were removed in #434 once every caller had moved to flat code.)
prepare_and_transact(conn, true,
    [this, objects]() {                          // Lambda 1: captures
        return run_with_statement(conn, sql,
            [this, objects](auto& stmt) {        // Lambda 2: captures again
                for (...) { ... }
            });
    });
```

### The Solution

```cpp
// ✅ FAST: Flat code (93-94% efficiency)
auto* stmt = *conn_->prepare_cached(sql);  // Connection cache: one prepare, reused by SQL text
conn_->execute("BEGIN TRANSACTION");
for (const auto& obj : objects) {
    stmt->reset();
    bind(...);
    stmt->execute();
}
conn_->execute("COMMIT");
```

### Why Lambdas Are Slower

- Capture storage overhead (storing `this`, spans, etc.)
- Indirect call through function pointer
- Compiler inlining barriers at lambda boundaries
- Extra stack frame creation per lambda

### When to Use Each

| Flat Code | Lambdas |
|-----------|---------|
| Hot paths (millions of calls) | Cold paths (setup, config) |
| Inner loops, batch operations | Callbacks, event handlers |
| Performance-critical ORM ops | Code reuse across callers |

## Raw Pointer Caching in Hot Loops

**For query loops extracting many rows, cache the raw `sqlite3_stmt*` pointer.** Benchmarks show ~5-6% improvement.

### The Problem

```cpp
// ❌ SLOW: unique_ptr::get() called on every column (90.6% efficiency)
while (stmt->step() == SQLITE_ROW) {
    obj.id = sqlite3_column_int64(stmt->handle(), 0);    // handle() = unique_ptr::get()
    obj.name = sqlite3_column_text(stmt->handle(), 1);   // handle() again
    obj.age = sqlite3_column_int(stmt->handle(), 2);     // handle() again
    // ... 6+ calls per row × millions of rows
}
```

### The Solution

```cpp
// ✅ FAST: Cache raw pointer once (96% efficiency)
sqlite3_stmt* raw_stmt = stmt->handle();  // Cache ONCE before loop
while (sqlite3_step(raw_stmt) == SQLITE_ROW) {
    obj.id = sqlite3_column_int64(raw_stmt, 0);    // Direct pointer
    obj.name = sqlite3_column_text(raw_stmt, 1);   // No indirection
    obj.age = sqlite3_column_int(raw_stmt, 2);     // Maximum speed
}
```

### Why This Matters

- `unique_ptr::get()` is not free - it's a function call with pointer dereference
- Called 6+ times per row (once per column)
- For 10,000 rows: 60,000+ unnecessary function calls
- Compiler may not inline across translation units

### Benchmark Evidence (SELECT WHERE with 10K rows)

| Pattern | Efficiency |
|---------|------------|
| Without raw pointer cache | 90.6% |
| With raw pointer cache | 96% |
