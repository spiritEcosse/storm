# Upsert support (`ON CONFLICT`) — Design

**Issue**: #205
**Date**: 2026-06-22
**Status**: Approved (brainstorming) → ready for implementation plan

## Goal

Add single-row upsert (`INSERT ... ON CONFLICT ...`) to the `QuerySet` insert
proxy, covering the two real-world patterns:

- **DO UPDATE** — overwrite listed columns with the attempted-insert values.
- **DO NOTHING** — get-or-create; skip the insert when a conflicting row exists.

Both SQLite (3.35+) and PostgreSQL share identical syntax, so there is no
backend divergence. MySQL (`ON DUPLICATE KEY UPDATE`) is out of scope.

## Out of scope (follow-up issues)

- **Bulk upsert** — `qs.insert(span).on_conflict<>()...`. The `DO NOTHING` +
  RETURNING-vector gap problem (skipped rows leave holes in the id vector) and
  the chunked-transaction path make this a separate, larger effort.
- **SET-expression DSL** — `SET count = count + 1`, literals, arbitrary
  expressions. v1 only emits `col = excluded.col`.
- **Auto conflict-target detection** — inferring the unique column. v1 requires
  an explicit target.

## API Surface

The upsert chain hangs off the existing single-row insert proxies
(`SingleQuery` for `ReturnId::Yes`, `VoidQuery` for `ReturnId::No`). The upsert
path itself does **not** carry `ReturnId` — the return type is determined by the
terminal verb, not a template flag.

```cpp
// DO UPDATE — overwrite listed columns; always returns the touched row's id
int64_t id = QuerySet<User>()
    .insert(user)
    .on_conflict<^^User::email>()          // → ConflictTarget proxy
    .update<^^User::name, ^^User::age>()   // → UpdateUpsertQuery
    .execute();                            // std::expected<int64_t, Error>

// DO NOTHING — get-or-create; nullopt = conflict skipped (row already existed)
std::optional<int64_t> r = QuerySet<User>()
    .insert(user)
    .on_conflict<^^User::email>()
    .nothing()                             // → NothingUpsertQuery
    .execute();                            // std::expected<std::optional<int64_t>, Error>
```

### Return-type rationale (`ReturnId` dropped on the upsert path)

`ReturnId::Yes/No` ("do I want the pk back?") and `DO UPDATE`/`DO NOTHING`
("what happens on conflict?") are independent axes. But the only useful reason
to call `.nothing()` is get-or-create, which *needs* the id — and `.update()`
always touches a row, so it always has an id. The `ReturnId::No` + upsert
combination is fire-and-forget you can't observe: practically useless. So the
upsert chain omits `ReturnId` entirely and the verb picks the return shape:

| verb            | conflict effect | return type                              |
| --------------- | --------------- | ---------------------------------------- |
| `.update<...>()`| overwrite row   | `std::expected<int64_t, Error>`          |
| `.nothing()`    | may skip        | `std::expected<std::optional<int64_t>, Error>` |

Plain `insert()` keeps `ReturnId::Yes/No` unchanged (fire-and-forget bulk loads
still want it). Only the upsert chain drops it.

### Proxy chain

- `on_conflict<Target...>()` — added to **both** `SingleQuery` and `VoidQuery`.
  Returns a `ConflictTarget` proxy holding the moved `InsertStatement`, the
  `obj` reference (`[[clang::lifetimebound]]`), and the target columns as NTTPs.
- `ConflictTarget::update<SetCols...>()` → `UpdateUpsertQuery`
  (`.execute()` / `.to_sql()` / `.sql()`).
- `ConflictTarget::nothing()` → `NothingUpsertQuery`
  (`.execute()` / `.to_sql()` / `.sql()`).

## Compile-time validation

Two `requires` constraints, fired at the call sites (CLAUDE.md rule #11 — use
`requires`, never `throw` in `consteval`):

- **`ConflictTargetUnique<T, Target...>`** — every `Target` is a non-static data
  member of `T`, and the set is either:
  - a single field carrying `FieldAttr::unique`, **or**
  - an exact column-set match for some `UniqueIndex<...>` in `Indexes<T>`.

  Rejects targets with no DB-level unique constraint, which would otherwise be a
  runtime `ON CONFLICT` error from SQLite/PG. (The PK is a valid implicit unique
  target too; included.)

- **`UpsertSettable<T, SetCols...>`** — reuses
  `UpdateGrammar::is_settable_member` (non-static data member, not the PK). The
  conflict-target columns may appear in the SET list but need not.

## SQL generation

A new `UpsertGrammar<T>` module mirrors `UpdateGrammar<T>`: all `consteval`,
deriving purely from reflection over `T` (via `Base = BaseStatement<T>`). It
appends the conflict clause onto the existing compile-time INSERT SQL
(`InsertStatement::build_insert_sql_array_impl<true>()` — the RETURNING variant),
reusing the table/column/placeholder machinery rather than re-spelling it.
RETURNING is **always** emitted (both verbs need it).

