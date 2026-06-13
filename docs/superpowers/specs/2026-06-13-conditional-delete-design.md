# Conditional bulk DELETE (#198) — Design

**Issue:** [#198](https://github.com/spiritEcosse/storm/issues/198) — "[API] Add conditional UPDATE and DELETE operations"
**Date:** 2026-06-13
**Scope:** DELETE only. Conditional UPDATE is deferred to a follow-up issue.

## Goal

Add a conditional bulk DELETE so users can delete all rows matching a WHERE filter
in a single SQL statement, instead of iterating and deleting row-by-row:

```cpp
// Before (workaround): N individual DELETEs
for (auto& p : qs.where(f<^^Person::age>() > 30).select())
    qs.erase(p);

// After: one DELETE FROM ... WHERE ...
qs.where(f<^^Person::age>() > 30).erase().execute();
```

## Decisions (locked with user 2026-06-13)

| Question | Decision | Rationale |
|---|---|---|
| Scope | **DELETE only**; UPDATE deferred | Conditional UPDATE needs a new SET-assignment DSL (`f<^^Person::salary>() = 50000`) — a separate, larger design. DELETE reuses existing WHERE machinery cleanly. |
| Method name | **`erase()` no-arg** on a filtered QuerySet | Fits the existing DELETE family: `erase(obj)`, `erase(span)`, `erase_all()`. `delete` is a C++ keyword. |
| Empty WHERE | **Runtime error** (`std::unexpected`) | `erase()` with no `where()` would emit `DELETE FROM table` — a full-table wipe. Refuse it; `erase_all()` stays the explicit wipe. Safe-by-default. |
| Why not compile-time block | Rejected | Type-state (`bool HasFilter_` template param) would touch ~15-20 signatures in `queryset.cppm`, and the filter is a runtime `shared_ptr` anyway, so it can't be airtight. Not worth the plumbing. |

## API

```cpp
template <class T, DatabaseConnection ConnType, bool Finalized_>
class QuerySet {
    // NEW: conditional bulk DELETE — deletes rows matching the current where().
    // Returns a proxy with .execute() -> std::expected<void, Error> and .to_sql().
    [[nodiscard]] auto erase();   // no-arg overload (alongside erase(obj), erase(span))
};
```

- `qs.where(cond).erase().execute()` → `std::expected<void, Error>`
- `qs.erase().execute()` with **no** `where()` → `std::unexpected(Error{-1, "erase() requires a WHERE clause; use erase_all() to delete all rows"})`
- `qs.where(cond).erase().to_sql()` → `std::expected<std::string, Error>` (expanded SQL for debugging)

Affected-row count is **not** returned (consistent with existing `erase()`/`erase_all()`, which
return `std::expected<void, Error>`). The issue floated `size_t`, but the existing DELETE family
returns void; matching it keeps the API consistent. A row-count variant can be a later addition.

LIMIT / OFFSET / ORDER BY are **not** applied to the DELETE (SQLite builds without
`SQLITE_ENABLE_UPDATE_DELETE_LIMIT` by default). Out of scope.

## Components

All changes live in two existing files — no new modules.

### 1. `EraseStatement` (`src/orm/statements/erase.cppm`)

Add a conditional-delete path that mirrors how `SelectStatement` consumes `where_expr_`:

```
build_conditional_delete_sql(where_expr)   // "DELETE FROM <table> WHERE " + where::to_sql(*expr)
   -> conn_->prepare_cached(sql)
   -> Base::bind_where_params<Statement, Error>(stmt, where_expr)
   -> stmt->execute()
```

New members:
- `query_where(ExpressionVariantPtr)` → returns a `ConditionalDeleteQuery` proxy.
- `ConditionalDeleteQuery { execute(), to_sql() }` proxy struct (same shape as the existing
  `SingleQuery`/`BulkQuery`/`DeleteAllQuery` proxies).
- `execute_where(where_expr)` / `to_sql_where(where_expr)` — guard empty `where_expr` first
  (`if (!where_expr) return std::unexpected(Error{-1, "..."})`), else build SQL + bind + run.

The `DELETE FROM <table> WHERE ` prefix reuses the existing `append_delete_prefix` /
`delete_prefix_size` helpers where possible (they already spell `DELETE FROM <table> WHERE`).
The WHERE *body* is dynamic (runtime `to_sql`), so the SQL string is assembled at runtime like
SELECT's dynamic path — not a compile-time `ConstexprString`.

### 2. `QuerySet::erase()` (`src/orm/queryset.cppm`)

```cpp
// Conditional bulk DELETE — deletes rows matching the current where() filter.
[[nodiscard]] auto erase() {
    return orm::statements::EraseStatement<T, ConnType>(conn_).query_where(where_expr_);
}
```

Sits next to the existing `erase(obj)` / `erase(span)` overloads. Overload resolution is
unambiguous: no-arg vs. `const T&` vs. `std::span<const T>`.

## Data flow

```
qs.where(cond)            // populates where_expr_ (shared_ptr<ExpressionVariant>)
   .erase()               // EraseStatement.query_where(where_expr_) -> ConditionalDeleteQuery
   .execute()             // guard empty -> build SQL -> prepare_cached -> bind_where_params -> execute
```

One SQL statement; atomic in SQLite (single implicit transaction).

## Error handling

- Empty `where_expr_` → `std::unexpected(Error{-1, "erase() requires a WHERE clause; use erase_all() to delete all rows"})`, returned **before** any DB call. Same `Error{int,string}` shape on SQLite and PostgreSQL.
- prepare / bind / step failures → `std::unexpected(Error)` propagated, statement reset on failure (existing `bind_where_params` already resets).

## Testing (TDD — written and failing before implementation)

New test file or additions following `test_orm_*.cpp` TYPED_TEST patterns (SQLite + PostgreSQL).

**Filter coverage** (per the Thorough Testing Checklist):
- All 6 comparison operators: `==`, `!=`, `>`, `>=`, `<`, `<=`
- Special expressions: `IN`, `BETWEEN`, `LIKE`, `IS NULL` / `IS NOT NULL`
- Logical combinations: `AND`, `OR`, nested `(A && B) || C`
- Types: int, string, double

**Result coverage:**
- Rows match → only those rows deleted, others remain (verify via re-SELECT count)
- Empty result (filter matches nothing) → succeeds, no rows deleted
- Single-row match
- Large set (100+ rows) deleted in one statement

**Safety / error paths:**
- `qs.erase()` with **no** `where()` → `std::unexpected`, table untouched (re-SELECT confirms)
- prepare/bind failure via the mock-error pattern (`test_orm_mock_errors.cpp`)

**SQL shape:**
- `to_sql()` returns `DELETE FROM <table> WHERE ...` with bound values inlined

**Chaining:**
- `where().where().erase()` (AND-combined filter) deletes the intersection

## Out of scope (follow-up)

- Conditional bulk **UPDATE** (`qs.where(cond).update(fields)`) — needs a SET-assignment DSL.
  File a follow-up issue.
- Affected-row count return type.
- `DELETE ... LIMIT` / `ORDER BY`.
