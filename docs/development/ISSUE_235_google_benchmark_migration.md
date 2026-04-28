# Issue #235 — Migrate to Google Benchmark, drop raw SQLite comparisons

## Decision

**Drop raw SQLite comparisons. Migrate to Google Benchmark for Storm ORM only.**

The "is the abstraction free?" question is already settled at 96–108% efficiency vs raw SQLite (CLAUDE.md, `docs/benchmarks/`). Re-running that comparison every benchmark invocation adds maintenance cost without informing day-to-day work. Google Benchmark's built-in `Range()` / `Complexity()` / `SetItemsProcessed()` give us better signals (scaling curves, per-row cost, throughput, regression detection) in absolute terms.

**One concession**: keep a *small, sparse* set of raw SQLite anchor benchmarks (single-row INSERT, ~1K-row SELECT) so we can spot-check the efficiency claim during a release without re-instrumenting. Anchors are standalone — no per-Storm-benchmark pairing.

## Goals

- Replace `benchmarks/runner.hpp` (custom timing + raw comparison) with Google Benchmark `BENCHMARK_REGISTER_F` / `BENCHMARK_TEMPLATE_F`.
- Use Google Benchmark's statistics (median, mean, stddev, min/max, CV) and `--benchmark_repetitions` instead of our hand-rolled loop in `runner.hpp:204–309`.
- Use `->Range(1 << 0, 1 << 17)` + `->Complexity(benchmark::oN)` instead of our `dispatch_sized` size-profile machinery (`runner.hpp:325–367`, `benchmarks/sizes.cppm`).
- Delete `benchmarks/raw.cppm` (250 lines of reflection-based raw helpers) once anchors are converted to direct C-API.
- Cancel the `ISSUE_refactor_raw_bindings.md` follow-up — its motivation disappears once `raw.cppm` is gone.

## Out of scope (explicit)

- Test definition format. `benchmarks/tests/benchmark_tests.yaml` + `parser.cppm` + compile-time `#embed` dispatch is **kept**. Google Benchmark replaces only the runner; the YAML-driven test catalog stays as the source of truth so Storm's compile-time SQL generation continues to be exercised at the call sites.
- The `models.hpp` / `registry.cppm` / `schema.cppm` model layer.
- The `benchmark_tests.json` codegen step.
- `--smoke` / `--quick` / `--thorough` modes — these become Google Benchmark CLI flag presets (`--benchmark_min_time=`, `--benchmark_repetitions=`) wrapped by a thin shell script.

## Plan

### Phase 1 — Add Google Benchmark dep + smoke harness  *(no behavior change)*

1. CPM-fetch `google/benchmark` pinned to **`v1.9.5`** in `cmake/cpm.cmake`.
2. New target `storm_gbench` in `benchmarks/CMakeLists.txt` alongside the existing `storm_bench` — both build and pass.
3. New file `benchmarks/gbench_main.cpp` with one trivial Storm fixture (e.g. a `Person` SELECT-by-PK over 1K rows) using `BENCHMARK_F` + `->Range()` + `->Complexity()`. Proves the toolchain and module imports work under Google Benchmark.

**Verify**: `./build/release/benchmarks/storm_gbench --benchmark_min_time=0.5s` runs and prints a complexity report.

### Phase 2 — Port one operation family end-to-end  *(SELECT)*  — **DONE 2026-04-28**

4. Port `QueryBenchmark<Model, test>` to a benchmark-local module `storm_benchmark_query` (was textual header `operations/query_benchmark.hpp`). Storm-only execution — `execute_raw` / `bind_raw_params` / `step_raw` / `bind_*_params` paths dropped. Public surface narrowed to `prepare(int)` + `run_once()`.
5. Drive registration from `BENCHMARK_TESTS` via runtime `benchmark::RegisterBenchmark(name, lambda)` rather than the `BENCHMARK_TEMPLATE_F` macro. The YAML `size_profile` field stays in the schema; Phase 2 maps the existing strings to ranges in C++ instead of churning the YAML:
   - `dataset_standard` → `RangeMultiplier(10)->Range(100, 100'000)` → `{100, 1k, 10k, 100k}`
   - `dataset_small` → `RangeMultiplier(5)->Range(10'000, 50'000)` → `{10k, 50k}`
   - (no profile / CRUD profiles) → fixed-size `Arg(dataset_size)`
   The `(range_min, range_max, multiplier)` YAML rename is deferred — `sizes.cppm` is no longer referenced by SELECT-family tests but stays in-tree for Phase 3 (CRUD batch profiles still use `batch_standard` / `batch_*_edge`).
