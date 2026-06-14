# Conditional bulk UPDATE (#403) — Design

**Issue:** [#403](https://github.com/spiritEcosse/storm/issues/403) — "[API] Conditional bulk UPDATE — qs.where(cond).update(fields)"
**Date:** 2026-06-14
**Follow-up to:** [#198](https://github.com/spiritEcosse/storm/issues/198) / PR #402 (conditional bulk DELETE — `qs.where(cond).erase()`).
**Scope:** Conditional bulk UPDATE only. Reuses the WHERE machinery already built for the conditional DELETE.

## Goal

Update every row matching a WHERE filter in one statement, instead of SELECT-then-loop:

```cpp
// Before (workaround): SELECT matching rows, mutate each, UPDATE row-by-row
for (auto& p : qs.where(f<^^Person::salary>() < 50000).select()) {
    p.salary = 60000;
    qs.update(p);
}

// After: one UPDATE ... SET ... WHERE ...
qs.where(f<^^Person::salary>() < 50000)
  .update<^^Person::salary, ^^Person::is_active>(Person{.salary=60000, .is_active=true})
  .execute();
```

Generated SQL: `UPDATE person SET salary=?, is_active=? WHERE salary<?`.

## Decisions (locked with user 2026-06-14)

| Question | Decision | Rationale |
|---|---|---|
| SET-assignment DSL | **Partial-struct + member NTTPs**: `update<^^Person::salary, ^^Person::is_active>(Person{.salary=60000, .is_active=true})` | SET column list is known at compile time (so the SET clause is a `ConstexprString` keyed on `<T, Members...>`, like single-row UPDATE). Values are read from the listed members of a prototype object. Rejected: `set(f<>(), value)` varargs (more runtime machinery for no win here); column-relative `salary = salary + 1000` (separate, larger design). |
| Return type | **`std::expected<void, Error>`** | Matches the whole CRUD family (`update(obj)`, `erase()`, `erase_all()` all return void). Affected-row count was floated in the issue but would make this the first CRUD op to return a count — deferred. |
| Empty WHERE | **Runtime refusal** (`std::unexpected`) before any DB call | `update<...>(proto)` with no `where()` would emit `UPDATE table SET ...` — a full-table write. Refuse it, identical to `erase()`. `update_all()` is a possible follow-up, not in scope. |
| `auto_update` timestamps | **Auto-include**: if the model has an `auto_update` `time_point` field, append it to the SET clause (stamped `now()`) even when not listed | Matches single-row UPDATE semantics (#209) — `updated_at` stays correct. Caller never has to remember to list it. A listed member that *is* the `auto_update` field is deduplicated (not emitted twice). |
| Why not compile-time empty-WHERE block | Rejected (same as DELETE) | The filter is a runtime `shared_ptr<ExpressionVariant>`; type-state can't be airtight and would touch ~15-20 `queryset.cppm` signatures. Not worth the plumbing. |

## API

```cpp
template <class T, DatabaseConnection ConnType, bool Finalized_>
class QuerySet {
    // Existing (unchanged):
    //   auto update(const U& obj);              // single-row, non-templated
    //   auto update(std::span<const T> objects); // bulk, non-templated

    // NEW: conditional bulk UPDATE — updates rows matching the current where().
    // SET columns are the explicit Members... NTTPs; values come from `proto`.
    // Returns a proxy with .execute() -> std::expected<void, Error> and .to_sql().
    template <std::meta::info... Members>
        requires (sizeof...(Members) > 0)
    [[nodiscard]] auto update(const T& proto [[clang::lifetimebound]]);
};
```

- `qs.where(cond).update<^^Person::salary>(proto).execute()` → `std::expected<void, Error>`
- `qs.update<^^Person::salary>(proto).execute()` with **no** `where()` → `std::unexpected(Error{-1, "update() requires a WHERE clause; use update_all() to update all rows"})`
- `qs.where(cond).update<^^Person::salary>(proto).to_sql()` → `std::expected<std::string, Error>` (expanded SQL for debugging)

**Overload resolution.** The new `update` is templated and requires `sizeof...(Members) > 0`. When the caller writes `update<^^...>(obj)`, explicit template arguments select it unambiguously; a plain `update(obj)` / `update(span)` still selects the existing non-templated overloads. The `requires` guard ensures the conditional form always carries at least one SET column.

**Member validation.** Each `Members...` entry must be a non-static data member of `T` (same constraint `f<>()` enforces). A primary-key member in the SET list is rejected (`requires`/`static_assert`) — you cannot SET the PK in a conditional UPDATE.

LIMIT / OFFSET / ORDER BY are **not** applied (SQLite builds without `SQLITE_ENABLE_UPDATE_DELETE_LIMIT` by default). Out of scope, same as conditional DELETE.

## Components

All changes live in two existing files — no new modules.

### 1. `UpdateStatement` (`src/orm/statements/update.cppm`)

Add a conditional-update path that mirrors `EraseStatement::ConditionalDeleteQuery`.

**Compile-time SET clause** (keyed on the `Members...` pack):
```
build_conditional_set_clause<Members...>()
  -> "<name1>=?, <name2>=?[, <auto_update_name>=?]"
```
- For each listed member: emit `identifier_of(member)` + `=?`, or `identifier_of(member)` + `_id=?` if it is an FK field (reuse the FK-detection branch already in `build_field_assignments`).
- Append any `auto_update` `time_point` field of `T` that is **not already** in the listed pack.
- Dedup: a listed member that is itself the `auto_update` field is emitted once (as a normal listed member, stamped `now()` at bind time).

**Runtime SQL assembly** (the WHERE body is dynamic):
```
build_conditional_update_sql<Members...>(where_expr)
  = "UPDATE <table> SET " + set_clause<Members...>  (compile-time ConstexprString)
  + " WHERE " + where::to_sql(*where_expr)            (runtime)
```

**Binding order** (SET params first, then WHERE — per the issue):
```
int param_index = 1;
// SET: bind each listed member from `proto`, advancing param_index
//   - reuse the per-member binder (bind_value_by_type / the auto_update now() path
//     from bind_field_at_index) so FK/_id and timestamp semantics match single-row UPDATE
// auto_update field (if auto-appended): bind now()
// WHERE: continue from the same param_index via bind_expr_or_reset(stmt, where_expr, param_index)
```

New members of `UpdateStatement`:
- `query_where<Members...>(const T& proto, ExpressionVariantPtr where_expr)` → returns a `ConditionalUpdateQuery` proxy.
- `ConditionalUpdateQuery { execute(), to_sql() }` proxy struct (same shape as `EraseStatement::ConditionalDeleteQuery`). Holds the proto by `const T&` (lifetime contract — see below) and the `where_expr` `shared_ptr`.
- `execute_where<Members...>(proto, where_expr)` / `to_sql_where<Members...>(proto, where_expr)` — guard empty `where_expr` first, else build SQL + bind + run.
- `empty_where_error()` — `std::unexpected(Error{-1, "update() requires a WHERE clause; use update_all() to update all rows"})`, identical pattern to `EraseStatement`.
- `ready_conditional_update<Members...>(proto, where_expr)` — empty-WHERE check + `prepare_cached` + SET-then-WHERE bind, shared by `execute_where` / `to_sql_where` (mirrors `ready_conditional_delete`).

**Lifetime.** The proto is the source of the SET values. The proxy holds `const T&` with `[[clang::lifetimebound]]` (same contract as `update(obj)`'s `SingleQuery`): catches inline-temporary misuse at compile time; keep the proto alive until the terminal `.execute()`/`.to_sql()` call.

### 2. `QuerySet::update<Members...>` (`src/orm/queryset.cppm`)

```cpp
// Conditional bulk UPDATE — updates rows matching the current where() filter.
// SET columns are the Members... NTTPs; values come from `proto`.
template <std::meta::info... Members>
    requires (sizeof...(Members) > 0)
[[nodiscard]] auto update(const T& proto [[clang::lifetimebound]]) {
    return orm::statements::UpdateStatement<T, ConnType>(conn_)
        .template query_where<Members...>(proto, where_expr_);
}
```

Sits next to the existing `update(obj)` / `update(span)` overloads.

## Data flow

```
qs.where(cond)                          // populates where_expr_ (shared_ptr<ExpressionVariant>)
   .update<^^salary, ^^is_active>(proto) // UpdateStatement.query_where<...>(proto, where_expr_)
                                          //   -> ConditionalUpdateQuery
   .execute()                            // guard empty -> build SQL (SET compile-time + WHERE runtime)
                                          //   -> prepare_cached -> bind SET then WHERE -> execute
```

One SQL statement; atomic in SQLite (single implicit transaction).

## Error handling

- Empty `where_expr_` → `std::unexpected(Error{-1, "update() requires a WHERE clause; use update_all() to update all rows"})`, returned **before** any DB call. Same `Error{int,string}` shape on SQLite and PostgreSQL.
- prepare / bind / step failures → `std::unexpected(Error)` propagated; statement reset on bind failure (the shared `bind_expr_or_reset` already resets).

## Testing (TDD — written and failing before implementation)

Additions following `test_orm_*.cpp` TYPED_TEST patterns (SQLite + PostgreSQL).

**SET coverage:**
- Single SET column
- Multiple SET columns
- SET column of each supported type: int, string, double, bool
- FK SET column (verify `<name>_id=?` emitted)
- `auto_update` field auto-included (verify SQL contains `updated_at=?` and the value is refreshed); model with NO timestamp field (verify no extra column appears)
- Listed member that IS the `auto_update` field → emitted once, not duplicated

**WHERE / filter coverage** (per the Thorough Testing Checklist):
- All 6 comparison operators: `==`, `!=`, `>`, `>=`, `<`, `<=`
- Special expressions: `IN`, `BETWEEN`, `LIKE`, `IS NULL` / `IS NOT NULL`
- Logical combinations: `AND`, `OR`, nested `(A && B) || C`
- WHERE types: int, string, double

**Result coverage:**
- Rows match → only those rows get the new values, others unchanged (verify via re-SELECT)
- Empty result (filter matches nothing) → succeeds, no rows changed
- Single-row match
- Large set (100+ rows) updated in one statement

**Safety / error paths:**
- `qs.update<...>(proto)` with **no** `where()` → `std::unexpected`, table untouched (re-SELECT confirms)
- prepare/bind failure via the mock-error pattern (`test_orm_mock_errors.cpp`)

**SQL shape:**
- `to_sql()` returns `UPDATE <table> SET <cols> WHERE ...` with bound values inlined

**Chaining:**
- `where().where().update<...>(proto)` (AND-combined filter) updates only the intersection

## Out of scope (follow-up)

- `update_all()` (explicit full-table UPDATE escape hatch).
- `set(f<>(), value)` varargs DSL and column-relative expressions (`salary = salary + 1000`).
- Affected-row count return type.
- `UPDATE ... LIMIT` / `ORDER BY`.
