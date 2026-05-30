# #214 Cache-Architecture Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Benchmark the four statement-caching configurations (all-3 / no-L2 / no-L1 / L3-only) across four scenarios, produce a results table, and recommend #214 Option A/B/C/D — without collapsing any cache level or editing #273.

**Architecture:** Two `#ifdef` guards (`STORM_DISABLE_L1`, `STORM_DISABLE_L2`) on the cache-reuse branches in `src/orm/queryset.cppm` and `src/orm/statements/select.cppm` produce the variants at compile time (dead code when flags are off, so the default build is unchanged). A standalone bench binary `storm_cache_probe` (plain `.cpp`, runtime `RegisterBenchmark` — the `register.cpp` pattern, NOT the `BENCHMARK` macro, which crashes clang-p2996 inside any module purview) drives the four scenarios. Each config is a separate `ninja-release` build with different `-D` flags; results are collated by hand into a table.

**Tech Stack:** C++26 / clang-p2996, CMake + Ninja (`ninja-release`), Google Benchmark, SQLite (`:memory:`).

---

## File structure

- `src/orm/queryset.cppm` (modify) — `STORM_DISABLE_L1` guard in `get_select_statement()` / `get_update_statement()`.
- `src/orm/statements/select.cppm` (modify) — `STORM_DISABLE_L2` guard in `prepare_simple_path()` and `prepare_and_bind()`.
- `benchmarks/cache_probe.cpp` (create) — standalone bench binary, four scenarios, runtime registration.
- `benchmarks/CMakeLists.txt` (modify) — add the `storm_cache_probe` executable.
- `docs/superpowers/results/2026-05-30-cache-investigation-results.md` (create) — results table + Option A/B/C/D recommendation.

All changes land on `feature/214-cache-investigation`. develop is untouched (the spec commit already on develop is harmless docs).

---

### Task 1: Create the feature branch

**Files:** none (git only)

- [ ] **Step 1: Create and check out the issue-linked branch**

Run:
```bash
gh issue develop 214 --name feature/214-cache-investigation --base develop --checkout
```
Expected: branch `feature/214-cache-investigation` created, linked to issue #214, and checked out.

- [ ] **Step 2: Verify branch**

Run: `git branch --show-current`
Expected: `feature/214-cache-investigation`

---

### Task 2: Add the L2 disable guard

**Files:**
- Modify: `src/orm/statements/select.cppm` (`prepare_simple_path` ~432, `prepare_and_bind` ~463)

L2 reuse is the `cached_*_stmt_ != nullptr` short-circuit. The guard forces a fresh
`conn_->prepare_cached()` every call by treating the cached pointer as always-null.

- [ ] **Step 1: Guard `prepare_simple_path`**

In `src/orm/statements/select.cppm`, change the condition at line ~433 from:

```cpp
        [[nodiscard]] __attribute__((always_inline)) auto prepare_simple_path() -> std::expected<Statement*, Error> {
            if (cached_simple_stmt_ == nullptr) {
```
to:
```cpp
        [[nodiscard]] __attribute__((always_inline)) auto prepare_simple_path() -> std::expected<Statement*, Error> {
#ifdef STORM_DISABLE_L2
            if (true) { // L2 disabled: always re-fetch from Connection pool (#214 investigation)
#else
            if (cached_simple_stmt_ == nullptr) {
#endif
```

- [ ] **Step 2: Guard `prepare_and_bind`**

In `src/orm/statements/select.cppm`, change the condition at line ~463 from:

```cpp
            if (cached_stmt_ == nullptr || cached_sql_ != sql) {
```
to:
```cpp
#ifdef STORM_DISABLE_L2
            if (true) { // L2 disabled (#214 investigation)
#else
            if (cached_stmt_ == nullptr || cached_sql_ != sql) {
#endif
```

- [ ] **Step 3: Guard the addr fast-path so L2 can't short-circuit via `can_use_addr_fast_path`**

