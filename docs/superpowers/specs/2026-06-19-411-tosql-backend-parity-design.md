# #411 — Cross-backend `to_sql()` parity test + documentation

**Date:** 2026-06-19
**Issue:** #411 — `test(db): no cross-backend parity check for Statement::to_sql()`
**Type:** Testing + documentation. No source/behavior change.

## Problem

`Statement::to_sql()` (parameter-inlined SQL, for debugging only — execution always
binds `?` parameters) is produced by two different mechanisms with no test asserting
their behavior:

- **SQLite** — `expanded_sql()` calls the engine-native `sqlite3_expanded_sql()`
  (`src/db/sqlite.cppm` ~L258).
- **PostgreSQL** — `expanded_sql()` hand-rolls `?`-placeholder substitution into a
  stored `original_sql_`, quoting each `param_values_[i]` and escaping `'`→`''`
  (`src/db/postgresql_statement.cppm` ~L275, `append_quoted_param` ~L257).

A divergence in quoting/escaping would go unnoticed.

## Findings — where the backends actually agree vs. diverge

Read from the two `expanded_sql()` implementations and the PG bind methods:

**Note (corrected against a live PG run):** the initial code-read predicted int/bool
would agree. Running the tests against PostgreSQL proved otherwise — PG's hand-rolled
`expanded_sql` stores *every* bound param as text and wraps it in single quotes, so
numeric and bool operands come out quoted (`'30'`, `'1'`) while SQLite emits them bare
(`30`, `1`). Same value, different quoting. The table below reflects the verified
behavior.

| Operand | SQLite (`sqlite3_expanded_sql`) | PostgreSQL (hand-rolled) | Verdict |
|---|---|---|---|
| int / int64 | `30` (bare) | `'30'` (quoted) | **diverge** (quoting) |
| bool | `1` / `0` (bare) | `'1'` / `'0'` (quoted) | **diverge** (quoting) |
| NULL (empty optional) | `NULL` | `NULL` (`param_ptrs_[i] == nullptr`) | **agree** |
| embedded quote `O'Brien` | `'O''Brien'` | `'O''Brien'` (`'`→`''`) | **agree** |
| literal `?` inside text | preserved (engine) | preserved (PG scanner tracks `in_single_quote`) | **agree** |
| double | engine float→text (bare) | `'%.17g'` quoted (`bind_double`) | **diverge** (quoting + formatting) |
| BLOB | `x'48656C6C6F'` hex literal | raw bytes inside `'...'` (only `'` escaped) | **diverge** (encoding) |

Exact whole-string cross-backend parity is **infeasible** (PG quotes all scalar
operands; BLOB hex vs raw bytes; double formatting). The DoD explicitly permits
documenting divergence where exact parity is not achievable.

## Decision (approved)

**Pin actual behavior + document.** No change to the debug-only `expanded_sql()` paths.

1. Tests that assert the **agreeing** operands render identically (same expectation
   string for both `TypeParam` instantiations — if a backend ever diverges, its
   instantiation fails).
2. Tests that **pin the known-divergent** operands per backend via
   `if constexpr (std::is_same_v<TypeParam, storm::db::postgresql::Connection>)`
   (the established idiom in `test_db_helpers.h`).
3. A documentation subsection recording the divergence and the debug-only caveat.

## Test plan

**File:** `tests/schema/test_sql_inspection.cpp` — extend the existing
`SqlInspectionTest` fixture (`TYPED_TEST_SUITE(..., DatabaseTypes)`, runs SQLite + PG;
PG auto-skips without `STORM_PG_CONNSTR`).

**Model:** `Person` (shared/models.h) covers every required operand on one struct:
`age` (int), `salary` (double), `is_active` (bool), `name` (string — for embedded
quote and literal `?`), `score` (`std::optional<int>` — NULL), `avatar`
(`std::vector<uint8_t>` — BLOB).

Each test inserts a row, then calls `qs.where(...).select().to_sql()` (or
`insert(p).to_sql()`) so the operand value is bound and inlined.

New `TYPED_TEST`s:

1. **`ToSqlIntByBackend`** — `where(age == 30)` → SQLite contains bare `= 30`
   (unquoted); PG contains `= '30'` (quoted). Pins the quoting divergence.
2. **`ToSqlBoolByBackend`** — `where(is_active == true)` → SQLite `= 1`; PG `= '1'`.
3. **`ToSqlNullParity`** — insert a row with empty `score`, then
   `insert(p).to_sql()` → contains `NULL` for the score column. Same expectation.
4. **`ToSqlEmbeddedQuoteParity`** — `where(name == "O'Brien")` → contains
   `'O''Brien'`. Same expectation both backends.
5. **`ToSqlLiteralQuestionMarkParity`** — `where(name == "a?b")` → contains `'a?b'`
   (the `?` inside the quoted literal is NOT substituted). Same expectation. This is
   the core escaping check from the issue.
6. **`ToSqlBlobDivergesByBackend`** — `insert(p).to_sql()` with a non-empty `avatar`.
   `if constexpr` PG: assert the blob renders as a quoted string operand; else
   (SQLite): assert it renders as an `x'...'` hex literal. Pins the known divergence.
7. **`ToSqlDoubleByBackend`** — `where(salary == 1234.5)` → assert each backend's
   actual rendering of the double (pin behavior; tolerate formatting difference).

Operand coverage required by DoD — literal `?` (#5), embedded quotes (#4), NULL (#3),
BLOB (#6), numeric int/double (#1, #7), bool (#2). ✅

**TDD note:** the int/bool tests were initially written as cross-backend *parity*
assertions (predicted from the code-read). Running them against a live PostgreSQL
server **failed** — PG quotes scalar operands — which proved the tests exercise real
behavior and corrected the design. They were re-pinned per backend.

## Documentation plan

Add a subsection **"SQL inspection (`to_sql()`) — backend behavior"** to
`docs/features/CRUD_OPERATIONS.md` (where `to_sql()` is already referenced):

- `to_sql()` is a **debug/inspection aid only**; execution always binds `?` params.
- SQLite uses engine-native `sqlite3_expanded_sql()`; PG hand-rolls `?` substitution.
- Agreeing operands: int, int64, bool, NULL, embedded quotes, literal `?` in text.
- Known divergences: **BLOB** (SQLite hex `x'...'` vs PG raw quoted bytes) and
  **double formatting** (SQLite engine vs PG `%.17g`).
- Link back to issue #411.

## Verification

- `ctest --preset ninja-debug` (SQLite + PG) — all green, including new tests.
- Coverage unaffected (tests only; no new source lines). Run coverage target to
  confirm 100% maintained.
- No sanitizer/benchmark runs needed — no source/hot-path change (test + docs only).

## Out of scope

- Unifying the two `expanded_sql()` implementations (rejected — large change to a
  debug-only path for no functional gain).
- `to_sql()` for JOIN / aggregate statements (issue scopes a "representative query").
