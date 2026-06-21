# Collapse L1/L2 Statement Caching (Option C) — Design

**Date:** 2026-05-30
**Issue:** #214 (investigation concluded Option C)
**Depends on:** `feature/214-cache-investigation` (the benchmark evidence — see
`docs/superpowers/results/2026-05-30-cache-investigation-results.md`)
**Unblocks:** #273 (rewrite — LRU + stats on the single L3 map)

## Motivation

The #214 investigation proved L1 and L2 statement caching provide no measurable benefit on any
workload — multi-row queries, bulk writes, and single-row PK tight loops all land within the
measurement noise floor (≤3%, mostly <1%) when both are disabled. L3 (`Connection::statement_cache_`,
mutex-protected since #271) carries the workload alone. This spec collapses the three-level cache to
a single L3 level by removing L1 and L2 entirely.

The ownership model (decided with the user): **per-call stack temporary**, realized via
**Approach A** — the result proxy owns the statement by value instead of holding a reference to a
cached member.

## Scope

Files: `src/orm/queryset.cppm`, `src/orm/statements/{select,update,insert,erase,distinct,aggregate}.cppm`,
`benchmarks/cache_probe.cpp`, `benchmarks/CMakeLists.txt`, `docs/architecture/STATEMENT_CACHING.md`.

**Delete L1** (QuerySet level):
- The four `mutable std::unique_ptr<…Statement> {insert,erase,select,update}_stmt_` members.
- The four `get_*_statement()` lazy accessors (including their `#ifdef STORM_DISABLE_L1` guards).
- The 12 call sites that call `get_*_statement()` construct a statement inline instead (see below).

**Delete L2** (Statement level), in every statement class that has them:
- Cached pointer/string members: `cached_simple_stmt_`, `cached_first_stmt_`, `cached_get_stmt_`,
  `cached_stmt_`, `cached_where_addr_`, `cached_sql_` (SelectStatement) and the analogous members in
  update/insert/aggregate/distinct.
- All fast-path logic that reads them: `can_use_addr_fast_path`, `rebind_where_only`, the
  `cached_* == nullptr` short-circuit branches, and the `#ifdef STORM_DISABLE_L2` guards.
- Each prepare path becomes an unconditional `conn_->prepare_cached(sql)` → bind → execute.

**Remove the scaffolding** (per the user-confirmed flag lifecycle):
- `STORM_DISABLE_L1` / `STORM_DISABLE_L2` macros (nothing references them once the `#else` branches
  become the only code).
- `benchmarks/cache_probe.cpp` and the `storm_cache_probe` CMake target.

## Ownership change (Approach A) — uniform across all statement classes

Every statement class uses the same proxy pattern today: a `Query`/`SingleQuery`/`BulkQuery`/
`FirstQuery`/`GetQuery`/etc. struct holding `Statement& stmt;`, returned by a `query(...)` factory as
`return {*this, …};`. With L1/L2 gone, a naive stack temporary would dangle because the proxy is
returned to the caller and the statement would die at the end of the accessor. Approach A fixes this
by making the proxy **own** the statement:

- Change each proxy's `Statement& stmt;` → `Statement stmt;` (by value).
- Change each `query(...)` factory to move a freshly-constructed statement into the proxy:
  `return {std::move(*this), …};` (the factory runs on a temporary statement).
- The QuerySet method constructs the statement and routes through `query()`. Example — `select()`:

  ```cpp
  // before
  return get_select_statement().query(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_);
  // after
  return SelectStatement<T, ConnType>(conn_)
          .query(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_);
  ```

Proxy + statement now share one lifetime and both die at the end of the `qs.select().execute()`
full-expression. No dangling.

**Movability requirement:** once the L2 raw-pointer/string members are deleted, each statement holds
only `std::shared_ptr<ConnType> conn_` plus compile-time constants, so the implicit move constructor
is correct and cheap (one shared_ptr move). Each class will be confirmed movable (no deleted or
hand-written special members that would break the implicit move) during implementation.

**Unaffected:** the `[[clang::lifetimebound]]` annotations on insert/update `query(obj/objects)` args
bind the *user's data* to the proxy, not the statement — they stay as-is.

## Data flow & error handling

After collapse: `qs.select()` → construct `SelectStatement(conn_)` (a shared_ptr move) → `.query(...)`
moves it into a `Query` proxy → `.execute()` → `conn_->prepare_cached(sql)` (the surviving L3 lookup,
mutex-guarded) → bind → step → materialize. Results, SQL, and error propagation are unchanged — every
path still returns `std::expected<…, Error>` and `prepare_cached` failures propagate exactly as
before. The deleted fast-paths were pure optimizations, not error sources.

## Thread safety

The collapse is **strictly safer** than the cached model, and the per-thread-connection guidance is
unchanged.

- **L3 is the only shared mutable state, and it is already thread-safe.**
  `Connection::statement_cache_` is protected by a `std::shared_mutex` (#271, commit 0fe2194);
  `prepare_cached()` takes the lock. The collapsed path touches no other shared mutable state.
- **L1/L2 were the unsynchronized parts — removing them removes a hazard.** The deleted L1
  `unique_ptr` members and L2 raw `cached_stmt_` pointers were per-object mutable state with no
  synchronization; they are the reason the safety model is "per-thread connections, never share a
  QuerySet." A per-call stack temporary is owned by exactly one thread's stack frame and never
  shared, so this class of latent dangling/race state is eliminated, not relocated.
- **`conn_` is a `std::shared_ptr<ConnType>`** — copying it into each per-call statement is an atomic
  refcount bump. The pointee Connection's only mutable shared state is the mutex-guarded L3 cache.
- **No new sharing semantics.** This does not make it safe to share one QuerySet across threads —
  that was never the model and is not a goal. The guidance "use per-thread connections" stands. The
  change only removes a latent per-object cache hazard.

Required proof: the existing `ninja-tsan` job (which runs #271's concurrent
`prepare_cached`/`clear_statement_cache` tests) must stay clean after the collapse.

## Testing & gates

The existing suite (1929 tests, SQLite + PostgreSQL) is the behavioral spec. This is a refactor of
covered code: **all tests stay green at every commit.** No new behavioral tests are added — we remove
caching without changing observable behavior, and testing "a statement is constructed per call" would
test internals (violates the surgical-change rule).

Required gates after the collapse:
- `ninja-asan-ubsan` (object-lifetime change → UAF/lifetime is the exact risk class) — clean.
- `ninja-tsan` — clean (see Thread safety).
- Full `ctest --preset ninja-debug` (SQLite + PostgreSQL) — all pass.
- Coverage stays 100%.
- **Core 8 Release re-benchmark** — the `#ifdef` bypass left dead members in place; the real deletion
  shifts struct layout/inlining, so this must be measured fresh on the project's standard regression
  filter (Release), confirming ±5%. NOT assumed from the investigation numbers.

## Sequencing

Branch: `feature/214-collapse-l1-l2` off `develop` (the investigation branch stays as the evidence
artifact). Linked to a GitHub issue (reuse #214 or a dedicated "implement Option C" issue — decided at
plan time).

Incremental order, tests green at every commit:
1. **L2, statement-by-statement** — one commit per class (Select → Update → Insert → Erase → Distinct
   → Aggregate): delete cached members + fast-paths, convert proxies to own-by-value, run tests.
   L2-before-L1 keeps each class self-contained per commit.
2. **L1** — delete the four QuerySet members + accessors, rewrite the 12 call sites to
   construct-and-move. One commit, tests green.
3. **Remove scaffolding** — delete the flags, `cache_probe.cpp`, the `storm_cache_probe` target.
4. **Gates** — asan-ubsan, tsan, ctest, coverage, Core 8 re-bench.
5. **Docs** — update `docs/architecture/STATEMENT_CACHING.md` (now single-level); rewrite #273 to
   "LRU + stats on the single L3 map" (the automatic-L2-invalidation requirement is gone).

## Success criteria

1. No `cached_*` member or `get_*_statement()` accessor remains; QuerySet holds no statement members.
2. Every statement proxy owns its statement by value; no proxy holds a `Statement&`.
3. `STORM_DISABLE_L1/L2`, `cache_probe.cpp`, and `storm_cache_probe` are deleted.
4. All gates pass: asan-ubsan, tsan, full ctest, 100% coverage, Core 8 within ±5% (Release).
5. `STATEMENT_CACHING.md` describes a single L3 cache; #273 rewritten.
