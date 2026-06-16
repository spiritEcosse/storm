# Supported Field Types

Storm ORM supports all standard SQLite types through compile-time type dispatch in `BaseStatement::bind_value_by_type()` (src/orm/statements/base.cppm) and `SelectStatement::extract_column_inline_fast()` (src/orm/statements/select.cppm).

## Integer Types

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `int` | INTEGER | `bind_int()` | `extract_int()` |
| `short` | INTEGER | `bind_int()` (cast) | `extract_int()` (cast) |
| `unsigned short` | INTEGER | `bind_int()` (cast) | `extract_int()` (cast) |
| `unsigned int` | INTEGER | `bind_int()` (cast) | `extract_int()` (cast) |
| `int64_t` | INTEGER | `bind_int64()` | `extract_int64()` |
| `long` | INTEGER | `bind_int64()` | `extract_int64()` |
| `long long` | INTEGER | `bind_int64()` | `extract_int64()` |
| `uint64_t` | INTEGER | `bind_int64()` (cast ⚠️) | `extract_int64()` (cast ⚠️) |
| `unsigned long` | INTEGER | `bind_int64()` (cast ⚠️) | `extract_int64()` (cast ⚠️) |
| `unsigned long long` | INTEGER | `bind_int64()` (cast ⚠️) | `extract_int64()` (cast ⚠️) |

