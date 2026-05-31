# #214 Cache-Architecture Investigation — Phase 1 Benchmarks

**Date:** 2026-05-30
**Issue:** #214 (`[INVESTIGATION] Evaluate 3-level caching architecture for potential simplification`)
**Related:** #215 (parent), #271 (L3 mutex, closed), #273 (LRU eviction, open — scope depends on this investigation's outcome)

## Motivation

#214 asks whether Storm's 3-level statement caching is over-engineered: do L1 and L2 provide
meaningful benefit given that L3 (Connection statement pool) avoids SQL parsing — the expensive
part. The answer gates #273: if L2 is removed (Option B/C), #273's hardest requirement
("L2 pointer invalidation on eviction is automatic") disappears and the issue must be rewritten.

This spec covers **Phase 1 only**: build the four caching configurations, benchmark them across
four scenarios, and produce a results table plus an Option A/B/C/D recommendation. No cache code
is collapsed and no issues are edited in this phase — the deliverable is numbers + a recommendation.

## The three cache levels (as they exist in code)

- **L1** — `QuerySet::select_stmt_` / `update_stmt_`: lazy-init `unique_ptr` in
  `get_select_statement()` / `get_update_statement()` (`src/orm/queryset.cppm:543–556`).
  Avoids re-allocating the C++ Statement object. Only helps when the *same QuerySet instance*
  is reused.
- **L2** — `SelectStatement::cached_stmt_` (raw `Statement*`) + `cached_sql_`
  (`src/orm/statements/select.cppm:463–476`, member at `:637`; analogous members on
  update/aggregate). Avoids the L3 hash lookup when the SQL string is unchanged.
- **L3** — `Connection::statement_cache_`: `unordered_map<string, unique_ptr<Statement>>`,
  shared_mutex-protected since #271 (`src/db/sqlite.cppm`, `src/db/postgresql.cppm`).
  Avoids SQL parse/plan compilation — the dominant cost (the documented 100x).

## Producing the four configurations (compile-time)

L1/L2 are compiled-in, not runtime toggles, so variants are produced with `#ifdef` guards on
the **cache-reuse branches** (not by adding runtime branches — that would contaminate the hot
path we are measuring). Guards live in `src/` on the feature branch only; with flags off the
guarded branches compile out, so the default build is behaviorally unchanged.

- `STORM_DISABLE_L2` — in the L2 fast-path (`prepare_and_bind` / `get_or_prepare` in
  `select.cppm`, equivalents in update/aggregate), force the `cached_stmt_ == nullptr` branch
  every call so each op goes straight to `conn_->prepare_cached()`.
- `STORM_DISABLE_L1` — make `get_select_statement()` / `get_update_statement()` always
  `make_unique` a fresh statement (no member reuse). A fresh statement has a null `cached_stmt_`,
  so this **implies no L2 reuse too** — which is exactly #214's intended "no-L1" semantics.

| Config       | Flags                                   | Meaning                              |
|--------------|-----------------------------------------|--------------------------------------|
| all-3        | (none)                                   | current production behavior          |
| no-L2        | `STORM_DISABLE_L2`                        | L1 + L3 only                         |
| no-L1        | `STORM_DISABLE_L1`                        | fresh stmt each op ⇒ effectively L3-only path, but L1 alloc still skipped per-op |
| L3-only      | `STORM_DISABLE_L1` + `STORM_DISABLE_L2`   | rely solely on Connection pool       |

## Benchmark scenarios

New bench file `benchmarks/cache_investigation.cppm` (feature branch only, not destined for
develop). Each scenario runs against all four configs (16 measurement sets total). Latency
(ns/op or ms/query), Release build, `--benchmark_repetitions=10` for median/stddev. Fair-benchmark
rules apply: setup outside the loop, execute inside, identical logic on both sides.

1. **Reuse QuerySet** — one `QuerySet<Person>` instance, N `select()` ops in the loop.
   Maximum L1 benefit; where L1 should look best.
2. **New QuerySet per op** — fresh `QuerySet<Person>` constructed inside the loop, one `select()`
   each. Zero L1 benefit by construction; the key data point for Option C.
3. **Mixed / varying WHERE** — rotating WHERE value so the SQL string changes, exercising
   L2 miss → L3 hit/miss interplay.
4. **Bulk CRUD** — batch INSERT/UPDATE/DELETE over a dataset; the path where statement reuse
   across chunk iterations matters.

## Execution flow

1. `gh issue develop 214 --name feature/214-cache-investigation --base develop --checkout`.
2. Add the two `#ifdef` guards (L1, L2) + the new bench file; commit scaffolding.
3. For each of the four configs: configure `ninja-release` with the appropriate `-D` flags,
   build, run `storm_bench --benchmark_filter='CacheInvestigation/.*' --benchmark_repetitions=10`,
   capture JSON to `/tmp`.
4. Collate a results table: per-scenario, per-config median + % delta vs the all-3 baseline.
5. Map deltas to #214's decision matrix.

## Decision matrix (#214)

- L3-only within **5%** of all-3 across all scenarios → **Option C** (collapse to single L3 cache).
- L2 contributes **<2%** but L1 matters → **Option B** (remove L2 only).
- L1 or L2 contributes **>5%** in any scenario → **Option A** (keep all three; document where they matter).

## Scope boundaries

- **In scope:** the four variant builds, the bench file, the results table, an Option A/B/C/D
  recommendation, and committing the scaffolding to `feature/214-cache-investigation`.
- **Out of scope (this phase):** editing #273, implementing any collapse, merging to develop.
- **No sanitizer runs:** bench-only, Release-only; the default (unflagged) build has no behavior
  change. Confirm the default build's guarded branches are dead code (flags off) rather than
  running the sanitizer matrix.

## Success criteria

1. All four configs build cleanly under `ninja-release`.
2. Benchmark JSON captured for all 16 scenario×config combinations (≥10 repetitions each).
3. Results table with per-scenario median + % delta vs all-3 baseline.
4. A written Option A/B/C/D recommendation grounded in the measured deltas.
5. Scaffolding committed to `feature/214-cache-investigation`; develop untouched.
