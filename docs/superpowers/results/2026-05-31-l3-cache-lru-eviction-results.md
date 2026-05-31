# L3 Cache LRU Eviction — Bench Gate Results (#273)

**Date:** 2026-05-31
**Branch:** `feature/273-l3-cache-lru-eviction`
**Spec:** `docs/superpowers/specs/2026-05-31-l3-cache-lru-eviction-design.md`
**Decision:** **Keep default capacity 512.** No bench regression — the CLOCK
ref-bit write and the capacity check add no measurable hot-path cost.

## Method

`ninja-release` (`-O3 -DNDEBUG`, flags verified non-empty), Google Benchmark,
20 repetitions, `--benchmark_report_aggregates_only`, SQLite `:memory:`. Same
binary built on `develop` (baseline) and on the feature branch, compared on the
median `real_time`. The default capacity 512 is active on the feature branch
(eviction is live for these runs).

## Core write/read paths (N:1 / N:10 / N:100)

| benchmark | develop (ns) | branch (ns) | Δ% |
|---|---:|---:|---:|
| INSERT/insert/N:1 | 3069.2 | 3039.5 | −0.97 |
| INSERT/insert/N:10 | 4839.1 | 4808.1 | −0.64 |
| INSERT/insert/N:100 | 33942.8 | 34244.1 | +0.89 |
| INSERT/insert_no_return/N:1 | 2580.9 | 2540.2 | −1.58 |
| INSERT/insert_no_return/N:10 | 5089.3 | 5001.2 | −1.73 |
| INSERT/insert_no_return/N:100 | 34054.5 | 33895.7 | −0.47 |
| SELECT/select/N:100 | 21303.5 | 21300.6 | −0.01 |

## Single-row tight-loop paths (the #214-sensitive ones)

`UPDATE_PK/N:1` is the path that regressed +7.5% in the #214 collapse — the most
cache-hit-sensitive in the suite. Under #273 it is within noise (a slight
speedup).

| benchmark | develop (ns) | branch (ns) | Δ% |
|---|---:|---:|---:|
| UPDATE_PK/update_pk/N:1 | 873.0 | 865.1 | **−0.91** |
| UPDATE_PK/update_pk/N:10 | 5249.5 | 5284.0 | +0.66 |
| DELETE_PK/delete_pk/N:1 | 3673.3 | 3686.3 | +0.36 |
| DELETE_PK/delete_pk/N:10 | 11073.0 | 10895.4 | −1.60 |

## Verdict

Worst regression across all measured paths: **+0.89%** (INSERT/N:100), well
inside the ±5% gate. The single-row cache-hit hot path (`UPDATE_PK/N:1`) is
−0.91%. The CLOCK design — a relaxed atomic ref-bit store on hit, capacity
check + sweep only on insert-past-capacity under the write lock already held —
costs nothing measurable on the hit path, as intended.

**Default capacity 512 is kept.** No fall-back to unbounded (0) required.

## Sanitizers

ASAN+UBSAN and TSAN both 100% pass (1930 tests each, 0 failures) — the new
`std::atomic` ref bit / counters and the shared_mutex eviction path are
memory-safe and race-clean.
