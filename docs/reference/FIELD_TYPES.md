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
| `uint64_t` | INTEGER | `bind_int64()` (cast) | `extract_int64()` (cast) |
| `unsigned long` | INTEGER | `bind_int64()` (cast) | `extract_int64()` (cast) |
| `unsigned long long` | INTEGER | `bind_int64()` (cast) | `extract_int64()` (cast) |

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
