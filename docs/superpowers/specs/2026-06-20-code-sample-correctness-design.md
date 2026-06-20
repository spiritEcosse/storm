# Code-Sample Correctness Pass — Design (Part D)

**Date:** 2026-06-20
**Scope:** Part D — verify every user-facing code sample compiles against current
`src/`, found via a full audit the user prompted ("are all samples of code correct
with src/?"). Same branch.

## Goal

Make the QuerySet code samples in the docs match the REAL, tested API surface in
`src/orm/queryset.cppm` + `src/orm/statements/*`, cross-checked against `tests/`
(which compile against current code).

## Ground truth (verified against src/ + tests/)

**Terminal calls return a PROXY; you must call `.execute()` to run the query:**
- `select()` → proxy; `.execute()` → `std::expected<plf::hive<T>, Error>` (select.cppm:186).
- `first()` → proxy; `.execute()` → `std::expected<std::optional<T>, Error>` (:199).
- `get()` → proxy; `.execute()` → `std::expected<T, Error>` (:219).
- `count()/sum()/avg()/min()/max()` → proxy; `.execute()` → expected scalar.
- `insert()/update()/erase()/erase_all()/update_all()` → proxy; `.execute()` →
  `std::expected<...>`.
- Tests confirm: `.select().execute()` (345×), `.insert(...).execute()`,
  `.count().execute()` everywhere. NO test uses a bare terminal call to obtain
  results.

**Already correct in current docs (leave alone):**
- Aggregates (`count()/sum()/...`) — docs already append `.execute()`.
- Batch insert of a container: `qs.insert(people).execute()` where `people` is a
  vector/hive is VALID (test_values.cpp:194 etc.) — do NOT force `std::span`.
- Chainable builders `where()/join()/order_by()/limit()/group_by()/having()` — no
  `.execute()` (they return QuerySet).
- COOKBOOK lines ~110-145 (the lower half) already use `.execute()` correctly.

## The bug

The "basic usage" sections show terminal `select`/`insert`/`update`/`erase` WITHOUT
`.execute()`, implying the call returns rows/results when it returns a proxy.
COOKBOOK is internally inconsistent (upper half bare, lower half correct).

## Exact inventory (verified lines)

### Missing `.execute()` on terminal `select()`
- `guide/COOKBOOK.md`: 44, 47, 62, 63, 64, 65, 66, 67, 70, 73, 76, 79, 80, 92, 205, 219, 222, 223
- `guide/features/SELECT_QUERIES.md`: 29, 51, 64, 67
- `guide/features/JOIN_OPERATIONS.md`: 34, 62, 96, 106
- `internals/architecture/COMPILE_TIME_VS_RUNTIME.md`: 31, 124
- `internals/architecture/DESIGN_DECISIONS.md`: 197, 200
- `internals/performance/PERFORMANCE.md`: 424, 445

### Missing `.execute()` on terminal `insert()/update()/erase()`
- `guide/COOKBOOK.md`: 31, 37, 89, 94, 128 (NOT 35/36 — those are `people.insert(...)`
  building a std::vector, a different `.insert`; leave them)
- `guide/features/CRUD_OPERATIONS.md`: 57, 122, 231

### Each fix
Append `.execute()` to the terminal call. Where the sample then USES the result
(iterates rows, reads a value), also show `.value()` / error handling consistent
with the tests' `result.has_value()` / `result.value()` idiom — but keep edits
minimal: for one-liners that only demonstrate the call, `…select().execute();`
(or assigned to `auto result = …execute();`) is enough; do NOT balloon every
snippet into full error handling unless the snippet already reads the rows.

### False positives — do NOT touch
- `internals/architecture/STATEMENT_CACHING.md:29` `return pos->second.get()` — internal
  cache code, not a QuerySet call.
- COOKBOOK `people.insert({...})` (std::vector building) lines 35, 36.
- `.distinct<>()`, `.values<>()`, `.where()` etc. used mid-chain.

## Verification
1. No terminal bare select/first/get assigned-or-returned without execute:
   `grep -rnE '(=|return).*\.(select|first|get)\(\)\s*;?\s*$' docs/guide docs/internals --include='*.md' | grep -v superpowers | grep -v '\.execute()' | grep -v 'pos->second'` → empty.
2. No terminal bare insert/update/erase without execute:
   `grep -rnE '\b(qs|queryset|message_qs|\w*_qs)\.(insert|update|erase|erase_all|update_all)\([^)]*\)\s*;\s*$' docs/guide docs/internals --include='*.md' | grep -v superpowers | grep -v '\.execute()'` → empty (excluding `people.insert` vector-building).
3. Spot-check 5 fixed snippets read coherently against a matching test.
4. `mkdocs build --strict` EXIT=0.
5. Part A/B/C invariants still clean.

## Risk notes
- These are inside code fences (not links) — mkdocs won't catch correctness; greps + the test cross-check are the gate.
- Keep edits surgical: append `.execute()`, don't rewrite whole snippets or add error-handling boilerplate where the snippet is just illustrating a call shape.
- Out of scope (unchanged): hardcoded perf NUMBERS; CLAUDE.md (it has the same bare-`select()` style but is the project's own file, not under docs/).