In `src/orm/statements/select.cppm`, `can_use_addr_fast_path` (~494) returns true only when
`cached_stmt_ != nullptr`. With L2 disabled `cached_stmt_` is still assigned in `prepare_and_bind`
(line ~468), so also neutralize the addr cache. Change the body from:

```cpp
        [[nodiscard]] __attribute__((always_inline)) auto can_use_addr_fast_path(const void* where_addr) const -> bool {
            const bool have_cache = cached_stmt_ != nullptr && cached_where_addr_ != nullptr;
            return have_cache && where_addr == cached_where_addr_;
        }
```
to:
```cpp
        [[nodiscard]] __attribute__((always_inline)) auto can_use_addr_fast_path(const void* where_addr) const -> bool {
#ifdef STORM_DISABLE_L2
            (void) where_addr;
            return false; // L2 disabled: never reuse via WHERE-address cache (#214 investigation)
#else
            const bool have_cache = cached_stmt_ != nullptr && cached_where_addr_ != nullptr;
            return have_cache && where_addr == cached_where_addr_;
#endif
        }
```

- [ ] **Step 4: Verify default (no-flag) build still compiles unchanged**

Run:
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release --target storm 2>&1 | tail -5
```
Expected: builds clean. (Guarded branches are `#else` = current code when no flag set.)

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/select.cppm
git commit -m "bench(214): STORM_DISABLE_L2 guard on statement pointer cache"
```

---

### Task 3: Add the L1 disable guard

**Files:**
- Modify: `src/orm/queryset.cppm` (`get_update_statement` ~543, `get_select_statement` ~551)

L1 reuse is the cached `unique_ptr` member. The guard forces a fresh statement object each call.
A fresh statement has a null L2 pointer, so this also yields the "no-L1 ⇒ no L2 reuse" semantics
from the spec.

- [ ] **Step 1: Guard `get_update_statement`**

In `src/orm/queryset.cppm`, change (~543):

```cpp
        auto get_update_statement() const -> orm::statements::UpdateStatement<T, ConnType>& {
            if (!update_stmt_) [[unlikely]] {
                update_stmt_ = std::make_unique<orm::statements::UpdateStatement<T, ConnType>>(conn_);
            }
            return *update_stmt_;
        }
```
to:
```cpp
        auto get_update_statement() const -> orm::statements::UpdateStatement<T, ConnType>& {
#ifdef STORM_DISABLE_L1
            update_stmt_ = std::make_unique<orm::statements::UpdateStatement<T, ConnType>>(conn_); // L1 disabled (#214)
#else
            if (!update_stmt_) [[unlikely]] {
                update_stmt_ = std::make_unique<orm::statements::UpdateStatement<T, ConnType>>(conn_);
            }
#endif
            return *update_stmt_;
        }
```

- [ ] **Step 2: Guard `get_select_statement`**

In `src/orm/queryset.cppm`, change (~551):

```cpp
        auto get_select_statement() const -> orm::statements::SelectStatement<T, ConnType>& {
            if (!select_stmt_) [[unlikely]] {
                select_stmt_ = std::make_unique<orm::statements::SelectStatement<T, ConnType>>(conn_);
            }
            return *select_stmt_;
        }
```
to:
```cpp
        auto get_select_statement() const -> orm::statements::SelectStatement<T, ConnType>& {
#ifdef STORM_DISABLE_L1
            select_stmt_ = std::make_unique<orm::statements::SelectStatement<T, ConnType>>(conn_); // L1 disabled (#214)
#else
            if (!select_stmt_) [[unlikely]] {
                select_stmt_ = std::make_unique<orm::statements::SelectStatement<T, ConnType>>(conn_);
            }
#endif
            return *select_stmt_;
        }
```

- [ ] **Step 3: Verify default build still compiles unchanged**

Run:
```bash
cmake --build --preset ninja-release --target storm 2>&1 | tail -5
```
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/orm/queryset.cppm
git commit -m "bench(214): STORM_DISABLE_L1 guard on QuerySet statement cache"
```

---

### Task 4: Create the standalone bench binary

**Files:**
- Create: `benchmarks/cache_probe.cpp`

