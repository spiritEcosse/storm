# Stale Test/Benchmark Layout Sweep — Design (Part C)

**Date:** 2026-06-20
**Scope:** Part C of the docs task. A third staleness class the user caught
(starting from `benchmarks/common.py`): docs describe a DEAD test/benchmark file
layout and tooling. Same branch (`feature/docs-staleness-sweep`).

## Goal

Make every test-file path, benchmark binary name, benchmark CLI command, and
"how to add a benchmark/test" instruction in the docs match the CURRENT repo.

## Ground truth (verified against the repo)

**Benchmark binaries** (CMake targets in `benchmarks/CMakeLists.txt`):
- `storm_bench` — Google Benchmark entry point. Filtered with
  `--benchmark_filter='Storm/<CAT>/.*'`, `--benchmark_repetitions=N`. (NOT
  `--size`/`--iterations`/`--storm-join-1` — those custom flags do not exist.)
- `storm_anchors` — raw SQLite anchors (release-time spot check).
There is NO `bench_storm`, `bench_sqlite`, `bench_sqlite_orm`, `bench_join`,
`bench_where`, `bench_limit`, or per-feature `bench_<feature>.cpp`.

**Adding a benchmark** (per CLAUDE.md + `benchmarks/register.cpp`): add a YAML
entry under `benchmarks/tests/benchmark_tests.yaml`; `register.cpp` walks
`BENCHMARK_TESTS` and registers it automatically. There is NO `common.py` and no
per-feature `.cpp` benchmark file to create.

**Test layout**: one binary `storm_tests`; sources live in category subdirs:
- `tests/crud/test_crud.cpp`, `tests/crud/test_select.cpp`
- `tests/query/test_distinct.cpp`
- `tests/schema/test_fk_fields.cpp`
- (others under `tests/{db,errors,mock_sqlite,mock_libpq,tools,...}/`)
There is NO flat `tests/test_crud.cpp` / `tests/test_*.cpp` / `tests/test_<feature>.cpp`.

## NOT stale (verified — leave alone)
- `benchmarks/dashboard/storm_bench_dashboard` (BENCHMARK_DASHBOARD.md) — real
  build output; the `dashboard/` dir exists.
- `benchmark_tests.yaml` references — already correct.
- `ninja-*` presets — real.
- `storm_bench`/`storm_anchors` build-output paths — correct.

## Out of scope
- Hardcoded performance NUMBERS (deferred follow-up).
- `docs/superpowers/`, `docs/archive/`.

## Exact stale inventory (35 lines / 9 docs)

