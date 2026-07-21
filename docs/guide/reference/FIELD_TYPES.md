# Supported Field Types

Storm ORM supports all standard SQLite types through compile-time type dispatch in `BaseStatement::bind_value_by_type()` (src/orm/statements/base.cppm) and `SelectStatement::extract_column_inline_fast()` (src/orm/statements/select.cppm).

> **Annotation spelling (#442).** All field annotations are re-exported into the top-level
> `storm` namespace, so models spell them as `storm::primary`, `storm::fk<>`,
> `storm::fk<storm::RefAction::Cascade>`, `storm::many_to_many<>`, `storm::reverse_fk<...>`.
> The longer `storm::meta::` spelling (`storm::meta::fk<>`, …) still works unchanged — the
> re-exports are purely additive. Examples in this document use the short `storm::` form.

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
| `uint64_t` | *annotation-gated* | see below | see below |
| `unsigned long` | *annotation-gated* | see below | see below |
| `unsigned long long` | *annotation-gated* | see below | see below |

> ⚠️ **64-bit unsigned fields require an explicit storage annotation (#436).**
> Neither SQLite nor PostgreSQL has an unsigned 64-bit integer type, so a bare
> `uint64_t` / `unsigned long` / `unsigned long long` field is a **compile-time
> error**. Annotate the field with exactly one of:
>
> | Annotation | Column type | Range with correct ordering | Cost |
> |---|---|---|---|
> | `storm::signed_storage` | `INTEGER` (SQLite) / `BIGINT` (PG) | `0 .. INT64_MAX` (2⁶³−1) | none — same `bind_int64`/`extract_int64` hot path |
> | `storm::full_unsigned` | zero-padded 20-char `TEXT` (SQLite) / `NUMERIC(20,0)` (PG) | `0 .. 2⁶⁴−1` (full range) | slower string bind/extract, wider column |
>
> **`signed_storage`** keeps today's behavior. It is byte-identical to a plain
> signed 8-byte column and routes through the same `bind_int64`/`extract_int64`
> calls, so there is **zero performance change**. Use it when the values are
> always ≤ `INT64_MAX`. **Caveat:** a value > `INT64_MAX` is stored as a negative
> `int64` — equality still round-trips (the read casts the signed bits back), but
> `ORDER BY` / `>` / `<` / `BETWEEN` sort by the signed interpretation (so `2⁶³+1`
> sorts *before* `1`) and any external reader (raw SQL, a BI tool) sees a negative
> number. Pinned by `Uint64SignedStorageTest` in `tests/schema/test_types.cpp`.
>
> **`full_unsigned`** stores the value order-preservingly: a 20-character
> zero-padded decimal string on SQLite (`uint64_max` = `18446744073709551615` is
> 20 digits, so lexicographic order == numeric order) and `NUMERIC(20,0)` on
> PostgreSQL. The **full `0 .. 2⁶⁴−1` range round-trips for equality AND sorts
> correctly**, and external readers see the true unsigned value. It pays a string
> bind/extract on every access, so reach for it only when you actually need values
> above `INT64_MAX`. Pinned by `Uint64FullUnsignedTest` in the same file.
>
> Signed 64-bit types (`int64_t` / `long` / `long long`) and all smaller integer
> types are unaffected — no annotation needed.

**Usage (64-bit unsigned):**
```cpp
struct Account {
    [[=storm::primary]] int id;
    [[=storm::signed_storage]] std::uint64_t small_id;   // ≤ INT64_MAX
    [[=storm::full_unsigned]]  std::uint64_t full_range; // 0 .. 2⁶⁴−1
};
```

**Usage:**
```cpp
struct Example {
    [[=storm::primary]] int id;
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
    [[=storm::primary]] int id;
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
    [[=storm::primary]] int id;
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
    [[=storm::primary]] int id;
    std::string title;
    std::string content;
};

// All these work for binding:
Document doc1{0, "Title", "Content"};
Document doc2{0, std::string("Title"), std::string("Content")};
std::string_view title_view = "Title";
Document doc3{0, std::string(title_view), "Content"};
```

### Bounded length (`max_length<N>`) (#493)

Annotate a text field with `max_length<N>` to bound its length **in the database**:

```cpp
struct Account {
    [[=storm::primary]] int id;
    [[=storm::max_length<50>]] std::string name;              // NOT NULL, ≤ 50
    [[=storm::max_length<20>]] std::optional<std::string> tag; // nullable, ≤ 20
};
```

| Field | PostgreSQL | SQLite |
|-------|-----------|--------|
| `std::string name` + `max_length<50>` | `name VARCHAR(50) NOT NULL` | `name TEXT NOT NULL CHECK(length(name) <= 50)` |
| `std::optional<std::string> tag` + `max_length<20>` | `tag VARCHAR(20)` | `tag TEXT CHECK(length(tag) <= 20)` |

**Both dialects genuinely enforce the bound** on every write path — Storm's own
INSERT/UPDATE/upsert *and* any raw SQL. This is stronger than Django
(`CharField(max_length=…)`) and SQLAlchemy (`String(…)`), which emit `varchar(N)` that
**SQLite silently ignores**; they rely on an optional app-layer validator. Storm has no
app layer, so it emits a real `CHECK` on SQLite — nothing to remember or bypass.

- **Nullable + bounded**: the SQLite `CHECK` passes when the value is NULL (standard SQL),
  so a `std::optional<std::string>` column allows NULL and still rejects over-limit values.
- **Combines with** `unique` (appended after the CHECK/VARCHAR), a C++ default-member
  initializer (`#413`, DEFAULT precedes the CHECK on SQLite), and `indexed` (orthogonal).
  SQLite regular-field order: `<name> TEXT [NOT NULL] [DEFAULT <v>] [CHECK(length(<name>) <= N)] [UNIQUE]`.
- **Type guard**: `max_length<N>` on a non-text field is a **compile error** at the model
  boundary (`ModelMaxLengthValid<T>`), consistent with the bare-`uint64` hard error — only
  `std::string` / `std::string_view` / `std::optional<those>` accept it.

Out of scope (YAGNI): `min_length` (separate follow-up — `CHECK(length >= N)` passes on NULL,
so it does *not* mean "required") and `check<"expr">` (raw-SQL escape hatch).

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
    [[=storm::primary]] int id;
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
    [[=storm::primary]] int id;
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
    [[=storm::primary]] int id;
    std::string name;
    [[=storm::auto_create]] std::chrono::system_clock::time_point created_at;
    [[=storm::auto_update]] std::chrono::system_clock::time_point updated_at;
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

## Many-to-Many Container Fields (`many_to_many<>` / `many_to_many_through`)

A container member annotated with `[[= storm::many_to_many<>]]` (or
`many_to_many_through<JunctionModel>`) declares a many-to-many relationship (#203):

```cpp
struct Student {
    [[= storm::primary]] int id{};
    std::string name;
    [[= storm::many_to_many<>]] std::vector<Course> courses;
};
```

- **Not a column.** The member maps to a junction table, not to a column —
  `INSERT`/`SELECT`/`UPDATE`/schema generation skip it entirely. Plain `select()`
  leaves it empty; `join<^^Student::courses>()` eager-loads it.
- **Supported containers:** `std::vector<T>`, `plf::hive<T>`, `std::deque<T>`,
  and smart-pointer elements (`std::vector<std::shared_ptr<T>>`); the related
  model type is extracted via C++26 `std::meta`.
- **Junction DDL** is auto-generated for the `many_to_many<>` form (see
  [JOIN_OPERATIONS.md](../features/JOIN_OPERATIONS.md#many-to-many-joins-203)).

## Persistable vs. Filterable

Every type on this page is **persistable and readable** — you can `INSERT` it and `SELECT`
it back. A **narrower** set is **filterable in a WHERE clause** (`f<^^T::field>() == …`,
`.between(…)`, `.in(…)`): the expression system stores operands in a closed variant, so a
type must have a variant arm to be filtered on (#407). Temporal types
(`year_month_day`, `system_clock::time_point`) and `storm::UUID` are both persistable **and**
filterable; `std::chrono::duration`, `std::filesystem::path`, and BLOB are persistable but
**not** filterable. See the full filterability table in
[WHERE_CLAUSES.md → Filterable field types](../features/WHERE_CLAUSES.md#filterable-field-types).

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

### `BindableType` concept (#473)

The parameter binder (`bind_parameter_value`) and the WHERE operand path
(`normalize_operand`) are both constrained by
`storm::orm::utilities::BindableType<T>` — a concept whose disjunction mirrors the
`if constexpr` dispatch arms exactly (integer/64-bit-integer, `enum`, `bool`,
`double`/`float`, chrono date/datetime/duration, `filesystem::path`, `UUID`, text,
BLOB byte-vectors, and `std::optional<U>` where `U` is itself bindable). Passing an
unsupported operand type now fails at the call site with a clear constraint
violation instead of deep inside the binder's dispatch. Composed from the same
`is_*_source_v` predicates the dispatcher branches on, so the concept cannot drift
from what actually binds.

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
    id INTEGER PRIMARY KEY,  -- AUTOINCREMENT is opt-in (#379): storm::primary_autoincrement
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

## Model Type Validation (`Entity`) (#472)

`storm::meta::Entity<T>` is the compile-time **structural** gate for model types — true iff
`T` is a reflectable class:

```cpp
template <typename T>
concept Entity = std::meta::is_class_type(^^T) && requires {
    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
    std::meta::identifier_of(^^T);
};
```

`QuerySet<T>` and `BaseStatement<T>` (and therefore every statement class) `require` it, so a
non-model `T` — `int`, a pointer, a function type — fails at this named boundary with a clear
constraint violation instead of deep inside reflection-based code.

`Entity` sits **above** and stays **separate from** the semantic model concepts on
`BaseStatement<T>` — `ModelWithPrimaryKey`, `ModelStorageAnnotated`, `ModelFkPoliciesValid`.
Those check model *policy* (a primary key exists, every `uint64_t` field is storage-annotated,
every `SET NULL` FK is nullable); `Entity` checks *reflectability*. They are deliberately not
merged: `QuerySet<T>` must be usable without requiring a primary key, so it needs the structural
gate alone.

`Entity` is structural only — a `union` also satisfies it. Full model-ness is guaranteed by the
semantic concepts together with `Entity`, not by `Entity` alone.

## Field Selector Validation (`ValidFieldInfo`) (#478)

`storm::meta::ValidFieldInfo<MemberInfo>` is the compile-time gate that a `std::meta::info`
**NTTP** names a real field — a non-static data member that has an identifier:

```cpp
template <std::meta::info MemberInfo>
concept ValidFieldInfo =
    std::meta::is_nonstatic_data_member(MemberInfo) && std::meta::has_identifier(MemberInfo);
```

The field selector `f<^^Model::member>()` and its `Field` / `CollatedField` proxies `require` it,
so a selector that is not a field — a static member, a member function, a whole-type reflection
such as `^^Person`, or a scalar like `^^int` — fails at this named boundary instead of deep inside
`identifier_of` / `type_of`. Both requirements are load-bearing: on the clang-p2996 build,
`is_nonstatic_data_member(^^int)` and `is_nonstatic_data_member(^^Type::static_member)` are both
`false`, so the concept genuinely rejects (contrast `Entity`, whose structural requirements alone
accept `int` and rely on `is_class_type` to gate).

`ValidFieldInfo` is orthogonal to `Entity` (which gates the whole model *type*) and to the `f<>`
call site's extra `!is_relation_field(MemberInfo)` check, which excludes `many_to_many` /
`reverse_fk` container members that are not persisted columns — that check is still ANDed alongside
`ValidFieldInfo` at each selector site.
