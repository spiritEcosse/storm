---
description: Run Storm benchmarks correctly — Release build, A/B interleaving, cv trust-check, flag verification
allowed-tools: Bash, Read, Grep
---

Benchmark Storm ORM the right way. Release builds only. The point of this command is the
protocol below — it encodes the noise-control rules that are easy to get wrong and produce
fake regressions/speedups.

Usage:
- /benchmark                      - Full run on the current checkout (single-binary)
- /benchmark <filter>            - Filtered run, e.g. /benchmark 'Storm/INSERT/.*'
- /benchmark ab <filter>         - A/B compare current branch vs develop (interleaved)

## Build (Release ONLY — never bench Debug)
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
```

## Single-binary run
```bash
./build/release/benchmarks/storm_bench                                    # full
./build/release/benchmarks/storm_bench --benchmark_filter='<filter>'      # filtered
./build/release/benchmarks/storm_bench --benchmark_repetitions=10         # median/mean/stddev/cv
./build/release/benchmarks/storm_anchors                                  # raw SQLite anchors
```

### Reading the aggregates (the trust check)
- `_median` = the latency to trust. If `_mean` ≫ `_median`, a fat outlier is dragging the
  mean — investigate before trusting it.
- `_cv = stddev/mean` = the headline TRUST number. Storm's gates fire at **5%**.
  - **cv ≪ 5% → trustworthy.** cv ≈/> 5% → chasing ghosts: bump `--benchmark_repetitions`,
    pin the machine (gbench's "CPU scaling / ASLR enabled" warning is what inflates cv).
- Never claim a sub-5% improvement or regression as real unless cv sits comfortably below it.

## A/B compare (current branch vs develop)

**Trap 1 — verify build flags match FIRST.** A fresh worktree configure can leave
`CMAKE_CXX_FLAGS_RELEASE` EMPTY (no `-O3`), making develop look ~3.5× slower — a fake
"huge speedup". Any A/B swing >2× is environment/flags, not code.
```bash
grep CMAKE_CXX_FLAGS_RELEASE build/release/CMakeCache.txt <develop-build>/CMakeCache.txt
# both must read: -O3 -DNDEBUG   (force with -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" + rebuild)
```
Worktree gotcha: the compiler path `${sourceDir}/../clang-p2996` must resolve — symlink it if
the worktree lives outside `~/projects/storm/`.

**Trap 2 — INTERLEAVE the runs.** Running all develop reps THEN all feature reps gives false
regressions: thermal/scheduler drift hands one side lucky-low samples. At the 5% gate a ~10ns
fixed delta on a 600ns op is 1.6% — well inside cross-run variance. Only interleaving cancels
the slow common-mode drift.
```bash
# 5 rounds, each round runs BOTH binaries; take the median-of-medians
for i in 1 2 3 4 5; do
  for tag in dev feat; do
    bin=$([ "$tag" = dev ] && echo <develop-bin> || echo ./build/release/benchmarks/storm_bench)
    echo "round $i $tag:"; "$bin" --benchmark_filter='<filter>' --benchmark_repetitions=12 \
      --benchmark_report_aggregates_only=true
  done
done
```
Trust the median-of-medians, not a single non-interleaved pair. If one round shows dev cv=0.4%
but feat cv=4%, that round is noise — re-interleave.

## `% of raw` sanity
`% of raw = raw_ns / storm_ns`. If it suddenly halves/doubles right after a PR that renamed or
changed a raw **anchor's** workload, suspect a **stale per-branch raw baseline**, not a code
regression — a merge does NOT re-record anchors. Re-record the anchor on the current branch and
restart the dashboard so `raw:last` re-locks. (See memory: stale-raw-baseline.)

## After bench-only changes
If `git status --short` shows every modified path under `benchmarks/`, a Release build +
representative run is sufficient — do NOT run sanitizer presets (they build from src/+tests/,
which didn't change). Mixed changes still require the CI sanitizer gate.