> ⚠️ **Signed storage caveat for 64-bit unsigned types (#419).** Neither SQLite
> nor PostgreSQL has an unsigned 64-bit integer type. Storm maps `uint64_t` /
> `unsigned long` / `unsigned long long` to a **signed** 8-byte column (`INTEGER`
> on SQLite, `BIGINT` on PostgreSQL) and casts to `std::int64_t` at bind time.
>
> - **Values ≤ `INT64_MAX` (`9223372036854775807`, i.e. 2⁶³−1) are exact** — no caveat.
> - **Values > `INT64_MAX` (the upper half of the unsigned range) are stored as a
>   negative `int64`** (two's-complement reinterpretation). For such values:
>   1. **Equality still round-trips.** A `SELECT` through Storm casts the stored
>      signed bits back to unsigned, recovering the original value, so `WHERE col = v`
>      and reading the field both work.
>   2. **Ordering is wrong.** `ORDER BY`, `>`, `<`, `BETWEEN` sort by the *signed*
>      interpretation, so a `uint64` of `2⁶³ + 1` sorts **before** `1`.
>   3. **External readers see a negative number.** Raw SQL, a BI/report tool, or
>      another application querying the column directly sees the signed value, not
>      the intended unsigned one. (PostgreSQL `BIGINT` would also *reject* the true
>      unsigned value with `bigint out of range`; Storm avoids that error only
>      because it pre-casts to signed.)
>
> **Recommendation:** if you need the full unsigned 64-bit range *with* correct
> ordering and external readability, store the value as `TEXT` (zero-padded) or a
> `BLOB` you compare/sort yourself, rather than the native integer column. This
> behavior is identical on SQLite and PostgreSQL and is pinned by
> `Uint64SignedStorageTest` in `tests/schema/test_types.cpp`.

**Usage:**
```cpp
struct Example {
    [[=storm::meta::FieldAttr::primary]] int id;
    int64_t big_number;
    unsigned short count;
};
```

## Floating Point Types

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `double` | REAL | `bind_double()` | `extract_double()` |
| `float` | REAL | `bind_double()` (cast) | `extract_float()` |

**Usage:**
```cpp
struct Measurement {
    [[=storm::meta::FieldAttr::primary]] int id;
    double precision_value;
    float approximate_value;
};
```

## Boolean Type

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `bool` | INTEGER | `bind_int()` | `extract_bool()` |

**Storage**: Stored as INTEGER (0 = false, 1 = true)

**Usage:**
```cpp
struct User {
    [[=storm::meta::FieldAttr::primary]] int id;
    bool is_active;
    bool is_admin;
};
```

## String Types

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `std::string` | TEXT | `bind_text()` | `extract_text_ptr()` |
| `const char*` | TEXT | `bind_text()` | N/A (output only as std::string) |
| `std::string_view` | TEXT | `bind_text()` | N/A (output only as std::string) |

**Notes:**
- Any type convertible to `std::string_view` can be used for binding
- Extraction always returns `std::string` (owns the data)
- Optimized string extraction using `sqlite3_column_bytes()` (avoids strlen)

**Usage:**
```cpp
struct Document {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string title;
    std::string content;
};

// All these work for binding:
Document doc1{0, "Title", "Content"};
Document doc2{0, std::string("Title"), std::string("Content")};
std::string_view title_view = "Title";
Document doc3{0, std::string(title_view), "Content"};
```

## Optional Types (NULL Support)

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `std::optional<T>` | NULL / T's type | `bind_null()` / recursive | `is_null()` check |

**Supported optional types:**
- `std::optional<int>`
- `std::optional<int64_t>`
- `std::optional<double>`
- `std::optional<float>`
- `std::optional<bool>`
- `std::optional<std::string>`
- `std::optional<std::vector<uint8_t>>`

**Behavior:**
- `std::nullopt` → SQLite NULL
- Value present → Recursively binds the contained value

**Usage:**
```cpp
struct Contact {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    std::optional<std::string> email;     // Can be NULL
    std::optional<int> age;               // Can be NULL
    std::optional<bool> is_verified;      // Can be NULL
};

// Examples
Contact c1{1, "Alice", "alice@example.com", 25, true};      // All fields set
Contact c2{2, "Bob", std::nullopt, std::nullopt, std::nullopt}; // NULLs
Contact c3{3, "Charlie", "charlie@example.com", std::nullopt, false}; // Mixed
```

## BLOB Types (Binary Data)

| C++ Type | SQLite Type | Binding Method | Extraction Method |
|----------|-------------|----------------|-------------------|
| `std::vector<uint8_t>` | BLOB | `bind_blob()` | `extract_blob()` |
| `std::vector<unsigned char>` | BLOB | `bind_blob()` | `extract_blob()` |

**Usage:**
```cpp
struct FileData {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string filename;
    std::vector<uint8_t> data;
};

// Example with binary data
std::vector<uint8_t> binary_data = {0x89, 0x50, 0x4E, 0x47}; // PNG header
FileData file{0, "image.png", binary_data};
```

## Automatic Timestamps (`auto_create` / `auto_update`)

Two field attributes populate `std::chrono::system_clock::time_point` columns with
the current time automatically, so you never set them by hand (#209):

| Attribute | INSERT | UPDATE |
|-----------|--------|--------|
| `auto_create` | set to `now()` | preserved (bound from the object's stored value) |
| `auto_update` | set to `now()` | set to `now()` |

```cpp
struct User {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    [[=storm::meta::FieldAttr::auto_create]] std::chrono::system_clock::time_point created_at;
    [[=storm::meta::FieldAttr::auto_update]] std::chrono::system_clock::time_point updated_at;
};

// INSERT — both stamped automatically; any value you set is ignored.
QuerySet<User>().insert(User{.name = "John"}).execute();
// row: created_at = now, updated_at = now

// UPDATE — only updated_at re-stamped; created_at preserved.
User u{.id = 1, .name = "Jane", .created_at = original_created_at};
QuerySet<User>().update(u).execute();
// row: created_at unchanged, updated_at = now
```

**Contract and constraints:**

- **Bind-time only, no write-back.** The value is computed in C++ (`system_clock::now()`)
  and bound as a parameter. The caller's in-memory object is **not** mutated — re-SELECT
  the row to read the stamped value.
- **Preserving `created_at` on UPDATE** requires the object to carry its original
  `created_at` (UPDATE binds it from the object). Load-modify-save, or pass the value
  you read on INSERT.
- **One `now()` per batch.** A bulk INSERT/UPDATE reads the clock once and shares it
  across every row, so all rows in a batch get the same timestamp.
- **Type-checked at compile time.** An `auto_create`/`auto_update` field that is not a
  `std::chrono::system_clock::time_point` fails to compile with a clear message.
- **Zero cost when unused.** Models without any timestamp field do not pay for the
  clock read — the call compiles away.

The column maps to `TIMESTAMP` (PostgreSQL) / `TEXT` (SQLite) and round-trips via the
existing `time_point` ↔ `"YYYY-MM-DD HH:MM:SS"` conversion.

## Many-to-Many Container Fields (`many_to_many` / `many_to_many_through`)

A container member annotated with `[[= storm::meta::many_to_many]]` (or
`many_to_many_through<JunctionModel>`) declares a many-to-many relationship (#203):

```cpp
struct Student {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::meta::many_to_many]] std::vector<Course> courses;
};
```

- **Not a column.** The member maps to a junction table, not to a column —
  `INSERT`/`SELECT`/`UPDATE`/schema generation skip it entirely. Plain `select()`
  leaves it empty; `join<^^Student::courses>()` eager-loads it.
- **Supported containers:** `std::vector<T>`, `plf::hive<T>`, `std::deque<T>`,
  and smart-pointer elements (`std::vector<std::shared_ptr<T>>`); the related
  model type is extracted via C++26 `std::meta`.
- **Junction DDL** is auto-generated for the `many_to_many` form (see
  [JOIN_OPERATIONS.md](../features/JOIN_OPERATIONS.md#many-to-many-joins-203)).

## Type Dispatch Implementation

The binding uses compile-time `if constexpr` type dispatch to select the appropriate SQLite binding function with **zero runtime overhead**.

```cpp
template <typename U>
static constexpr auto bind_value_by_type(Statement& stmt, int index, const U& value) -> bool {
    if constexpr (std::is_same_v<U, int>) {
        return stmt.bind_int(index, value);
    } else if constexpr (std::is_same_v<U, int64_t> || std::is_same_v<U, long> || std::is_same_v<U, long long>) {
        return stmt.bind_int64(index, static_cast<int64_t>(value));
    } else if constexpr (std::is_same_v<U, double>) {
        return stmt.bind_double(index, value);
    } else if constexpr (std::is_same_v<U, bool>) {
        return stmt.bind_int(index, value ? 1 : 0);
    } else if constexpr (/* string types */) {
        return stmt.bind_text(index, value);
    } else if constexpr (/* optional types */) {
        // Handle std::optional recursively
    } else if constexpr (/* blob types */) {
        return stmt.bind_blob(index, value.data(), value.size());
    }
}
```

## Future Type Support

Planned additions:
- `std::chrono::time_point` (as INTEGER or TEXT)
- `std::chrono::duration` (as INTEGER)
- `std::filesystem::path` (as TEXT)
- Custom types via user-defined conversions
- `std::span<uint8_t>` for BLOB (non-owning)

## Table Creation Guidelines

When creating tables, ensure column types match the C++ type mappings:

```sql
CREATE TABLE Example (
    id INTEGER PRIMARY KEY,  -- AUTOINCREMENT is opt-in (#379): FieldAttr::primary_autoincrement
    name TEXT NOT NULL,
    age INTEGER,
    salary REAL,
    is_active INTEGER,  -- bool
    email TEXT,         -- std::optional<std::string> (can be NULL)
    data BLOB
);
```

## Type Safety

Storm ORM provides compile-time type safety:
- Incorrect type usage → Compilation error
- No runtime type checking overhead
- SQLite type affinity automatically handled
