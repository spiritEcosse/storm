# Design: thread-safe L3 statement cache (#271, Phase 2 of #215)

## Status

Approved 2026-05-30. Standalone fix, independent of #214 (the L1/L2-collapse
investigation). Stays correct whether or not #214 lands later.

## Problem

The Level-3 statement cache — `statement_cache_`
(`unordered_map<string, unique_ptr<Statement>>`) on each `Connection` — is
accessed with **no synchronization** in both backends
(`src/db/sqlite.cppm`, `src/db/postgresql_connection.cppm`). Phase 1 (#270)
fixed the dangling-pointer hole and added per-table invalidation, but the cache
is still explicitly **not thread-safe**.

## Scope (from #271 Definition of Done)

1. L3 cache reads/writes protected by a `shared_mutex` on both backends.
2. Bench within ±5% of develop baseline (Core 8 filter Phase 1 used).
3. TSAN clean under concurrent `prepare_cached` + `clear_statement_cache` +
   `clear_statement_cache(table)`.
4. `docs/architecture/STATEMENT_CACHING.md` updated.
5. Debug-only single-owner assertion at pool checkout/checkin.

## Design

### Mutex placement

Add `mutable std::shared_mutex cache_mutex_;` to `Connection` in both backends,
guarding every access to `statement_cache_`.

- **Reads (cache hit)** → `std::shared_lock` — allows concurrent readers.
- **Writes (insert on miss, both `clear_statement_cache` overloads)** →
  `std::unique_lock`.

### `prepare_cached(sql)` — shared-lock hot path

```
1. shared_lock; find(sql)
     hit  → reset() + return ptr (release shared_lock)
     miss → release shared_lock
2. prepare_raw(sql) / prepare_pg_statement(sql)   // NO lock held during the
                                                  // expensive prepare syscall
3. unique_lock; try_emplace(sql, std::move(stmt))
     - won race  → return new entry ptr
     - lost race → another thread inserted same SQL meanwhile;
                   discard ours, return the existing entry ptr
```

Rationale: keeps the hot path on a shared lock (concurrent readers), never holds
the mutex across `sqlite3_prepare_v2` / `PQprepare`, and `try_emplace` resolves
the duplicate-prepare race created by dropping the lock between steps 1 and 2.

### `clear_statement_cache()` / `clear_statement_cache(table)`

`std::unique_lock` around `clear()` / `std::erase_if`. The no-arg overload stays
`noexcept` (locking a non-recursive mutex we own cannot fail except on a
programming error — consistent with the rest of the codebase).

### `cached_statement_count() const noexcept`

`std::shared_lock` on the `mutable` mutex; stays `const noexcept`.

### Safety invariant (documented, NOT enforced by the mutex)

A returned `Statement*` outlives the lock. It stays valid because the
**ConnectionPool checks out each Connection exclusively** — between `checkout()`
and `checkin()` exactly one thread touches a given Connection. The mutex makes
the *map operations* TSAN-clean and is defense-in-depth; it does **not** make the
*returned pointer* safe against a concurrent `clear()` if a Connection were truly
shared. The exclusive-checkout invariant is what prevents that. This is
documented in `STATEMENT_CACHING.md`.

### Debug single-owner assertion (pool level)

In `PoolCore` (`src/db/pool.cppm`), add a debug-only owner check that asserts a
Connection is not checked out by two threads at once — the actual invariant #271
asks for:

- Add `std::thread::id owner{};` to `Entry`, gated `#ifndef NDEBUG`.
- On `checkout` (when an entry is handed out): assert `owner == std::thread::id{}`
  (not already owned), then set `owner = std::this_thread::get_id()`.
- On `checkin`: assert `owner == std::this_thread::get_id()`, then reset
  `owner = {}`.
- All under the existing pool `mutex_`, so no new synchronization. Release builds
  pay nothing (`#ifndef NDEBUG`).

## Testing (TDD — tests written first, must fail, then implement)

New file `tests/db/test_statement_cache_threading.cpp`, run under the existing
`ninja-tsan` matrix. TYPED_TEST over `DatabaseTypes` (SQLite + PostgreSQL,
PG skips gracefully if not running).

Cases:
1. **Concurrent hits**: N threads call `prepare_cached` on the SAME connection
   with the SAME SQL set in a loop → TSAN-clean, every returned ptr non-null.
   (Connection is shared here ONLY for the cache-map stress test; no Level-2
   pointer is retained across a clear, so it exercises the mutex without hitting
   the documented exclusive-checkout caveat.)
2. **Concurrent miss/insert race**: N threads prepare DISTINCT SQL concurrently →
   all inserted, count correct, TSAN-clean.
3. **prepare + clear_statement_cache()**: writer thread clears while readers
   prepare → TSAN-clean (no torn map access). Returned pointers are used only
   *before* the concurrent clear in each iteration to respect lifetime.
4. **prepare + clear_statement_cache(table)**: same, with per-table invalidation.
5. **Pool exclusive-checkout**: existing pool semantics still hold; debug owner
   assertion does not fire under correct usage (sequential checkout/checkin per
   thread).

Tests must compile-fail or TSAN-fail against the current (unlocked) code to prove
they test real behavior, then pass after the mutex lands.

## Benchmarks

Release build, Core 8 filter (`Storm/SELECT`, `Storm/SELECT_LIMIT`,
`Storm/INSERT`). Must be within ±5% of develop baseline. Capture baseline on
develop first, then re-run on the feature branch.

## Out of scope

- #214 (removing L1/L2). The L2 raw `Statement*` cache stays; its safety rests on
  the exclusive-checkout invariant, now documented.
- LRU eviction, cache-size limits.

## Files touched

- `src/db/sqlite.cppm` — mutex + locks.
- `src/db/postgresql_connection.cppm` — mutex + locks.
- `src/db/pool.cppm` — debug owner assertion on Entry/checkout/checkin.
- `tests/db/test_statement_cache_threading.cpp` — new TSAN tests.
- `docs/architecture/STATEMENT_CACHING.md` — Phase 2 thread-safety note.
