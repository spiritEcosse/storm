# #214 Cache-Architecture Investigation — Phase 1 Results

**Date:** 2026-05-30
**Branch:** `feature/214-cache-investigation`
**Spec:** `docs/superpowers/specs/2026-05-30-cache-architecture-investigation-design.md`
**Plan:** `docs/superpowers/plans/2026-05-30-cache-architecture-investigation.md`

## What was measured

Four caching configurations, produced via compile-time `#ifdef` guards
(`STORM_DISABLE_L1`, `STORM_DISABLE_L2`) on the cache-reuse branches, each built as a
separate `ninja-release` binary and run with `--benchmark_repetitions=10`
(median + stddev). SQLite `:memory:`, 1000 seeded `Person` rows.

- **L1** = `QuerySet` `unique_ptr` statement cache (avoids re-allocating the C++ Statement object).
- **L2** = `Statement::cached_stmt_` raw pointer (avoids the L3 hash lookup).
- **L3** = `Connection::statement_cache_` (avoids SQL parse/plan — the documented 100x).

| Config   | Flags                                   |
|----------|-----------------------------------------|
| all-3    | (none) — production behavior            |
| no-L2    | `STORM_DISABLE_L2`                       |
| no-L1    | `STORM_DISABLE_L1`                       |
| L3-only  | `STORM_DISABLE_L1 STORM_DISABLE_L2`      |

## Results (median real_time, ns)

| Scenario    |    all-3 |    no-L2 |    no-L1 |  L3-only | L3-only Δ vs all-3 | all-3 stddev |
|-------------|---------:|---------:|---------:|---------:|-------------------:|-------------:|
| Reuse       |  160 518 |  154 745 |  157 488 |  155 695 |          **−3.00%** |  2 983 (1.86%) |
| NewPerOp    |  159 085 |  155 565 |  157 813 |  157 586 |          **−0.94%** |    775 (0.49%) |
| MixedWhere  |  106 483 |  106 022 |  106 376 |  106 381 |          **−0.10%** |    496 (0.47%) |
| BulkUpdate  |  849 209 |  856 313 |  844 366 |  851 551 |          **+0.28%** |  3 784 (0.45%) |

Negative Δ means L3-only is *faster* than all-3. Every delta is at or below the
measurement noise floor (the largest, Reuse −3.00%, is barely above its 1.86% stddev
and is a *speedup*, not a regression).

## Interpretation

1. **L2 gives no measurable benefit on these workloads.** no-L2 is within noise of all-3
   on every scenario. The raw-pointer cache saves one `unordered_map` hash lookup per call;
   that cost is invisible next to query execution.
2. **L1 gives no measurable benefit on these workloads.** no-L1 (fresh statement object per op)
   matches all-3. **Reuse and NewPerOp are statistically identical in every config** — the
   scenario specifically built to reward L1 (reusing one QuerySet) shows no gain over
   constructing a fresh QuerySet each call.
3. **L3-only is within ±3% of all-3 everywhere.** The #214 simplification bar (<5% regression
   for L3-only) is met with margin.

Per the #214 decision matrix, this points to **Option C — collapse to a single L3 cache** (remove
L1 and L2).

## Honest limitation (READ BEFORE ACTING)

These four scenarios are **multi-row** selects plus a bulk update. Per-query time
(~106–160µs for selects) is dominated by **row materialization** — stepping result rows into
`plf::hive<Person>` — not by statement setup. L1/L2 only save *statement setup* cost, which is a
small fraction of a multi-row query. So this bench is **under-sensitive to the exact workload
L1/L2 were built to optimize.**

The project's own performance docs attribute "~23% to statement-pointer caching" specifically to
**single-row operations in tight loops** (e.g. `get()`/`first()` by primary key returning one row),
where statement setup *is* a large fraction of total time. **No such scenario is in this Phase 1
bench.** Note also that the L2 guards in this investigation *do* cover the `execute_one_fast` /
`execute_get_fast` single-row paths (`cached_first_stmt_` / `cached_get_stmt_`), so the
instrumentation is ready — but no benchmark exercises them yet.

**Conclusion is therefore conditional:** Option C is correct *for multi-row query and bulk-write
workloads*. Before deleting L1/L2 we should add a **single-row tight-loop scenario** (PK `get()` /
`first()` returning one row, thousands of iterations) and confirm L3-only stays within 5% there
too. If L2 shows its documented ~23% on that path, the decision shifts to **Option B** (keep L2 for
single-row, drop L1) or **Option A** (keep both, document the single-row case).

## Recommendation

1. **Do not delete L1/L2 yet.** The multi-row evidence favors Option C, but the single-row path —
   the one the docs say L2 matters for — is untested.
2. **Add a single-row tight-loop scenario** (`CacheProbe/GetByPk`: `qs.get(pk)` / `first()` in the
   loop) to `cache_probe.cpp` and re-run all four configs. That one scenario decides between
   Option C (collapse) and Option B/A (keep L2).
3. **Then, and only then, rewrite #273.** If the final decision is Option C, #273's "automatic L2
   pointer invalidation on eviction" requirement disappears and it reduces to "LRU + stats on the
   single L3 map." If Option B, #273 stays largely as written. **#273 should not be edited or
   implemented until this single-row data exists.**

## Flag lifecycle (decided with the user)

The `STORM_DISABLE_L1` / `STORM_DISABLE_L2` flags are **investigation scaffolding**, kept on
`feature/214-cache-investigation` only. They are **never merged to develop as toggles**. After the
decision:
- **Option C** → the flags are removed and replaced by the real deletion of the L1/L2 code paths.
- **Option A/B** → the flags are simply stripped (no reason to keep a disable-toggle for a cache we
  keep).

## Reproduction

```bash
for cfg in "all3:" "noL2:-DSTORM_DISABLE_L2" "noL1:-DSTORM_DISABLE_L1" "l3only:-DSTORM_DISABLE_L1 -DSTORM_DISABLE_L2"; do
  name=${cfg%%:*}; flags=${cfg#*:}
  cmake --preset ninja-release -B build/cache-$name -DCMAKE_CXX_FLAGS="$flags"
  cmake --build build/cache-$name --target storm_cache_probe -j1   # -j1 once: scan-deps race
  ./build/cache-$name/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_$name.json
done
```
