# PostgreSQL Binary Result Format (#600)

Storm's PostgreSQL backend automatically requests binary result format from libpq for SELECT-shaped queries, decoding network-byte-order bytes directly instead of parsing ASCII text. This optimization is **transparent to users** — the ORM computes at compile time whether all columns a statement returns are "binary-safe," and only then requests binary format; if even one column isn't safe, the whole statement stays on text, the same as today.

**Measured performance gain** (isolated libpq microbenchmark, #600): **34% faster** extraction (66.4% of text's time) on bulk SELECT with numeric columns, isolating conversion cost from network round-trip. Exact impact inside Storm's own Release-build extraction path is pending benchmark harness expansion (#601).

## Design Constraint: Per-Statement Format

libpq's result-format flag (`PQexecPrepared`'s last argument) is **per statement, not per column**. This drives the entire design: Storm must classify every column a statement returns, and only request binary format if ALL are decodable as raw bytes.

```cpp
// Three columns in one SELECT: all must be binary-safe or none is used
// (A model with one unsafe column → all columns extracted as text)
SELECT int_col, string_col, date_col FROM MyTable
```

This is a correctness constraint — selecting binary format when even one column can't be safely decoded would silently corrupt results.

## Safe and Unsafe Type Sets

### Safe for Binary Format

These types are decoded as a direct byte reinterpretation (`memcpy` + `std::byteswap` + `bit_cast` where needed):

| C++ Type | Annotation | PG Storage | Binary Decode |
|---|---|---|---|
| `bool` | (none) | `BOOLEAN` | Single byte, `0x00`/`0x01` |
| `int`, `short`, `char`, unsigned variants, ... | (none) | `BIGINT` | Network-byte-order int, width read from the server (see below) |
| `int64_t`, `long`, `long long` | (none) | `BIGINT` | Same as above |
| `double` | (none) | `DOUBLE PRECISION` | 8-byte IEEE 754 double |
| `float` | (none) | `REAL` | 4-byte IEEE 754 single |
| `std::string` | (none) | `TEXT` or `VARCHAR(N)` | UTF-8 bytes; text format also works |
| `std::filesystem::path` | (none) | `TEXT` | UTF-8 bytes |
| `std::vector<uint8_t>` | (none) | `BYTEA` | Raw bytes |
| `std::optional<T>` | (none) | NULL + above | `is_null()` checked first |

All integer-stored and int64-stored C++ types benefit — this includes `signed char`, `unsigned char`, `short`, `unsigned short`, `unsigned int`, `unsigned long` and `std::byte`.