```sql
-- .on_conflict<email>().update<name, age>()
INSERT INTO users (name, email, age) VALUES (?, ?, ?)
ON CONFLICT (email) DO UPDATE SET name=excluded.name, age=excluded.age
RETURNING id

-- .on_conflict<email>().nothing()
INSERT INTO users (name, email, age) VALUES (?, ?, ?)
ON CONFLICT (email) DO NOTHING
RETURNING id
```

- **SET clause** — a variant of `build_conditional_set_clause` emitting
  `col=excluded.col`. FK columns use the `_id` column-name writer (#422):
  `owner_id=excluded.owner_id`. These take **no bind params**.
  `auto_update` timestamp fields (#209) are auto-appended to the SET list and
  **do** bind `now()` at execution — consistent with conditional UPDATE, so an
  upserted-via-DO-UPDATE row gets a fresh `updated_at`.
- **Conflict target** — `(col1, col2, ...)` via the `_id`-aware column-name
  writer (so an FK conflict target renders `owner_id`).
- The clause text is a compile-time constant per `<Target..., SetCols...>`
  instantiation (`ConstexprString` → `static inline const std::string`), the
  same caching pattern as `insert_returning_sql_string`.
- **Bind order is unchanged** — the same non-PK INSERT binders run; only the
  trailing `auto_update` `now()`-stamps are appended (exactly as conditional
  UPDATE does). `excluded.col` SET targets bind nothing.

## Execution & return semantics

Two new single-row paths on `InsertStatement`, modeled on
`execute_single_optimized` (prepare_cached → bind non-PK + auto_update now() →
`step_raw()`):

```cpp
execute_upsert_update(obj)  -> std::expected<int64_t, Error>
execute_upsert_nothing(obj) -> std::expected<std::optional<int64_t>, Error>
```

- **`execute_upsert_update`** — `DO UPDATE` always touches a row:
  - `ROW_AVAILABLE` → `extract_int64(0)` (inserted or updated row id).
  - `NO_MORE_ROWS` → defensive `Error` (should not occur with DO UPDATE).
  - error rc → `std::unexpected(Error{...})`.
- **`execute_upsert_nothing`** — `DO NOTHING` may skip:
  - `ROW_AVAILABLE` → `std::optional{extract_int64(0)}` (newly inserted).
  - `NO_MORE_ROWS` → `std::nullopt` (conflict skipped — no id available;
    `DO NOTHING` returns no row, so the *existing* row's id is not retrievable
    here. Callers who need the existing id every time should use `.update()`).
  - error rc → `std::unexpected(Error{...})`.

No `ReturnId::No` / void path on the upsert chain (removed by construction — it
is the one practically useless combination).

`.to_sql()` (params inlined, for debugging) and `.sql()` (raw template) reuse
the existing `to_sql_impl` / static-string helpers in `InsertStatement`.

## Testing

`TYPED_TEST` over `DatabaseTypes` (SQLite + PostgreSQL — both share the syntax).

- DO UPDATE: single column, multiple columns; assert the row is overwritten and
  the returned id matches the conflicting row.
- DO NOTHING: insert-new (assert id present) vs conflict-skip (assert `nullopt`);
  assert the existing row is untouched.
- FK column in SET list → `owner_id=excluded.owner_id`.
- FK column as conflict target → `ON CONFLICT (owner_id)`.
- `auto_update` field stamped fresh on a DO UPDATE upsert.
- Composite conflict target via `UniqueIndex<a, b>`.
- `.to_sql()` / `.sql()` golden strings (both verbs).
- Error path via the mock backend (prepare/step failure), following the
  `test_orm_mock_errors.cpp` pattern.
- **Compile-fail tests** (where the harness supports them): non-unique conflict
  target; PK in the SET list.

TDD order (CLAUDE.md rule #9): write tests → run (new tests MUST fail) →
implement → run (all pass).

## Documentation

- New `docs/guide/features/UPSERT.md` with both patterns, the return-type table,
  and the get-or-create caveat (DO NOTHING does not return the existing id).
- Link from `docs/README.md` and add an upsert row to the QuerySet API section
  in `CLAUDE.md`.
- Update the `storm-docs-writer` agent surface if it enumerates features.

## Module / file layout

- `src/orm/statements/upsert_grammar.cppm` — new `UpsertGrammar<T>` (mirrors
  `update_grammar.cppm`): `consteval` conflict-target + `excluded.col` SET-clause
  builders, and the per-instantiation upsert SQL string.
- `src/orm/statements/insert.cppm` — add `on_conflict<>()` to `SingleQuery` /
  `VoidQuery`; add `ConflictTarget`, `UpdateUpsertQuery`, `NothingUpsertQuery`
  proxies; add `execute_upsert_update` / `execute_upsert_nothing`. (Watch the
  600-line hook limit; if it pushes over, the proxies may move to a sibling
  `namespace detail` like the UpdateStatement proxies did in #438.)
- Both `.cppm` files are GLOB-discovered — no CMake changes.
```
