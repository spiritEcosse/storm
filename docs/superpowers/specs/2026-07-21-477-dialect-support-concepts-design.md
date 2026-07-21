# #477 — Dialect-support concepts (design)

## Problem

Backend-dialect capability checks are done ad-hoc through anonymous
`if constexpr (requires { ConnType::some_trait; })` probes rather than through
named concepts. This makes the gating harder to read and gives no clear
compile-time error when a backend lacks a capability.

## Premise verification (issue is partly stale)

The issue sketches `SupportsRightJoin` and `TransactionCapable`. Checking the
current tree:

- **`right_join()` was removed in #397** (replaced by reverse-FK, #398).
  `tests/schema/test_fk_fields.cpp` even static-asserts `!has_right_join<QuerySet<Task>>`.
  A `SupportsRightJoin` concept would therefore gate **nothing** — dead code the
  day it lands (and a likely SonarCloud unused-symbol flag). **Skipped.**
- **Dialect capabilities already exist** as `static constexpr bool` members on
  the connection types:
  - `postgresql::Connection`: `supports_limit_all`, `supports_returning`, `uses_pg_dialect`
  - `sqlite::Connection`: `supports_limit_all`, `supports_returning`, `supports_strict_tables`
- Only **two** of these are actually consumed today:
  - `uses_pg_dialect` — `schema.cppm:991`, `base.cppm:938`
  - `supports_limit_all` — `base.cppm:959`
  `supports_returning` and `supports_strict_tables` are **declared but never
  read** anywhere. Concepts for them would be speculative (same dead-concept
  trap as `SupportsRightJoin`). **Skipped.**
- **Transactions already work** (#415): `TransactionGuard::begin()` /
  `storm::begin()` / `storm::transaction()` call
  `in_transaction()`/`enter_transaction()`/`leave_transaction()`/`execute()` and
  the `Error` alias — but `TransactionGuard` is **unconstrained**. There is no
  named concept expressing that contract.

## Scope (approved: "Real traits + TransactionCapable")

Add named concepts for the dialect traits that genuinely exist AND are consumed,
plus a `TransactionCapable` concept, and refactor the ad-hoc sites to use them.

### New concepts (in `src/db/concept.cppm`, `namespace storm::db`)

```cpp
// A connection whose backend uses the PostgreSQL SQL dialect.
template <typename ConnType>
concept SupportsPgDialect = requires {
    { ConnType::uses_pg_dialect } -> std::convertible_to<bool>;
};

// A connection whose backend spells "unlimited rows" as LIMIT ALL (PG) rather
// than LIMIT -1 (SQLite). The concept captures the EXISTENCE probe; call sites
// still read the bool VALUE to pick the spelling.
template <typename ConnType>
concept SupportsLimitAll = requires {
    { ConnType::supports_limit_all } -> std::convertible_to<bool>;
};

// A connection that can run an RAII transaction (storm::begin / TransactionGuard).
// Captures exactly what TransactionGuard calls.
template <typename ConnType>
concept TransactionCapable = requires(ConnType& conn, std::string_view sql) {
    typename ConnType::Error;
    { conn.in_transaction() } -> std::convertible_to<bool>;
    conn.enter_transaction();
    conn.leave_transaction();
    { conn.execute(sql) } -> std::same_as<std::expected<void, typename ConnType::Error>>;
};
```

`SupportsPgDialect`/`SupportsLimitAll` model *presence of a trait*, not its
truth. `SupportsPgDialect<sqlite::Connection>` is **false** (SQLite declares no
`uses_pg_dialect` member) and **true** for PG — so it doubles as the dialect
switch the ad-hoc `requires { ... }` probe was already performing.
`SupportsLimitAll` is **true for BOTH** backends (both declare the member); its
call site still reads the value to choose `LIMIT ALL` vs `LIMIT -1`. This
matches the existing two-level `if constexpr` (existence, then value) exactly —
no behavior change.

### Refactored call sites (behavior-preserving)

| File | Before | After |
|---|---|---|
| `schema.cppm:991` | `requires { ConnType::uses_pg_dialect; }` | `db::SupportsPgDialect<ConnType>` |
| `base.cppm:938` | `requires { ConnTypeForDialect::uses_pg_dialect; }` | `db::SupportsPgDialect<ConnTypeForDialect>` |
| `base.cppm:959` | `requires { ConnTypeForDialect::supports_limit_all; }` | `db::SupportsLimitAll<ConnTypeForDialect>` |

The `base.cppm` sites default `ConnTypeForDialect = void`; `void` satisfies
neither concept, so the SQLite-compatible fallback branch is preserved.

### `TransactionCapable` constraint

Constrain `TransactionGuard<ConnType>` and the `storm::begin` / `storm::transaction`
free functions on `TransactionCapable<ConnType>`. This turns a mis-typed
connection into a clear constraint violation at the `begin()` call site instead
of a deep error inside the guard body. No behavior change for the real
connections (both satisfy it).

## Verification

Compile-time only, per the issue:

- `static_assert` near each connection definition (or in the test TU):
  - `SupportsPgDialect<postgresql::Connection>` / `!SupportsPgDialect<sqlite::Connection>`
  - `SupportsLimitAll<sqlite::Connection>` && `SupportsLimitAll<postgresql::Connection>`
  - `TransactionCapable<sqlite::Connection>` && `TransactionCapable<postgresql::Connection>`
  - Negative gates: `!SupportsPgDialect<int>`, `!TransactionCapable<int>`
- New `tests/schema/test_dialect_concepts.cpp` (auto-discovered by GLOB), a
  runnable GTest TU whose real content is the static_asserts (mirrors
  `test_entity_concept.cpp`).

TDD: write the test TU first, confirm it fails to compile (concepts undefined),
then add the concepts + refactor, then it passes.

## Explicitly out of scope

- `SupportsRightJoin` — no call site (right_join removed in #397).
- `SupportsReturning`, `SupportsStrictTables` — declared but unused; adding
  concepts would be dead code.
- Any new dialect capability or backend feature.
