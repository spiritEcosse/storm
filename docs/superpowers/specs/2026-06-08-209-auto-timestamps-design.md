# Automatic Timestamp Support (`auto_create` / `auto_update`) — Design

**Issue:** #209
**Date:** 2026-06-08
**Status:** Implemented (feature/209-auto-timestamps)

## Summary

Add two field attributes — `auto_create` and `auto_update` — that automatically
populate `std::chrono::system_clock::time_point` columns with the current time
during INSERT and UPDATE. The value is computed in C++ (`system_clock::now()`)
and bound as a parameter; the database row receives the correct timestamp with no
manual bookkeeping by the user.

## Decisions

These were settled during brainstorming. They are deliberate and should not be
re-litigated without cause:

| Decision | Choice | Rationale |
|---|---|---|
| **Mutation** | Never mutate the caller's object | Preserves the `const T&` / `span<const T>` contract |
| **Write-back** | None — bind-time only | YAGNI: DB row is correct; re-SELECT to read the value in memory |
| **Overloads** | None added | Existing signatures unchanged → zero call-site churn |
| **Value source** | C++ `system_clock::now()` | Backend-uniform (same bound string for SQLite + PG); round-trips via existing `time_point`↔text conversion; makes the per-field UPDATE rule trivial |
| **Bulk timestamp** | One `now()` per batch, reused for all rows | Matches issue intent ("same `created_at` for all rows"); one clock read |
| **UPDATE behaviour** | `auto_update` → `now()`; `auto_create` → bound from the object's stored value | "Preserves" created_at without touching UPDATE column generation |
| **Manual override** | Always auto-set; manual values on auto fields ignored | No sentinel check, no hot-path branch |

### Why C++ clock, not SQL `CURRENT_TIMESTAMP`

- Backend-uniform: SQLite `CURRENT_TIMESTAMP` is second-precision UTC text;
  PostgreSQL is sub-second `timestamptz`. Binding a C++-formatted string gives the
  same value/format on both.
- `auto_update` on UPDATE cannot be expressed as a schema `DEFAULT` (defaults only
  fire on INSERT), so SQL-side would require literal substitution anyway.
- Storm already converts `time_point` ↔ `"YYYY-MM-DD HH:MM:SS"` text in both
  directions (`utilities.cppm:202`), so values round-trip cleanly when selected back.

## Lifetime safety (already in place — no prerequisite)

No prerequisite PR is needed. `-Wdangling` is **on by default** in clang and the
library target already compiles with `-Werror` (`CMakeLists.txt:85`), so the
existing `[[clang::lifetimebound]]` annotations on `insert`/`update` are **already
enforced as hard errors**. Verified empirically: a `lifetimebound` parameter bound
to a temporary fails the build today with `[-Werror,-Wdangling]`, with no `-Wall`
and no explicit `-Wdangling` flag.