6. `state.SetComplexityN(state.range(0))` + `state.SetItemsProcessed(...)` are emitted unconditionally; sized fixtures additionally chain `->Complexity(benchmark::oN)`. Verified against `Storm/SELECT/select` sweep — fits `203.62 N, RMS 3%` over `{100, 1k, 10k, 100k}`.

#### Build-system note: TU split for PCM-cache hash isolation

Putting `<benchmark/benchmark.h>` and `import storm_*` into the same TU diverges its module-cache hash from libstorm.a's (the `-Igoogle-benchmark/include` and `-DBENCHMARK_STATIC_DEFINE` flags fold into the cache subdir hash). That trips `module '_Builtin_stddef' is defined in both ...` at the first `import storm` site (memory: `project_pcm_hash_divergence`). Phase 2 splits the entry binary into:

- **`benchmarks/main.cpp`** — owns `<benchmark/benchmark.h>` + `BENCHMARK_MAIN`-equivalent glue. No imports. Per-source bench include path / `BENCHMARK_STATIC_DEFINE`.
- **`benchmarks/register.cpp`** — owns `import storm; import storm_benchmark_query; ...` + the textual `#include "benchmark_tests.hpp"` (`#embed` driver). No bench flags.
- **`benchmarks/bench_register.h`** — POD-only bridge: a `std::vector<RegisteredBenchmark>{name, setup_fn_ptr, run_fn_ptr, range...}`. Includes neither `<benchmark/...>` nor any storm header.

Each `(Model, test)` pair gets a `Trampoline<Model, test>` whose function-local `std::optional<QueryBenchmark<Model, test>>` is rebound by `setup(N)` and read by `run()`. main.cpp wraps each registered entry in a stable lambda calling those trampoline pointers.

Two additional gotchas hit in Phase 2:
- `query_benchmark.cppm` and `register.cpp` both need `#include <tuple>` + `#include "models.hpp"` in their GMF before any `import` — model-struct annotations are blind across BMI boundaries (`feedback_cpp26_module_reflection_annotations`), so any TU that instantiates an ORM template parameterized on a model needs the annotated definition textually visible.
- `register.cpp` needs explicit `import <expected>; import <algorithm>; import <ranges>;` even though it never uses those names directly — they're required for partial-specialization reachability when the consteval `where_model_` lambda inside `QueryBenchmark` is instantiated against the consumer's import set.

**Verified**: `./build/release/benchmarks/storm_bench --benchmark_list_tests` reports 146 entries (75 SELECT-family tests × Range steps); spot-checked SELECT, JOIN (left/right/multi-FK), aggregates (count/sum/avg/min/max), DISTINCT, GROUP BY (with HAVING), set operations, ORDER BY, LIMIT — all produce real timings + items/sec, sized fixtures emit `BigO` + `RMS` annotations.

### Phase 3 — Port INSERT / UPDATE / DELETE

7. Port `InsertBenchmark`, `UpdateBenchmark`, `DeleteBenchmark` (`benchmarks/operations/*.hpp`). These currently have explicit `prepare(int iterations)` calls in `runner.hpp:212` — map onto Google Benchmark's `SetUp(const State&)` / `TearDown(const State&)` fixture hooks.
8. Drop `execute_raw()` / `execute_raw_no_return()` paths from `benchmarks/operations/*.hpp` (lines per file: `insert.hpp:126,188`; same shape in `update.hpp` / `delete.hpp`).

**Verify**: Full YAML test catalog runs under `storm_gbench`. No reference to `raw.cppm` from any operation header.

### Phase 4 — Add raw SQLite anchors  *(intentional, sparse, separate target)*

9. New target **`storm_anchors`** in `benchmarks/CMakeLists.txt`, separate from `storm_bench`. Rationale: anchors measure raw SQLite, not Storm — keeping them in their own binary prevents contributors from accidentally treating them as comparison points to specific Storm runs, and their output stream stays out of regular `storm_bench` reports.
10. New file `benchmarks/anchors_raw.cpp` (the only TU for `storm_anchors`) with 4 Google Benchmark fixtures using direct `sqlite3_*` C API:
    - `Anchor_Raw_InsertSingleRow` (single-row INSERT throughput)
    - `Anchor_Raw_SelectByPK_1K` (1K-row PK lookup)
    - `Anchor_Raw_BatchInsert_1000` (batched INSERT at a representative size)
    - `Anchor_Raw_FullScan_10K` (sequential SELECT over 10K rows)