Plain `.cpp` (NOT `.cppm`) — the `BENCHMARK` macro segfaults clang-p2996 in a module purview
(see project memory). Use Google Benchmark's runtime `RegisterBenchmark` so no macro static-init
lands in any module. The TU `import storm`s and seeds a `:memory:` SQLite DB. Person model comes
from `benchmarks/models.hpp`.

Four scenarios, each as a registered lambda:
- `Reuse` — one `QuerySet<Person>`, repeated `where(...).select()`.
- `NewPerOp` — fresh `QuerySet<Person>` each iteration.
- `MixedWhere` — one QuerySet, rotating WHERE value so SQL string changes.
- `BulkUpdate` — batch UPDATE of a seeded dataset.

- [ ] **Step 1: Write the bench file**

Create `benchmarks/cache_probe.cpp`:

```cpp
// storm_cache_probe — #214 cache-architecture investigation (NOT a CI bench).
//
// Standalone binary; built four times with different -D flags
// (STORM_DISABLE_L1 / STORM_DISABLE_L2) to compare cache configurations.
// Plain .cpp + runtime RegisterBenchmark: the BENCHMARK macro crashes
// clang-p2996 inside a module purview (see project memory).

#include <benchmark/benchmark.h>

#include <tuple>

#include "models.hpp" // Person, reflection annotations textually visible (#262)

#include <meta>

import std;
import storm;

namespace {

    using storm::orm::QuerySet;
    using storm::benchmark::Person;

    constexpr int kSeedRows = 1'000;

    auto seed(int n) -> void {
        QuerySet<Person>::set_default_connection(":memory:");
        QuerySet<Person> qs;
        qs.create_table();
        for (int i = 0; i < n; ++i) {
            Person p{.id = i, .name = std::format("Person{}", i), .age = 20 + (i % 50), .salary = 1000.0 + i};
            qs.insert(p);
        }
    }

    // Reuse: one QuerySet instance, repeated identical query — max L1 benefit.
    auto bench_reuse(benchmark::State& state) -> void {
        seed(kSeedRows);
        QuerySet<Person> qs;
        for (auto _ : state) {
            auto r = qs.where(storm::orm::field<^^Person::age>() > 30).select();
            benchmark::DoNotOptimize(r);
        }
    }

    // NewPerOp: fresh QuerySet each iteration — zero L1 benefit by construction.
    auto bench_new_per_op(benchmark::State& state) -> void {
        seed(kSeedRows);
        for (auto _ : state) {
            QuerySet<Person> qs;
            auto             r = qs.where(storm::orm::field<^^Person::age>() > 30).select();
            benchmark::DoNotOptimize(r);
        }
    }

    // MixedWhere: rotating WHERE value so SQL string changes — exercises L2 miss/L3 hit.
    auto bench_mixed_where(benchmark::State& state) -> void {
        seed(kSeedRows);
        QuerySet<Person> qs;
        int              i = 0;
        for (auto _ : state) {
            const int threshold = 20 + (i++ % 50);
            auto      r         = qs.where(storm::orm::field<^^Person::age>() > threshold).select();
            benchmark::DoNotOptimize(r);
        }
    }

    // BulkUpdate: batch UPDATE over the seeded dataset.
    auto bench_bulk_update(benchmark::State& state) -> void {
        seed(kSeedRows);
        QuerySet<Person> qs;
        for (auto _ : state) {
            auto r = qs.where(storm::orm::field<^^Person::age>() > 30)
                             .update(storm::orm::field<^^Person::salary>() = 2000.0);
            benchmark::DoNotOptimize(r);
        }
    }

} // namespace

auto main(int argc, char** argv) -> int {
    benchmark::RegisterBenchmark("CacheProbe/Reuse", bench_reuse);
    benchmark::RegisterBenchmark("CacheProbe/NewPerOp", bench_new_per_op);
    benchmark::RegisterBenchmark("CacheProbe/MixedWhere", bench_mixed_where);
    benchmark::RegisterBenchmark("CacheProbe/BulkUpdate", bench_bulk_update);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
```