This catches the same-statement temporary case (`insert(Article{...})`). It does
**not** catch cross-statement use-after-free (no clang flag does — that is
borrow-checker territory; verified for #357). Since this design adds no new
reference-taking signatures (existing `const T&` / `span<const T>` unchanged), the
lifetime surface is unchanged.

## Architecture

The change has a single behavioural locus: the unified field binder. Everything
else is attribute plumbing and validation.

### 1. Field attribute extension

Append to `FieldAttr` in **both** `src/orm/statements/base.cppm:24` and
`src/orm/where.cppm:35` (the two enums must stay byte-identical):

```cpp
enum class FieldAttr : std::uint8_t {
    primary, primary_autoincrement, indexed, unique, fk, auto_create, auto_update
};
```

> The issue's proposed enum was stale — it dropped `primary_autoincrement` (added
> in #379). We **append**, not replace.

Add two consteval detectors in `BaseStatement`, mirroring `is_fk_field`
(`base.cppm:78`):

```cpp
static consteval auto is_auto_create_field(std::meta::info member) -> bool;
static consteval auto is_auto_update_field(std::meta::info member) -> bool;
```

### 2. Compile-time validation

A `requires`-based concept (not `throw`, per CLAUDE.md rule 11) ensures a timestamp
field has the correct type:

```cpp
template <auto MemberPtr>
concept ValidTimestampField =
    std::same_as<std::remove_cvref_t<typename [:std::meta::type_of(MemberPtr):]>,
                 std::chrono::system_clock::time_point>;
```

A wrong-typed timestamp field (e.g. `[[= FieldAttr::auto_create]] int created_at`)
produces a constraint violation at the call site with a clear message.

### 3. Bind-time substitution (the only runtime change)

Single injection point: `bind_field_at_index` (`base.cppm:267`). This unified
binder is used by **all four** paths:

- INSERT single/bulk — `bind_non_pk_fields_impl` (`base.cppm:258`) → `bind_field_at_index`
- INSERT all-fields — `bind_all_fields_impl` (`base.cppm:247`) → `bind_field_at_index`
- UPDATE single & bulk — `update.cppm:266` `inline_bind_all_fields` →
  `Base::bind_field_at_index`

**The binder already carries a 3rd template parameter** — `bool SkipPK = false`
(`base.cppm:265`). It is `true` for *both* INSERT-non-PK (`base.cppm:258`) *and*
UPDATE (`update.cppm:272`), so `SkipPK` **cannot** tell INSERT from UPDATE and must
not be reused for the timestamp rule. Add `IsUpdate` as a **separate, 4th**
template parameter:

```cpp
template <typename ConnType, std::size_t Index, bool SkipPK = false, bool IsUpdate = false>
```

Only the two UPDATE call sites (`update.cppm:272` and the bulk path) pass
`IsUpdate = true`; all INSERT call sites keep the default `false`. Then add an
`if constexpr` branch **before** the plain-field `else` (currently `base.cppm:302`):

```cpp
else if constexpr (is_auto_update_field(member)
                   || (is_auto_create_field(member) && !IsUpdate)) {
    // auto_update: now() on INSERT and UPDATE
    // auto_create: now() on INSERT only
    auto result = bind_value_by_type<ConnType>(*stmt, param_index, now);
    if (!result) return std::unexpected(result.error());
    ++param_index;
    return {};
}
// auto_create on UPDATE falls through to the normal obj.[:member:] bind below.
```

**Threading the batch `now()` value.** `bind_field_at_index` currently takes
`(stmt, obj, param_index)`. To make all rows in a bulk operation share one clock
read, the per-batch `now()` value must reach the binder as data, not be re-read per
field. Add a trailing `now` parameter (a `system_clock::time_point`), defaulted so
non-timestamp call paths need no churn:

```cpp
// new trailing binder param; default lets non-timestamp paths stay untouched
std::chrono::system_clock::time_point now = std::chrono::system_clock::now()
```

The caller reads `now()` **once** per operation (`bind_all_fields_impl`,
`bind_non_pk_fields_impl`, `inline_bind_all_fields`) and forwards the same value
into every `bind_field_at_index` invocation in the fold. The bound value reuses the
existing `time_point` text-storage binding (`is_text_stored_v`, `base.cppm:427`) —
no schema change.

### 4. Schema

No change. `schema.cppm:82` already maps `system_clock::time_point` to `TEXT`
(SQLite) / `TIMESTAMP` (PostgreSQL).

## Components and boundaries

| Unit | Responsibility | Depends on |
|---|---|---|
| `FieldAttr` enum (base + where) | Declare the two new attributes | — |
| `is_auto_create_field` / `is_auto_update_field` | Detect annotated members at compile time | `annotation_of_type<meta::FieldAttr>` + equality (mirrors `is_fk_field`) |
| `ValidTimestampField` concept | Reject wrong-typed timestamp fields | `std::meta::type_of` |
| `bind_field_at_index<…, SkipPK, IsUpdate>` | Substitute `now()` for the right fields at bind time | new 4th template param `IsUpdate`, batch `now()`, `bind_value_by_type` |

## Error handling

- Wrong field type → **compile-time** constraint violation (not a runtime error).
- Binding `now()` reuses the existing `bind_value_by_type` path, which already
  returns `std::expected<void, Error>`; failures propagate unchanged. No new runtime
  error modes.

## Testing

Tests are written **first** (must fail before implementation), and run on both
backends via `TYPED_TEST` with `DatabaseTypes`.

**Detection / validation**
- `is_auto_create_field` / `is_auto_update_field` return correct results.
- A model with a wrong-typed timestamp field fails to compile (negative compile
  test, where feasible).

**INSERT**
- Single insert stamps both `created_at` and `updated_at`.
- Bulk insert stamps every row; all rows in a batch share the same value.
- A model with no timestamp fields is unaffected (no-op).
- SQL/value verification: the bound value is `now()`, not the object's stale value.

**UPDATE**
- Single update stamps `updated_at`; `created_at` is bound from the object's stored
  value (not overwritten with `now()`).
- Bulk update behaves likewise.

**Cross-cutting**
- All supported timestamp scenarios run on SQLite and PostgreSQL.

## Documentation

- `docs/reference/FIELD_TYPES.md` — new section on `auto_create` / `auto_update`.
- `docs/.../COOKBOOK.md` — usage example.
- `CLAUDE.md` — attribute guideline + the bind-time-only / no-write-back contract.

## Out of scope

- In-memory write-back into the caller's object (explicitly declined — re-SELECT to
  read the value).
- `insert`/`update` overloads for write-back.
- SQL-side `CURRENT_TIMESTAMP` / schema `DEFAULT` population.
- Soft-delete (`auto_delete`) — mentioned in the issue as a future extension, not
  built here.