11. Anchors do **not** import `storm`, `storm_benchmark_raw`, or any benchmark module. They `#include <sqlite3.h>` directly (~50 LOC each, no reflection, no model dependency).
12. `storm_anchors` is **not** invoked by `compare_against_baseline.sh` — it's a release-time spot check, not a per-PR gate. Its baseline (if any) is tracked separately, manually.

**Verify**:
- `./build/release/benchmarks/storm_anchors` runs the 4 anchors; results within ±10% of pre-migration `runner.hpp` raw numbers.
- `./build/release/benchmarks/storm_bench --benchmark_list_tests` shows zero `Anchor_Raw_*` entries (anchors live in a separate binary).

### Phase 4b — Regression detection via committed baselines

**Approach**: Option A from the discussion thread — commit a JSON baseline per CMake preset, fail PRs that regress >5% with statistical significance. Defers external trend-tracking infrastructure (Bencher/CodSpeed) to a separate issue (see "Future external tracking" below).

13. Add `benchmarks/baselines/ninja-release.json` — committed JSON baseline produced by `storm_bench --benchmark_repetitions=10 --benchmark_format=json --benchmark_out=...` on a quiet machine.
14. Add `benchmarks/scripts/compare_against_baseline.sh` — thin wrapper around Google Benchmark's [`compare.py`](https://github.com/google/benchmark/blob/main/tools/compare.py) (vendored under `third_party/google-benchmark/tools/` via the CPM fetch in Phase 1).

    ```bash
    #!/usr/bin/env bash
    set -euo pipefail
    BUILD_DIR="${BUILD_DIR:-build/release}"
    BASELINE="${1:-benchmarks/baselines/ninja-release.json}"
    THRESHOLD="${REGRESSION_THRESHOLD:-1.05}"  # 5% slowdown
    "$BUILD_DIR/benchmarks/storm_bench" \
        --benchmark_repetitions=10 \
        --benchmark_format=json \
        --benchmark_out=current.json
    python3 third_party/google-benchmark/tools/compare.py \
        --threshold="$THRESHOLD" benchmarks "$BASELINE" current.json
    ```

15. Add `.github/workflows/benchmark.yml` — runs on PRs against `develop`. Builds release, runs `compare_against_baseline.sh`, parses output, fails the job on any benchmark with `>5%` slowdown at `p<0.05`.
16. Add a `re-baseline.sh` helper for manual rebaselining on `develop`:

    ```bash
    cmake --build --preset ninja-release
    ./build/release/benchmarks/storm_bench \
        --benchmark_repetitions=10 \
        --benchmark_format=json \
        --benchmark_out=benchmarks/baselines/ninja-release.json
    git add benchmarks/baselines/ninja-release.json
    git commit -m "bench: rebaseline after <reason>"
    ```

17. Document the rebaselining workflow in `benchmarks/README.md`: when to rebaseline (after intentional perf change, after toolchain bump, after release), how to interpret `compare.py` output, how to override threshold for a specific PR.

**Verify**:
- `compare_against_baseline.sh` exits 0 on `develop` HEAD (current code matches its own baseline).
- Synthetic regression test: add a `std::this_thread::sleep_for(1us)` to a hot path, rerun script, confirm it exits non-zero with the offending benchmark named.
- CI workflow blocks a draft PR carrying the synthetic regression.

**Known limitations** (deferred to the Docker/external-tracking issue):
- GitHub Actions free runners are noisy (±5–10% jitter). The 5% threshold catches real regressions but lets ±3% jitter through; expect occasional false positives.
- No trend graph — only "current vs. last committed baseline." History lives in `git log -p benchmarks/baselines/`.
- Manual rebaselining required after toolchain bumps. Easy to forget.

These limitations are the trigger for moving to a self-hosted runner or external service later — see the follow-up issue.

### Phase 5 — Delete the old runner

18. Delete `benchmarks/runner.hpp`, `benchmarks/timing.hpp`, `benchmarks/timing_trace.hpp`, `benchmarks/raw.cppm`, `benchmarks/main.cpp`.
19. Delete `benchmarks/sizes.cppm` (replaced by per-test range fields in YAML).
20. Rename `gbench_main.cpp` → `main.cpp`. Single benchmark binary `storm_bench`.
21. Cancel/close `docs/development/ISSUE_refactor_raw_bindings.md`.
22. Update `benchmarks/README.md` and `docs/development/PERFORMANCE_GUIDELINES.md` for the new CLI surface and remove `runner.hpp` references in CLAUDE.md / agent files.

**Verify**: `git grep -l raw.cppm` and `git grep -l runner.hpp` return zero hits in `benchmarks/`. Pre-commit hook still passes.