> **NOTE for the implementing engineer:** the QuerySet API calls above (`create_table`, `insert`,
> `where(...).select()`, `where(...).update(field = value)`, `field<^^...>()`) are best-effort from
> the spec. **Before relying on them, grep `src/orm/queryset.cppm` and `benchmarks/query_benchmark.cppm`
> for the exact method names and the correct `field<>` / update-assignment spelling**, and adjust the
> lambda bodies to match. The four scenario *shapes* (reuse / fresh / rotating-WHERE / bulk) are the
> contract; the exact API spelling is whatever the codebase actually exposes.

- [ ] **Step 2: Verify the file references real API**

Run:
```bash
grep -n "create_table\|auto insert\|auto update\|auto select\|field<" src/orm/queryset.cppm | head -20
```
Expected: confirm method names; fix `cache_probe.cpp` to match before building.

---

### Task 5: Wire the bench binary into CMake

**Files:**
- Modify: `benchmarks/CMakeLists.txt`

Mirror how `storm_anchors` (the `anchors_raw.cpp` binary) is declared — a plain executable that
links `storm` + benchmark, NOT part of the module-fileset bench target.

- [ ] **Step 1: Find the storm_anchors target declaration**

Run: `grep -n "storm_anchors\|anchors_raw\|add_executable" benchmarks/CMakeLists.txt`
Expected: shows the `add_executable(storm_anchors ...)` block to copy.

- [ ] **Step 2: Add the storm_cache_probe executable**

After the `storm_anchors` block in `benchmarks/CMakeLists.txt`, add an analogous block. Use the
exact link/include calls the anchors target uses (copy them verbatim, swapping the name/source).
Template (adjust to match the real anchors block):

```cmake
# storm_cache_probe — #214 cache-architecture investigation (not in CI).
# Built four times with different -D flags to compare cache configs.
add_executable(storm_cache_probe cache_probe.cpp)
target_link_libraries(storm_cache_probe PRIVATE storm benchmark::benchmark)
link_sqlite(storm_cache_probe)
```

> **NOTE:** `storm_cache_probe` imports `storm`, so it needs whatever module / `import std;`
> wiring the **module-based** bench target uses (e.g. `storm_benchmark_query`), NOT the
> module-free `storm_anchors` wiring. Copy the link + module-std settings from the target that
> already does `import storm` successfully. If you hit a PCM-cache hash divergence at first
> `import storm`, build with `-j1` once (see project memory), then resume parallel.

- [ ] **Step 3: Configure and build the default (all-3) binary**

