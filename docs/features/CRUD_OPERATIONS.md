# CRUD Operations

Storm ORM provides full CRUD (Create, Read, Update, Delete) operations with auto-generated IDs and optimized performance.

## Overview

All CRUD operations use:
- **Compile-time SQL generation** - Zero runtime SQL construction
- **Statement caching** - Reusable prepared statements
- **Type-safe binding** - Compile-time type dispatch
- **Auto-generated IDs** - Returned from INSERT operations

## INSERT Operations

### Return Types (Breaking Change)

Storm ORM returns auto-generated IDs from all INSERT operations:
- **Single INSERT**: `std::expected<int64_t, Error>` (returns the generated ID)
- **Batch INSERT**: `std::expected<std::vector<int64_t>, Error>` (returns all generated IDs)

### Table Requirements

Tables use a plain integer primary key, which SQLite auto-assigns on insert (it
aliases the rowid). Since #379 Storm emits plain `INTEGER PRIMARY KEY` by default —
`AUTOINCREMENT` (the never-reuse guarantee, ~358 ns/insert) is opt-in via
`FieldAttr::primary_autoincrement`.

```cpp
conn.execute("CREATE TABLE Person ("
    "id INTEGER PRIMARY KEY, "
    "name TEXT NOT NULL, "
    "age INTEGER NOT NULL)");
```

To opt into the never-reuse guarantee for a specific model, annotate its PK with
`primary_autoincrement` instead of `primary`:

```cpp
struct Audit {
    [[= storm::meta::FieldAttr::primary_autoincrement]] int id{};  // id INTEGER PRIMARY KEY AUTOINCREMENT
    // ...
};
```

### Single INSERT

```cpp
struct Person {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

storm::orm::QuerySet<Person> queryset(conn);

// Insert returns the generated ID
auto result = queryset.insert(Person{0, "Alice", 25});
if (result) {
    int64_t id = result.value();
    std::cout << "Inserted person with ID: " << id << std::endl;
}
```

### Batch INSERT

```cpp
std::vector<Person> people = {
    {0, "Alice", 25},
    {0, "Bob", 30},
    {0, "Charlie", 35}
};

// Returns all generated IDs
auto result = queryset.insert(std::span<const Person>(people));
if (result) {
    const auto& ids = result.value();
    for (size_t i = 0; i < ids.size(); ++i) {
        std::cout << people[i].name << " ID: " << ids[i] << std::endl;
    }
}
```

### Batch INSERT Strategy

Storm ORM uses smart thresholds based on SQLite's variable limit (999):

1. **≤50 objects**: Bulk INSERT with multiple VALUES
   ```sql
   INSERT INTO Person (name, age) VALUES (?, ?), (?, ?), ...
   ```

2. **>50 objects**: Individual statements within a transaction
   ```sql
   BEGIN TRANSACTION;
   INSERT INTO Person (name, age) VALUES (?, ?);
   INSERT INTO Person (name, age) VALUES (?, ?);
   ...
   COMMIT;
   ```

### Thread-Local SQL Caching

Batch INSERT uses an 8-entry thread-local cache for SQL strings:

```cpp
struct BulkSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0; // Round-robin replacement
};
thread_local BulkSQLCache bulk_sql_cache;
```

## UPDATE Operations

### Single UPDATE

Updates all non-primary-key fields:

```cpp
Person person{1, "Alice", 26};  // ID = 1, new age
auto result = queryset.update(person);
if (!result) {
    std::cerr << "Update failed: " << result.error().message << std::endl;
}
```

**SQL Generated**:
```sql
UPDATE Person SET name=?, age=? WHERE id=?
```

### Batch UPDATE

```cpp
std::vector<Person> people = {
    {1, "Alice", 26},
    {2, "Bob", 31},
    {3, "Charlie", 36}
};

auto result = queryset.update(std::span<const Person>(people));
```

**Strategy**: Individual UPDATE statements within a transaction for consistency

### Statement Caching

UpdateStatement uses the 3-level caching pattern:

```cpp
template <typename T> class UpdateStatement {
    Statement* cached_stmt_ = nullptr;

    auto execute_single_optimized(const T& obj) {
        if (!cached_stmt_) {
            cached_stmt_ = *conn_.prepare_cached(get_sql());
        }
        // Bind fields and execute
        cached_stmt_->reset();
    }
};
```