## Files to add / modify / delete

**Add**
- `benchmarks/gbench_main.cpp` (Phase 1, renamed to `main.cpp` in Phase 5)
- `benchmarks/anchors_raw.cpp` (Phase 4)
- `benchmarks/baselines/ninja-release.json` (Phase 4b)
- `benchmarks/scripts/compare_against_baseline.sh` (Phase 4b)
- `benchmarks/scripts/re-baseline.sh` (Phase 4b)
- `.github/workflows/benchmark.yml` (Phase 4b)
- This document

**Modify**
- `cmake/cpm.cmake` — add `google/benchmark`
- `benchmarks/CMakeLists.txt` — link Google Benchmark, switch target sources
- `benchmarks/operations/*.hpp` — replace custom `execute()` / `execute_raw()` shape with Google Benchmark fixture methods
- `benchmarks/parser.cppm` — replace `size_profile` string with `(range_min, range_max, multiplier)` fields
- `benchmarks/tests/benchmark_tests.yaml` — same field rename
- `benchmarks/README.md`, `docs/development/PERFORMANCE_GUIDELINES.md`, `CLAUDE.md`

**Delete**
- `benchmarks/runner.hpp` (556 LOC)
- `benchmarks/raw.cppm` (250 LOC)
- `benchmarks/sizes.cppm` (161 LOC)
- `benchmarks/timing.hpp` + `benchmarks/timing_trace.hpp` (246 LOC)
- `benchmarks/main.cpp` (225 LOC, replaced by gbench_main.cpp)
- `docs/development/ISSUE_refactor_raw_bindings.md`

**Net delta**: ~−1,400 LOC of custom runner / raw-helpers / timing infra; ~+250 LOC of Google Benchmark fixtures and 4 raw anchors.

## Success criteria

- `storm_bench --benchmark_filter=SELECT --benchmark_repetitions=5 --benchmark_report_aggregates_only=true` produces median/mean/stddev for every SELECT test.
- `storm_bench --benchmark_filter=insert.*` shows `Complexity` annotations (e.g. `O(N)` with coefficient).
- `storm_bench --benchmark_filter=Anchor_Raw_` runs the 4 anchors; results match pre-migration raw numbers within 10%.
- Documented Storm-vs-raw efficiency claim in `CLAUDE.md` is now sourced from anchor runs (release-time, not per-commit).
- Pre-commit hook still passes; SonarCloud Storm Strict gate still clean on the migration PR.

## Risks

| Risk | Mitigation |
|---|---|
| Lose per-test raw delta | Anchors give a periodic check; absolute regression detection via Google Benchmark's `--benchmark_format=json` + CI diff is stricter anyway |
| Google Benchmark micro-overhead skews tiny-op results | `->MinTime(0.5)` per fixture; rely on per-iteration cost (`state.SetItemsProcessed`) not wall time |
| YAML-driven `BENCHMARK_TEMPLATE` registrations explode template instantiation budget | Phase 2 ports SELECT first to measure compile time before committing to all families |
| C++26 module + Google Benchmark interaction (their headers use macros) | Keep `<benchmark/benchmark.h>` in the GMF of `gbench_main.cpp` only; never `import` it from a `.cppm` |

## Resolved decisions

1. **Google Benchmark pin**: `v1.9.5` (CPM-fetched in `cmake/cpm.cmake`).
2. **Anchor binary**: separate `storm_anchors` target — anchors do not import `storm` and are not invoked by the per-PR regression workflow.
3. **CI regression threshold**: `>5%` slowdown at `p<0.05` (configurable via `REGRESSION_THRESHOLD` env var in `compare_against_baseline.sh`).

## Future external tracking (deferred)

Phase 4b uses committed JSON baselines because it needs zero infrastructure and works today. The known limitations (CI noise, no trend graphs, manual rebaselining) become the trigger for moving to a self-hosted/Dockerized runner and external trend service.

Tracked in **#236** so the Google Benchmark migration can land independently:

- **Dockerize Storm CI toolchain** (Step 1 of #236) — prebuilt image with clang-p2996 + libc++ + cmake + ninja + sqlite3, published to GHCR. Cuts setup time per CI job from minutes to seconds; unlocks self-hosted runners; makes Storm reproducible for outside contributors.
- **External benchmark trend tracking** (Step 2 of #236, deferred) — once the Docker image exists, Bencher (walltime, runner-agnostic) becomes nearly free to wire up. CodSpeed Macro becomes plausible but carries Cachegrind-with-experimental-Clang risk. Revisit only if Phase 4b's false-positive rate is annoying *and* the project has outside contributors.