Run:
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release --target storm_cache_probe 2>&1 | tail -20
```
Expected: `storm_cache_probe` builds. If first `import storm` fails with "malformed or corrupted
precompiled file", rebuild that target with `-j1`.

- [ ] **Step 4: Smoke-run**

Run:
```bash
./build/release/benchmarks/storm_cache_probe --benchmark_min_time=0.1s 2>&1 | tail -15
```
Expected: four `CacheProbe/*` rows print with timings.

- [ ] **Step 5: Commit**

```bash
git add benchmarks/cache_probe.cpp benchmarks/CMakeLists.txt
git commit -m "bench(214): standalone storm_cache_probe binary, four cache scenarios"
```

---

### Task 6: Run all four configurations

**Files:** none (capture JSON to `/tmp`)

Each config is a clean Release build with the right flags via `CMAKE_CXX_FLAGS`, then a 10-rep run.
Use a fresh build dir per config so flags don't leak through the PCM cache (per-target flag drift
corrupts module hashes — project memory).

- [ ] **Step 1: all-3 (baseline)**

```bash
cmake --preset ninja-release -B build/cache-all3 && \
  cmake --build build/cache-all3 --target storm_cache_probe -j1 && \
  cmake --build build/cache-all3 --target storm_cache_probe && \
  ./build/cache-all3/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_all3.json
```
Expected: JSON written, median rows for all four scenarios.

- [ ] **Step 2: no-L2**

```bash
cmake --preset ninja-release -B build/cache-noL2 -DCMAKE_CXX_FLAGS="-DSTORM_DISABLE_L2" && \
  cmake --build build/cache-noL2 --target storm_cache_probe -j1 && \
  cmake --build build/cache-noL2 --target storm_cache_probe && \
  ./build/cache-noL2/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_noL2.json
```

- [ ] **Step 3: no-L1**

```bash
cmake --preset ninja-release -B build/cache-noL1 -DCMAKE_CXX_FLAGS="-DSTORM_DISABLE_L1" && \
  cmake --build build/cache-noL1 --target storm_cache_probe -j1 && \
  cmake --build build/cache-noL1 --target storm_cache_probe && \
  ./build/cache-noL1/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_noL1.json
```

- [ ] **Step 4: L3-only**

```bash
cmake --preset ninja-release -B build/cache-l3only -DCMAKE_CXX_FLAGS="-DSTORM_DISABLE_L1 -DSTORM_DISABLE_L2" && \
  cmake --build build/cache-l3only --target storm_cache_probe -j1 && \
  cmake --build build/cache-l3only --target storm_cache_probe && \
  ./build/cache-l3only/benchmarks/storm_cache_probe \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/cache_l3only.json
```

- [ ] **Step 5: Sanity-check all four JSON files exist with median rows**

Run:
```bash
for f in all3 noL2 noL1 l3only; do echo "== $f =="; \
  grep -o '"name": "CacheProbe/[^"]*_median"' /tmp/cache_$f.json; done
```
Expected: each file lists four `*_median` rows.

---

### Task 7: Collate results and recommend

**Files:**
- Create: `docs/superpowers/results/2026-05-30-cache-investigation-results.md`

- [ ] **Step 1: Extract median real_time per scenario per config**

Run (repeat per file, or script it):
```bash
for f in all3 noL2 noL1 l3only; do echo "== $f =="; \
  python3 -c "import json,sys; d=json.load(open('/tmp/cache_$f.json')); \
  [print(b['name'], round(b['real_time'],1), b['time_unit']) \
   for b in d['benchmarks'] if b['name'].endswith('_median')]"; done
```
Expected: 4 scenarios × 4 configs of median real_time.

- [ ] **Step 2: Write the results doc**

Create `docs/superpowers/results/2026-05-30-cache-investigation-results.md` with:
  - A table: rows = 4 scenarios, columns = all-3 / no-L2 / no-L1 / L3-only (median real_time + % delta vs all-3).
  - Per-scenario interpretation (especially: does L3-only stay within 5% of all-3? does NewPerOp differ from Reuse, proving/disproving L1's value?).
  - The #214 decision mapped from the deltas:
    - L3-only within 5% everywhere → **Option C** (collapse).
    - L2 <2% but L1 matters → **Option B**.
    - L1/L2 >5% anywhere → **Option A** (keep; document where).
  - A note on what this means for #273 (rewrite vs keep), but DO NOT edit #273 in this phase.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/results/2026-05-30-cache-investigation-results.md
git commit -m "docs(214): cache-architecture benchmark results + Option recommendation"
```

- [ ] **Step 4: Report to the user**

Present the table and the recommended Option in chat. Stop here — editing #273 and implementing
any collapse are explicitly out of scope for this phase (per the spec). Ask the user how to proceed.

---

## Notes for the implementer

- **Default build must stay byte-for-byte behavioral-identical** when no `-D` flag is set — every
  guard's `#else` branch is the unmodified original code. Task 2/3 step "verify default build" guards this.
- **No sanitizer runs** — bench-only, Release-only, default build unchanged.
- **Cleanup:** the `build/cache-*` dirs are throwaway; remove them after Task 7 if disk matters. The
  `#ifdef` scaffolding stays committed on the feature branch as the investigation artifact (never merged).
- **PCM hazards** (from project memory): first `import storm` in a new bench target can race scan-deps
  for the std PCM → build that target `-j1` once, then resume. Per-target flag differences shift the
  module-cache hash, which is *why* each config uses a separate build dir.