**Every C++ integer field maps to PG `BIGINT`, regardless of its own width** (`schema.cppm`'s `integer_type<D>()` returns `BIGINT` for every integer `StorageClass`, never `INTEGER`/`SMALLINT`). So `extract_int()` — the extractor for 32-bit-and-smaller C++ types — routinely faces an 8-byte `int8` value on the wire. The binary decoder therefore dispatches on the **server-reported byte width** (`PQgetlength`), not a width implied by the caller's C++ type: a fixed 4-byte read there would silently return the high half of the real value (zero for every small positive number, which looks entirely plausible). See `decode_binary_int` below.

### Unsafe (Excluded Even If Mechanically Possible)

| C++ Type | Annotation | Reason |
|---|---|---|
| `std::chrono::year_month_day` | (none) | Binary form is int32 days since 2000-01-01; text form is `YYYY-MM-DD` string |
| `std::chrono::system_clock::time_point` | (none) | Binary form is int64 microseconds since 2000-01-01; text form is ISO 8601 string |
| `storm::UUID` | (none) | Binary form is 16 raw bytes; text form is 36-char dashed string |
| `std::uint64_t` | `storm::full_unsigned` | Binary form is variable-length base-10000 numeric; text form is decimal string (for order-preserving full-range storage) |
| FK members | (none) | Binary form is the TARGET's primary-key type, not the member's own C++ type; classifying composite and multi-column FK targets deferred |

**Why these are excluded**: Phase 1's scope is column types decodable as byte-for-byte copies. Chrono types and UUID require special parsing of network-byte-order structures with magnitude semantics; `full_unsigned` and FK members require per-member or per-target logic that's separate from C++ type classification. Each is a straightforward extension — deferred as future work, not a correctness issue.

## Compile-Time Classification

### Per-Model Row Classification

```cpp
// src/orm/statements/base.cppm
static constexpr bool pg_binary_safe_row_ = pg_binary_safe_row_impl(
    std::make_index_sequence<field_count_>{});
```

`BaseStatement<T>` computes `pg_binary_safe_row_` at compile time, walking every member:

1. **Per-member check** (`member_pg_binary_safe<Member>`):
   - If FK field → false
   - Else if has `storm::full_unsigned` annotation → false
   - Else check C++ type via `ColumnExtractor::is_pg_binary_safe_column_v<FieldType>`

2. **Whole-row AND**: all members must be safe

3. **Result**: compile-time `bool` constant

### Per-Column Type Classification

```cpp
// src/orm/statements/extract.cppm
template <typename FT>
static constexpr bool is_pg_binary_safe_v =
        std::is_same_v<FT, bool> || is_int_stored_v<FT> || is_int64_stored_v<FT> ||
        is_floating_stored_v<FT> || is_blob_stored_v<FT> || std::is_same_v<FT, std::string> ||
        std::is_same_v<FT, std::filesystem::path>;

template <typename FT>
static constexpr bool is_pg_binary_safe_column_v = is_pg_binary_safe_v<utilities::optional_inner_type_t<FT>>;
```

For optional types, the **inner type** is classified — `is_null()` is already format-independent, so `std::optional<safe_type>` is safe.

## Activation Points

Binary format is requested **only for whole-model extraction** (all columns of `T` matter):

- ✅ `select()` — base-entity extraction
- ✅ `first()` / `get()` — single-row fast paths
- ✅ `rows()` generator — streaming results
- ✅ m2m/reverse-FK eager-load **Q1 only** (base entities) — uses base model's classification
- ✅ `union_` / `except_` / `intersect_` (set operations) — use base model's classification

For **projection queries** (only some columns selected):

- ✅ `values<>()` — independent classification of only the projected columns (a model's unsafe column doesn't block its safe columns in a projection)
- ✅ `distinct<>()` — same as `values<>`

### Deliberately NOT Wired (Correctness, Not Laziness)

**Aggregates** (`count()`, `sum()`, `avg()`, `min()`, `max()`): Verified against a live PG server — the wire types differ from the C++ extraction types. Example on an integer column:

| Aggregate | Wire Type | ORM Extracts As | Issue |
|---|---|---|---|
| `SUM(int_col)` | `numeric` (not `bigint`) | `int64_t` | Binary decode would silently return 0 |
| `AVG(int_col)` | `numeric` (not `double`) | `std::optional<double>` | Binary decode would silently return 0 |
| `MIN(int_col)` | `bigint` | `std::optional<double>` | Type mismatch |
| `MAX(int_col)` | `bigint` | `std::optional<double>` | Type mismatch |

Wiring this naively would silently corrupt results — needs per-operator, PG-OID-aware decoding. Deferred as separate future work (#601 or later).

**RETURNING-id paths** (`insert()`/`update()`/`erase()` returning the inserted/updated ID):
- Only one column per statement
- Low payoff vs mock-fixture churn
- Deferred, not a correctness issue — can be added later without breaking change

**m2m/reverse-FK eager-load Q2** (related/owner entities):
- Q2 hits a different model's table
- That model's classification doesn't span back to the base model's context
- Must track separately — not in scope of Phase 1

## PG-Only Activation via Concept Probing

The feature is genuinely PG-only; SQLite already extracts binary natively via its `sqlite3_column_*` API. The call sites use `if constexpr` and a concept probe:

```cpp
// src/orm/statements/base.cppm
template <typename Statement>
__attribute__((always_inline)) static auto request_binary_results(Statement* stmt, bool binary) noexcept -> void {
    if constexpr (requires { stmt->set_result_binary(bool{}); }) {
        stmt->set_result_binary(binary);
    }
}
```

SQLite's `Statement` has no `set_result_binary` method; the `requires` check is `false`, and the call compiles away entirely. PostgreSQL's `Statement` has the method, the check is `true`, and the flag is set.

This pattern means:
- No changes to generic `BaseStatement<T>` or `db::DatabaseStatement` concepts
- No conditional compilation tags
- Zero runtime cost on SQLite

## Implementation Details

### PostgreSQL Statement Wiring

```cpp
// src/db/postgresql_statement.cppm
class Statement {
    bool binary_results_ = false;

    auto set_result_binary(bool binary) noexcept -> void {
        binary_results_ = binary;
    }

    auto execute() -> std::expected<void, Error> {
        // ...
        result_ = PQexecPrepared(conn_, stmt_name_.c_str(), /* ... */,
                                  binary_results_ ? 1 : 0);  // #600
        // ...
    }
};
```

`step()` calls `execute()` lazily on its first invocation if the statement hasn't run yet, so the flag must be set before the first `step()`, not before `execute()` specifically — callers set it right after `prepare_cached()`/`prepare()` returns. The flag is cleared on `reset()` (cache hit) so a reused statement doesn't carry stale format from a previous caller.

### Column Extraction Dispatch

Text extraction is unchanged — it's the same `strtoll`/`strtod`/`val[0]=='t'` parsing it always was. Binary extraction branches in the hot path, and for integers/floats dispatches on the **server-reported byte width**, not the caller's C++ type width:

```cpp
// src/db/postgresql_statement.cppm (simplified — see the real decode_binary_int/
// decode_binary_floating/load_network for the full width-dispatch + byteswap)
auto extract_int64(int col_idx) const noexcept -> int64_t {
    if (binary_results_) {
        return decode_binary_int(col_idx);  // switches on PQgetlength: 8/4/2 bytes
    }
    const char* val = PQgetvalue(result_, current_row_, col_idx);
    return strtoll(val, nullptr, 10);
}
```

All extraction methods branch this way — bool, int, int64, float, double, and blob (string is already raw bytes in both formats, no branch needed).

## Performance

### Isolated Libpq Microbenchmark

Original issue #600 measured ~34% faster extraction on 50,000 rows × 3 numeric columns (two `int64_t`, one `double`), isolating the conversion cost from network round-trip.

- Text format: `strtoll` + `strtod` parsing
- Binary format: `memcpy` + `std::byteswap`

**Exact speedup inside Storm's Release-build extraction path**: pending #601 (benchmark harness doesn't yet support PG).

### No SQLite Regression

Verified via A/B benchmark: SQLite performance is unchanged (max delta 0.38%, well under 2.8% noise floor across 5 interleaved runs). The `if constexpr (requires {...})` probe compiles the entire feature away on SQLite.

## Cross-Backend Binary Float Decode Precision

**Interesting side effect on PostgreSQL < 12** (#600 design notes): Pre-PG-12, the server-side `extra_float_digits` defaults to 0, so text-format `float8` output is truncated to 15 significant digits. Text-format extraction via `strtod` cannot always recover the exact original bit pattern.

The binary path `bit_cast`s the wire bytes directly, so it's **always exact** — this is more precise than text on old servers. This actually narrows the gap with SQLite (whose `sqlite3_column_double` was already exact) rather than widening divergence.

## See Also

- [SELECT Queries](../../guide/features/SELECT_QUERIES.md) - User-facing SELECT guide (note on binary format)
- [Statement Caching](STATEMENT_CACHING.md) - How statements are cached and reused
- [SQL Generation](SQL_GENERATION.md) - Compile-time SQL generation
- GitHub issue [#600](https://github.com/spiritEcosse/storm/issues/600) - Original issue and phase scope
- GitHub issue [#601](https://github.com/spiritEcosse/storm/issues/601) - Benchmark harness expansion for PG