### A. Benchmark binary/command lines → `storm_bench` / `storm_anchors` + Google Benchmark flags
- `internals/compiler/COMPILER_ATTRIBUTES.md:228,234` — `bench_where --benchmark_min_time=2s` → `storm_bench --benchmark_filter='Storm/WHERE/.*' --benchmark_min_time=2s` (min_time IS a real Google Benchmark flag; keep it).
- `internals/performance/PERFORMANCE.md:349` `bench_storm` → `storm_bench`; `:416` `bench_limit --size=... --iterations=...` → `storm_bench --benchmark_filter='Storm/.*LIMIT.*'` (no bare `LIMIT` category — it's a compound suffix on SELECT_LIMIT/SELECT_LIMIT_OFFSET/etc.).
- `internals/performance/DISTINCT_ANALYSIS.md:146` `bench_storm` → `storm_bench --benchmark_filter='Storm/DISTINCT/.*'`.
- `internals/performance/JOIN_ANALYSIS.md:129-133` `bench_join --help/--size/--storm-join-1/...` → `storm_bench --benchmark_filter='Storm/.*JOIN.*'` (drop the non-existent custom flags; show `--benchmark_filter` variants; no bare `JOIN` category — it's a compound suffix on SELECT_JOIN/SELECT_LEFT_JOIN/etc., so the regex needs `.*JOIN.*`).
- `guide/features/BATCH_OPERATIONS.md:407,410,413` `bench_storm`/`bench_sqlite_orm`/`bench_sqlite` → `storm_bench` / `storm_anchors` (sqlite_orm anchor is part of anchors); `:416` `bench_storm.cpp` → point at `benchmarks/crud_benchmark.cppm` + `benchmark_tests.yaml`.
- `guide/features/JOIN_OPERATIONS.md:563-570` `bench_join ...` → `storm_bench --benchmark_filter='Storm/.*JOIN.*'`; `:573` `bench_join.cpp` → `benchmarks/query_benchmark.cppm` + YAML.
- `guide/features/SELECT_QUERIES.md:359,360` `bench_storm`/`bench_sqlite` → `storm_bench`/`storm_anchors`; `:363` `bench_storm.cpp` → `benchmarks/query_benchmark.cppm` + YAML.

### B. "Add a benchmark" workflow (the dead common.py / bench_<feature>.cpp model) → YAML model
- `internals/performance/PERFORMANCE.md:21,24,25,28,36,38,394,397` — the "Use common.py" + "bench_<feature>.cpp" + "bench_join.cpp --help" workflow → rewrite to: add a YAML entry in `benchmarks/tests/benchmark_tests.yaml`; `register.cpp` picks it up; run via `storm_bench --benchmark_filter=...`. Remove `common.py`.
- `internals/performance/PERFORMANCE.md:356` `**Benchmark file**: benchmarks/bench_<feature>.cpp` → `**Benchmark**: a YAML entry in benchmarks/tests/benchmark_tests.yaml`.
- `internals/building/ADDING_FEATURES.md:9` `Create performance benchmark in benchmarks/bench_<feature>.cpp` → `Add a benchmark YAML entry in benchmarks/tests/benchmark_tests.yaml`; `:129` `// benchmarks/bench_your_feature.cpp` + `:155` `bench_your_feature --size=...` → YAML + `storm_bench --benchmark_filter=...`.
- `internals/performance/PERFORMANCE.md:108` checklist `Create performance benchmark in benchmarks/bench_<feature>.cpp` → YAML-entry wording.

### C. Test-file paths → real subdir paths
- `guide/features/CRUD_OPERATIONS.md:491` `tests/test_crud.cpp` → `tests/crud/test_crud.cpp`.
- `guide/features/SELECT_QUERIES.md:339` `tests/test_select.cpp` → `tests/crud/test_select.cpp`.
- `internals/performance/DISTINCT_ANALYSIS.md:94` `tests/test_distinct.cpp` → `tests/query/test_distinct.cpp`.
- `guide/features/JOIN_OPERATIONS.md:543` `tests/test_fk_fields.cpp` → `tests/schema/test_fk_fields.cpp`.
- `guide/features/BATCH_OPERATIONS.md:421,422` `tests/test_crud.cpp`/`tests/test_fk_fields.cpp` → `tests/crud/test_crud.cpp` / `tests/schema/test_fk_fields.cpp`.
- `internals/testing/TESTING.md:14` `Location: tests/test_*.cpp` → `tests/<category>/test_*.cpp` (subdirs: crud, query, schema, db, errors, …); `:19` `tests/test_fk_fields.cpp` → `tests/schema/test_fk_fields.cpp`; `:119` `Create test file: tests/test_<feature>.cpp` → `tests/<category>/test_<feature>.cpp`.
- **MISSED in original sweep**: `internals/testing/TESTING.md:26` `**Location**: benchmarks/bench_*.cpp` (dead glob, no such files) → `benchmarks/tests/benchmark_tests.yaml` (YAML-declared; fixtures in `benchmarks/query_benchmark.cppm` / `benchmarks/crud_benchmark.cppm`).
- `internals/building/COMMON_TASKS.md:40` `tests/test_*.cpp` → `tests/<category>/test_*.cpp`.
- `internals/building/ADDING_FEATURES.md:8` `tests/test_<feature>.cpp` → `tests/<category>/test_<feature>.cpp`; `internals/performance/PERFORMANCE.md:107` same.

## Verification
1. No stale binary names: `grep -rn 'bench_storm\b\|bench_sqlite\b\|bench_join\b\|bench_where\b\|bench_limit\b\|bench_sqlite_orm\b\|bench_<feature>\|bench_your_feature\|common\.py' docs/ | grep -v superpowers` → empty.
2. No flat test paths: `grep -rn 'tests/test_[a-z*<]' docs/ | grep -v superpowers | grep -vE 'tests/(crud|query|schema|db|errors|mock_sqlite|mock_libpq|tools|test_cases|yaml|scripts|bench_dashboard)/'` → empty.
3. Every remaining `tests/<cat>/test_X.cpp` path in docs resolves to a real file (spot-check the named ones).
4. `mkdocs build --strict` still EXIT=0 (these are prose/code-fence edits, shouldn't affect links, but confirm).
5. Part A + Part B invariants still clean (no `3-level`, nav/README intact).

## Risk notes
- Most edits are inside code fences (run-commands) or inline backticks (paths) — not markdown links, so mkdocs won't catch errors here; the grep checks (1-3) are the real gate.
- The PERFORMANCE.md "Tips/how-to-add-a-benchmark" rewrite (§B) is the only place with real prose rewriting; keep it factual against `register.cpp` + `benchmark_tests.yaml`.