## DELETE Operations

### Single DELETE

```cpp
Person person{1, "Alice", 25};  // Only ID matters
auto result = queryset.erase(person);
```

**SQL Generated**:
```sql
DELETE FROM Person WHERE id=?
```

### Batch DELETE

```cpp
std::vector<Person> people = {
    {1, "Alice", 25},
    {2, "Bob", 30},
    {3, "Charlie", 35}
};

auto result = queryset.erase(std::span<const Person>(people));
```

**SQL Generated** (bulk IN clause):
```sql
DELETE FROM Person WHERE id IN (?, ?, ?)
```

## Error Handling

All operations return `std::expected<T, Error>`:

```cpp
auto result = queryset.insert(person);
if (result) {
    // Success
    int64_t id = result.value();
} else {
    // Error
    std::cerr << "Error: " << result.error().message << std::endl;
    std::cerr << "Code: " << result.error().code << std::endl;
}
```

## Supported Field Types

Storm ORM supports all standard SQLite types through compile-time type dispatch:

### Integer Types
- `int`, `short`, `unsigned int` → `bind_int()`
- `int64_t`, `long`, `long long`, `uint64_t` → `bind_int64()`

### Floating Point Types
- `double` → `bind_double()`
- `float` → `bind_double()` (cast)

### Boolean Type
- `bool` → Stored as INTEGER (0/1)

### String Types
- `std::string`, `const char*`, `std::string_view` → `bind_text()`

### Optional Types (NULL Support)
- `std::optional<T>` for any supported type T
- Binds NULL when `std::nullopt`, otherwise binds the contained value
- Examples: `std::optional<int>`, `std::optional<std::string>`

### BLOB Types
- `std::vector<uint8_t>`, `std::vector<unsigned char>` → `bind_blob()`

### Type Dispatch

The binding uses compile-time `if constexpr` for zero runtime overhead:

```cpp
template <typename ConnType, typename Statement, typename FieldType>
static auto bind_value_by_type(Statement& stmt, int idx, const FieldType& value) {
    if constexpr (std::is_same_v<FieldType, int>) {
        return stmt.bind_int(idx, value);
    } else if constexpr (std::is_same_v<FieldType, int64_t>) {
        return stmt.bind_int64(idx, value);
    } else if constexpr (std::is_same_v<FieldType, std::string>) {
        return stmt.bind_text(idx, value);
    }
    // ... more type checks
}
```

## Foreign Key Support

### INSERT with FK Fields

```cpp
struct Message {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string content;
    User sender;  // FK field - entire object
};

Message msg{0, "Hello", User{1, "Alice", 10}};
auto result = message_qs.insert(msg);
```

**Behavior**:
- Only the FK's primary key is bound (sender.id)
- Other FK fields are ignored during INSERT
- Returned ID is for the Message, not the FK

### UPDATE with FK Fields

```cpp
msg.content = "Updated content";
msg.sender.id = 2;  // Change FK reference
auto result = message_qs.update(msg);
```

**SQL Generated**:
```sql
UPDATE Message SET content=?, sender_id=? WHERE id=?
```

### DELETE with FK Fields

FK fields are ignored during DELETE - only the primary key matters:

```cpp
auto result = message_qs.erase(msg);  // Only msg.id is used
```

## Performance Optimization Tips

1. **Use batch operations** for multiple inserts/updates/deletes
2. **Reuse QuerySet instances** to benefit from statement caching
3. **Pre-allocate vectors** for batch operations
4. **Use transactions** explicitly for complex multi-operation workflows
5. **Avoid unnecessary copies** - use const references and std::span

## Testing

All CRUD operations are thoroughly tested:
- Empty table scenarios
- Single object operations
- Batch operations (various sizes)
- Error conditions
- ID validation
- FK field handling

See `tests/test_crud.cpp` for comprehensive test coverage.

## See Also

- [SELECT Queries](SELECT_QUERIES.md) - Read operations with caching
- [Batch Operations](BATCH_OPERATIONS.md) - Detailed batch operation strategies
- [Statement Caching](../architecture/STATEMENT_CACHING.md) - Caching architecture details
- [SQL Generation](../architecture/SQL_GENERATION.md) - Compile-time SQL generation
