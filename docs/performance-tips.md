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
auto& conn = QuerySet<Person>::get_default_connection();
conn->execute("BEGIN TRANSACTION");
for (auto& obj : objects) {
    qs.insert(obj);
}
conn->execute("COMMIT");
```

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
auto& conn = QuerySet<Person>::get_default_connection();

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
