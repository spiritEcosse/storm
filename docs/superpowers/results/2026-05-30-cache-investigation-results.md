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

Multi-row scenarios — 10 repetitions:

| Scenario    |    all-3 |    no-L2 |    no-L1 |  L3-only | L3-only Δ vs all-3 | all-3 stddev |
|-------------|---------:|---------:|---------:|---------:|-------------------:|-------------:|
| Reuse       |  160 518 |  154 745 |  157 488 |  155 695 |          **−3.00%** |  2 983 (1.86%) |
| NewPerOp    |  159 085 |  155 565 |  157 813 |  157 586 |          **−0.94%** |    775 (0.49%) |
| MixedWhere  |  106 483 |  106 022 |  106 376 |  106 381 |          **−0.10%** |    496 (0.47%) |
| BulkUpdate  |  849 209 |  856 313 |  844 366 |  851 551 |          **+0.28%** |  3 784 (0.45%) |

Single-row tight-loop scenario — 20 repetitions (added after the first pass to close the
statement-setup-cost gap below):

| Scenario    |    all-3 |    no-L2 |    no-L1 |  L3-only | L3-only Δ vs all-3 | all-3 stddev |
|-------------|---------:|---------:|---------:|---------:|-------------------:|-------------:|
| GetByPk     |    707.4 |    693.8 |    701.8 |    699.3 |          **−1.15%** |   15.0 (2.12%) |

Negative Δ means L3-only is *faster* than all-3. **Every delta in every scenario is at or below
the measurement noise floor** — the largest (Reuse −3.00%, a *speedup*) is barely above its 1.86%
stddev. On the single-row path all four configs sit within 707±14 ns of each other.

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

## The single-row path was the real test — and it agrees

The four multi-row scenarios are dominated by **row materialization** (stepping result rows into
`plf::hive<Person>`, ~106–160µs), not statement setup — so they are under-sensitive to exactly what
L1/L2 optimize. Storm's own performance docs attribute "~23% to statement-pointer caching"
specifically to **single-row operations in tight loops**. To avoid a premature conclusion, a
**`CacheProbe/GetByPk`** scenario was added: `qs.where(id == pk).get()` in a tight loop, one row per
call (~700 ns), where statement setup *is* a large fraction of total time.

**The documented ~23% did not reproduce.** On GetByPk, all four configs land within 707±14 ns
(L3-only −1.15% vs all-3, inside the 2.12% noise floor). The likely reason: L3 `prepare_cached()`
already returns a ready-to-use prepared statement after one short-string hash lookup, and that
lookup is cheap next to bind + step + one-row extract. L1/L2 were saving a cost that L3 plus the
optimizer already absorb.

**Conclusion is therefore unconditional.** Across multi-row queries, bulk writes, *and* single-row
tight loops, L1 and L2 provide no measurable benefit. This is a clean **Option C**.

## Recommendation

1. **Option C — collapse to a single L3 cache.** Remove the L1 (`QuerySet` `unique_ptr` statement
   members + `get_*_statement()` lazy-init) and L2 (`cached_*_stmt_` pointers + `cached_sql_` +
   `cached_where_addr_`) machinery. L3 carries the workload alone with no regression.
2. **Re-benchmark the Core 8 filter after the collapse** (Release, the project's standard
   regression gate) to confirm the real deletion — not just the `#ifdef` bypass — stays within ±5%.
   The `#ifdef` bypass leaves the now-dead member variables in place; deleting them could shift
   struct layout/inlining slightly, so the post-collapse build must be re-measured, not assumed.
3. **Rewrite #273.** With L2 gone, #273's hardest requirement — "L2 pointer invalidation on eviction
   is automatic, not manual" — disappears entirely. #273 reduces to **LRU eviction + hit/miss/eviction
   stats on the single L3 `unordered_map`**. The dangling-pointer class it worried about no longer
   exists once no L2 raw pointers are held.

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
  # multi-row scenarios (10 reps)
  ./build/cache-$name/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_$name.json
  # single-row scenario (20 reps, sub-µs needs more samples)
  ./build/cache-$name/benchmarks/storm_cache_probe \
    --benchmark_filter='CacheProbe/GetByPk' \
    --benchmark_repetitions=20 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/getbypk_$name.json
done
```
