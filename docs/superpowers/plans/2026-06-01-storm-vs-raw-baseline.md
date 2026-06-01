# Storm-vs-raw SQLite baseline lane — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the benchmark dashboard show Storm's efficiency relative to raw SQLite live (e.g. `95.1% of raw`) by streaming a raw-SQLite baseline run from `storm_anchors` and adding a `--baseline raw:<id>|raw:last` selector.

**Architecture:** Extend the existing `storm_anchors` binary to (a) stream over the same AF_UNIX wire as `storm_bench`, using the *exact* gbench test names of a small Storm subset, and (b) mark its `run_start` with a new `is_raw` flag. The dashboard persists `is_raw` (new Atlas migration), resolves a new `raw:` baseline selector, and — when the active baseline is raw — renders an efficiency label (`raw_ns / current_ns * 100`) instead of the signed REGRESS/IMPROVE delta.

**Tech Stack:** C++26 (clang-p2996), Google Benchmark, AF_UNIX SOCK_DGRAM NDJSON wire (`wire.hpp`/`wire.cppm`), Storm ORM for the dashboard SQLite store, Atlas migrations, GoogleTest, CMake/Ninja presets.

---

## Pinned subset (pitfall #3 — exact match key is `(test_name, dataset_size)`)

The raw lane emits these EXACT gbench names (including the `/N` size suffix that `main.cpp` appends via `ArgName("N")`). A mismatch silently yields `— (no raw)` with no error, so these strings are load-bearing.

| Raw benchmark | gbench `test_name` | category | dataset_size | Storm source |
|---|---|---|---|---|
| WHERE int > | `Storm/WHERE/where_int_comparison_gt/10000` | WHERE | 10000 | `benchmark_tests.yaml` fixed 10000 |
| WHERE bool == | `Storm/WHERE/where_bool_equality/10000` | WHERE | 10000 | `benchmark_tests.yaml` fixed 10000 |
| SELECT all | `Storm/SELECT/select/10000` | SELECT | 10000 | `dataset_standard` includes 10000 |
| INSERT single | `Storm/INSERT/insert/1` | INSERT | 1 | `batch_standard` includes 1 |

Notes:
- `dataset_size` in the wire message = the integer `N` (10000, 10000, 10000, 1). The dashboard match key is `(test_name, dataset_size)` and `test_name` already embeds `/N`, so both halves must agree.
- WHERE benchmarks filter `age > 30` / `is_active == true` over a 10000-row table — the *query* runs over the seeded table; the measured work is the SELECT, not the seed.
- This is intentionally partial. Every other Storm row (all other sizes, all other categories) will correctly show `— (no raw)`.

## Architecture decisions baked into this plan

1. **`is_raw` is a `run_start` field**, mirroring `is_full_run`. It flows: `storm_anchors` → `build_run_start(filter, is_full_run, is_raw)` → wire → `handle_run_start` → `BenchRun.is_raw` column → baseline resolution.
2. **`efficiency_pct` is a NEW `ResultMsg` field** computed at render/enrich time from the two `ns` values. It is NOT derived from `delta_pct` (pitfall #6). `delta_pct` stays exactly as-is for the non-raw (regression) path.
3. **`DashboardState.baseline_is_raw`** tells the enrich/render path which label to show. It is set true only when the resolved baseline run has `is_raw = 1` (for `raw:` selectors).
4. **Raw fixture loop boundary** (pitfall #4): open db, create table, seed N rows, prepare stmt all OUTSIDE `for (auto _ : state)`; only the measured query inside. This mirrors the existing anchors in `anchors_raw.cpp` and the Storm fixtures.
5. **`is_raw` lands byte-identically in `wire.hpp` AND `wire.cppm`** (pitfall #1). Every task that touches one touches both in the same commit.

---

## Task 1: Add `is_raw` to the wire format (both files, byte-identical)

**Files:**
- Modify: `benchmarks/dashboard/wire.hpp` (ResultMsg struct ~line 43-73; `build_run_start` ~line 117; parser ~line 348-353)
- Modify: `benchmarks/dashboard/wire.cppm` (mirror: ResultMsg struct; `build_run_start` ~line 101; parser ~line 317-320)

This is pure wire plumbing with no behavioural change yet — `is_raw` defaults to `false` so existing producers/consumers are unaffected (pitfall #5, backward-compatible).

- [ ] **Step 1: Add `is_raw` field to `ResultMsg` in `wire.hpp`**

In `wire.hpp`, inside `struct ResultMsg`, directly after the `is_full_run` line (currently line 47), add:

```cpp
        bool        is_full_run{false};
        bool        is_raw{false}; // run_start only: true => raw-SQLite baseline run
```

- [ ] **Step 2: Add the same `is_raw` field to `ResultMsg` in `wire.cppm`**

In `wire.cppm`, find the `is_full_run{false};` line inside `struct ResultMsg` (line 39) and add the identical line right after it:

```cpp
        bool        is_full_run{false};
        bool        is_raw{false}; // run_start only: true => raw-SQLite baseline run
```

- [ ] **Step 3: Extend `build_run_start` signature + body in `wire.hpp`**

Replace the `build_run_start` function in `wire.hpp` (currently lines 117-126) with:

```cpp
    inline auto build_run_start(std::string_view filter, bool is_full_run, bool is_raw) -> std::string {
        std::string s;
        s.reserve(80 + filter.size());
        s.append(R"({"type":"run_start","filter":")");
        append_escaped(s, filter);
        s.append(R"(","is_full_run":)");
        s.append(is_full_run ? "true" : "false");
        s.append(R"(,"is_raw":)");
        s.append(is_raw ? "true" : "false");
        s.push_back('}');
        return s;
    }
```

- [ ] **Step 4: Mirror `build_run_start` in `wire.cppm`**

Replace the corresponding `build_run_start` in `wire.cppm` (lines 101-110, the `{"type":"run_start",...}` builder) with the byte-identical body from Step 3.

- [ ] **Step 5: Parse `is_raw` in the run_start branch of `wire.hpp`**

In `wire.hpp`'s `parse()`, the `run_start` branch (lines 348-353) currently reads only `filter` and `is_full_run`. Add the `is_raw` read:

```cpp
            if (type_v == "run_start") {
                m.kind = MessageKind::RunStart;
                rdr.read_field("filter", m.filter);
                rdr.read_field("is_full_run", m.is_full_run);
                rdr.read_field("is_raw", m.is_raw);
                return m;
            }
```

- [ ] **Step 6: Mirror the parser change in `wire.cppm`**

In `wire.cppm`'s `parse()`, the `run_start` branch (line 317-320) reads `filter` and `is_full_run`. Add the identical `rdr.read_field("is_raw", m.is_raw);` line after the `is_full_run` read.

- [ ] **Step 7: Fix the one existing `build_run_start` caller in `reporter.cpp`**

`build_run_start` now takes 3 args. In `benchmarks/dashboard/reporter.cpp`, `ReportContext` (line 164) currently calls:

```cpp
                    send_line(wire::build_run_start(filter_, filter_.empty()));
```

Replace with (storm_bench is never raw):

```cpp
                    send_line(wire::build_run_start(filter_, filter_.empty(), /*is_raw=*/false));
```

- [ ] **Step 8: Build the dashboard + bench to verify the wire change compiles**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release --target storm_bench_dashboard storm_bench storm_anchors`
Expected: clean build (no behavioural change yet).

- [ ] **Step 9: Commit**

```bash
git add benchmarks/dashboard/wire.hpp benchmarks/dashboard/wire.cppm benchmarks/dashboard/reporter.cpp
git commit -m "feat(bench-wire): add is_raw flag to run_start (wire.hpp + wire.cppm)"
```

---

## Task 2: Persist `is_raw` on `BenchRun` (model + Atlas migration)

**Files:**
- Modify: `benchmarks/dashboard/models.hpp` (`struct BenchRun` ~line 17-26)
- Modify: `benchmarks/dashboard/db.hpp` (`insert_run` ~line 199-220)
- Modify: `benchmarks/dashboard/events.hpp` (`handle_run_start` ~line 53-69; `open_synthetic_session` ~line 72-87; `build_session_from_run` ~line 150-159)
- Create: `benchmarks/dashboard/migrations/<new-timestamp>.sql` (generated by Atlas, do NOT hand-write — pitfall #2)
- Modify: `benchmarks/dashboard/migrations/atlas.sum` (regenerated by Atlas)

> The dashboard never auto-creates schema. `insert_run` must NOT write a column the DB doesn't have, so the migration (Step 5-6) lands together with the model change.

- [ ] **Step 1: Add `is_raw` column to the `BenchRun` model**

In `benchmarks/dashboard/models.hpp`, inside `struct BenchRun`, after the `is_full_run` field (line 25), add:

```cpp
        bool                                      is_full_run{false}; // tagged so --baseline auto can skip partial runs
        bool                                      is_raw{false};      // true => raw-SQLite baseline run (storm_anchors)
```

- [ ] **Step 2: Thread `is_raw` through `insert_run`**

In `benchmarks/dashboard/db.hpp`, change `insert_run`'s signature and body (lines 199-220) to accept and persist `is_raw`:

```cpp
        [[nodiscard]] auto insert_run(std::string_view filter, bool is_full_run, bool is_raw)
                -> std::expected<std::int64_t, std::string> {
            ensure_host_context();

            // Re-read git on every run — the working tree may have moved since
            // the daemon started or since the previous run (Issue #267).
            const GitContext git = git_capture_();

            bench_dashboard::BenchRun row{};
            row.git_hash    = git.git_hash;
            row.branch      = git.branch;
            row.timestamp   = current_iso8601();
            row.hostname    = host_.hostname;
            row.compiler    = host_.compiler;
            row.filter      = std::string{filter};
            row.is_full_run = is_full_run;
            row.is_raw      = is_raw;

            auto rc = storm::QuerySet<bench_dashboard::BenchRun>().insert(row).execute();
            if (!rc)
                return std::unexpected(std::string{rc.error().message()});
            return *rc;
        }
```

- [ ] **Step 3: Pass `is_raw` from the two `insert_run` call sites in `events.hpp`**

In `benchmarks/dashboard/events.hpp`:

`handle_run_start` (line 59) — pass the flag from the message:

```cpp
        auto rc = db.insert_run(msg.filter, msg.is_full_run, msg.is_raw);
```

`open_synthetic_session` (line 78) — a Result with no preceding RunStart is always a non-raw full run:

```cpp
        auto rc = db.insert_run(msg.filter, /*is_full_run=*/true, /*is_raw=*/false);
```

- [ ] **Step 4: Carry `is_raw` into the rebuilt `Session` (needed later for the baseline label)**

In `benchmarks/dashboard/events.hpp`, `build_session_from_run` (lines 150-159) — this is where DB rows become Sessions on `r`/startup. Storing it on the Session lets Task 5's label logic work after a refresh. Add after `sess.is_full_run = run.is_full_run;` (line 154):

```cpp
        sess.is_full_run = run.is_full_run;
        sess.is_raw      = run.is_raw;
```

(The `Session::is_raw` field is added in Task 5 Step 1. If executing strictly in order, this line will not compile until Task 5 Step 1 lands — see the note in Task 5. Subagent executors: add `Session::is_raw` first if you hit a compile error here, then return.)

- [ ] **Step 5: Generate the Atlas migration for the new column**

The schema is Atlas-managed. Generate the migration through the project's makemigrations flow — do NOT hand-edit a `.sql` or `atlas.sum` (pitfall #2).

Run: `cmake --build build/release --target makemigrations`
(If the target name differs, inspect `cmake/` / `benchmarks/dashboard/CMakeLists.txt` for the Atlas `migrate diff` / makemigrations target. See `docs/development/MIGRATIONS.md`.)

Expected: a new `benchmarks/dashboard/migrations/<timestamp>.sql` containing:

```sql
-- Add column "is_raw" to table: "BenchRun"
ALTER TABLE `BenchRun` ADD COLUMN `is_raw` integer NOT NULL DEFAULT 0;
```

and `atlas.sum` updated with the new file's hash + new `h1:` top hash.

- [ ] **Step 6: Verify the migration applies to a scratch DB**

Run:
```bash
atlas migrate apply \
  --dir 'file://benchmarks/dashboard/migrations' \
  --url 'sqlite:///tmp/storm-bench-migtest.db'
```
Expected: all migrations apply cleanly, including the new `is_raw` one. Then `rm -f /tmp/storm-bench-migtest.db`.

- [ ] **Step 7: Build to confirm model + db changes compile**

Run: `cmake --build --preset ninja-release --target storm_bench_dashboard`
Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add benchmarks/dashboard/models.hpp benchmarks/dashboard/db.hpp benchmarks/dashboard/events.hpp benchmarks/dashboard/migrations/
git commit -m "feat(bench-dashboard): persist is_raw on BenchRun via Atlas migration"
```

---

## Task 3: Stream the raw subset from `storm_anchors`

**Files:**
- Modify: `benchmarks/anchors_raw.cpp` (add streaming + the 4 subset benchmarks with exact Storm names)
- Modify: `benchmarks/CMakeLists.txt` (link `storm_anchors` against the dashboard reporter so it can stream — verify whether it already links it)

> `storm_anchors` stays a plain `.cpp` — module purview segfaults the BMI writer (documented constraint, see file header). It must NOT `import storm`.
>
> The reporter (`install_storm_reporter`) lives in `reporter.cpp` and sends `run_start`/`Result`/`run_complete`. It currently hard-codes `is_raw=false` (Task 1 Step 7). For the raw lane we need a way to send `is_raw=true`. The simplest surgical approach: the reporter reads an env signal. `storm_anchors` sets it before installing the reporter.

- [ ] **Step 1: Make the reporter emit `is_raw=true` when `STORM_BENCH_RAW` is set**

In `benchmarks/dashboard/reporter.cpp`, `ReportContext` (lines 162-168). Read the env once. Replace:

```cpp
            auto ReportContext(Context const& /*ctx*/) -> bool override {
                if (!sent_start_) {
                    send_line(wire::build_run_start(filter_, filter_.empty(), /*is_raw=*/false));
                    sent_start_ = true;
                }
                return true;
            }
```

with:

```cpp
            auto ReportContext(Context const& /*ctx*/) -> bool override {
                if (!sent_start_) {
                    const bool is_raw = std::getenv("STORM_BENCH_RAW") != nullptr;
                    send_line(wire::build_run_start(filter_, filter_.empty(), is_raw));
                    sent_start_ = true;
                }
                return true;
            }
```

`<cstdlib>` is already included in `reporter.cpp` (line 34).

- [ ] **Step 2: Add the streaming-reporter wiring to `storm_anchors`**

In `benchmarks/anchors_raw.cpp`, the file currently ends with `BENCHMARK_MAIN();` (line 220). Replace the includes block and the `BENCHMARK_MAIN()` with an explicit `main()` that installs the dashboard reporter when `STORM_BENCH_SOCKET` is set, mirroring `benchmarks/main.cpp` lines 99-119.

First, add the reporter header to the includes (after `#include <sqlite3.h>`, line 26):

```cpp
#include <benchmark/benchmark.h>
#include <sqlite3.h>

#include "dashboard/reporter.h"
```

Then replace the final `BENCHMARK_MAIN();` line (220) with:

```cpp
auto main(int argc, char** argv) -> int { // NOLINT(bugprone-exception-escape)
    // Stream to the dashboard when STORM_BENCH_SOCKET is set, marking this run
    // raw (is_raw=true) so the dashboard can treat it as a Storm-vs-raw
    // baseline. STORM_BENCH_RAW is read by install_storm_reporter's
    // ReportContext. Unset STORM_BENCH_SOCKET → default text reporter, no
    // network calls (release-time spot check).
    ::benchmark::BenchmarkReporter* dashboard_reporter = nullptr;
    if (std::getenv("STORM_BENCH_SOCKET") != nullptr) { // NOLINT(concurrency-mt-unsafe)
        ::setenv("STORM_BENCH_RAW", "1", /*overwrite=*/1); // NOLINT(concurrency-mt-unsafe)
        dashboard_reporter = bench_dashboard::install_storm_reporter(/*socket_path=*/"", /*filter=*/"");
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    if (dashboard_reporter != nullptr) {
        benchmark::RunSpecifiedBenchmarks(dashboard_reporter);
    } else {
        benchmark::RunSpecifiedBenchmarks();
    }
    benchmark::Shutdown();
    return 0;
}
```

Add `#include <cstdlib>` is already present (line 29). `setenv` needs `<cstdlib>` (POSIX) — already included.

- [ ] **Step 3: Replace the 4 anchor benchmarks with the 4 subset benchmarks using EXACT Storm names**

The existing 4 anchors (`BM_Anchor_Raw_*`) have names that match no Storm test. Replace each `BENCHMARK(...)` registration so the gbench-reported name equals the Storm subset name (including `/N`). gbench appends `/N` automatically when you call `->Arg(N)`. Use `->Name("Storm/CAT/test")->Arg(N)` so the final reported name is `Storm/CAT/test/N`.

Rework the four benchmark functions + registrations as follows. Keep the existing helpers (`open_memory_db`, `exec`, `prepare`, `seed_person`, `kCreatePerson`). The seed size constants change to match the subset.

Replace the four `BM_Anchor_Raw_*` functions and their `BENCHMARK(...)` lines (lines 99-216) with:

```cpp
    constexpr int kWhereSeedRows = 10'000;
    constexpr int kSelectRows    = 10'000;

    // ========================================================================
    // Storm/WHERE/where_int_comparison_gt/10000 — SELECT … WHERE age > 30
    // ========================================================================
    auto BM_Raw_Where_IntGt(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        seed_person(db, kWhereSeedRows);
        sqlite3_stmt* sel = prepare(db, "SELECT id, name, age, salary FROM person WHERE age > 30");

        for (auto _ : state) {
            int rows = 0;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 0));
                benchmark::DoNotOptimize(sqlite3_column_text(sel, 1));
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 2));
                benchmark::DoNotOptimize(sqlite3_column_double(sel, 3));
                ++rows;
            }
            sqlite3_reset(sel);
        }
        state.SetItemsProcessed(state.iterations());

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Raw_Where_IntGt)->Name("Storm/WHERE/where_int_comparison_gt")->Arg(kWhereSeedRows);

    // ========================================================================
    // Storm/WHERE/where_bool_equality/10000 — SELECT … WHERE is_active = 1
    // ========================================================================
    auto BM_Raw_Where_BoolEq(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePersonBool);
        seed_person_bool(db, kWhereSeedRows);
        sqlite3_stmt* sel = prepare(db, "SELECT id, name, age, salary FROM person WHERE is_active = 1");

        for (auto _ : state) {
            int rows = 0;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 0));
                benchmark::DoNotOptimize(sqlite3_column_text(sel, 1));
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 2));
                benchmark::DoNotOptimize(sqlite3_column_double(sel, 3));
                ++rows;
            }
            sqlite3_reset(sel);
        }
        state.SetItemsProcessed(state.iterations());

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Raw_Where_BoolEq)->Name("Storm/WHERE/where_bool_equality")->Arg(kWhereSeedRows);

    // ========================================================================
    // Storm/SELECT/select/10000 — sequential SELECT over 10K rows
    // ========================================================================
    auto BM_Raw_Select_All(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        seed_person(db, kSelectRows);
        sqlite3_stmt* sel = prepare(db, "SELECT id, name, age, salary FROM person");

        for (auto _ : state) {
            int rows = 0;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 0));
                benchmark::DoNotOptimize(sqlite3_column_text(sel, 1));
                benchmark::DoNotOptimize(sqlite3_column_int(sel, 2));
                benchmark::DoNotOptimize(sqlite3_column_double(sel, 3));
                ++rows;
            }
            if (rows != kSelectRows) {
                die(db, "select row count mismatch");
            }
            sqlite3_reset(sel);
        }
        state.SetItemsProcessed(state.iterations() * kSelectRows);

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Raw_Select_All)->Name("Storm/SELECT/select")->Arg(kSelectRows);

    // ========================================================================
    // Storm/INSERT/insert/1 — single-row INSERT throughput
    // ========================================================================
    auto BM_Raw_Insert_Single(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        sqlite3_stmt* ins = prepare(db, "INSERT INTO person(name, age, salary) VALUES(?,?,?)");

        int counter = 0;
        for (auto _ : state) {
            const std::string name = std::format("P{}", counter++);
            sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, 30);
            sqlite3_bind_double(ins, 3, 50'000.0);
            if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "insert step");
            }
            sqlite3_reset(ins);
        }
        state.SetItemsProcessed(state.iterations());

        sqlite3_finalize(ins);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Raw_Insert_Single)->Name("Storm/INSERT/insert")->Arg(1);
```

- [ ] **Step 4: Add the bool-schema helpers needed by `BM_Raw_Where_BoolEq`**

The `where_bool_equality` query needs an `is_active` column. Add a second schema + seeder near the existing `kCreatePerson` / `seed_person` (after `seed_person`, ~line 97). The Storm `Person` model has `is_active` (bool) — match it:

```cpp
    constexpr auto kCreatePersonBool = "CREATE TABLE person ("
                                       "id INTEGER PRIMARY KEY,"
                                       "name TEXT NOT NULL,"
                                       "age INTEGER NOT NULL,"
                                       "salary REAL NOT NULL,"
                                       "is_active INTEGER NOT NULL"
                                       ")";

    auto seed_person_bool(sqlite3* db, int rows) -> void {
        exec(db, "BEGIN");
        sqlite3_stmt* ins = prepare(db, "INSERT INTO person(name, age, salary, is_active) VALUES(?,?,?,?)");
        for (int i = 0; i < rows; ++i) {
            const std::string name = std::format("Person{}", i);
            sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, 20 + (i % 50));
            sqlite3_bind_double(ins, 3, 30'000.0 + static_cast<double>(i));
            sqlite3_bind_int(ins, 4, i % 2);
            if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "seed bool step");
            }
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
        exec(db, "COMMIT");
    }
```

Also remove the now-unused old constants `kSelect1KRows`, `kBatchSize1000`, `kFullScan10K` (lines 42-44) since their benchmarks are gone — they would become orphaned (CLAUDE.md surgical-changes: clean up YOUR orphans).

- [ ] **Step 5: Update the file header comment to describe the new purpose**

The header (lines 2-23) still describes 4 sparse anchors. Update the docstring to state it now mirrors a pinned Storm subset by exact gbench name and streams to the dashboard as a raw baseline. Keep the `.cpp`-not-`.cppm` rationale paragraph verbatim. Replace the "Four anchors:" list (lines 12-16) with:

```cpp
 * Raw subset (exact Storm gbench names, so the dashboard's (test_name,
 * dataset_size) matcher slots them against the Storm rows they mirror):
 *   - Storm/WHERE/where_int_comparison_gt/10000
 *   - Storm/WHERE/where_bool_equality/10000
 *   - Storm/SELECT/select/10000
 *   - Storm/INSERT/insert/1
 *
 * When STORM_BENCH_SOCKET is set, streams over the dashboard wire with
 * is_raw=true (run_start), producing the Storm-vs-raw baseline run.
```

- [ ] **Step 6: Confirm `storm_anchors` links the reporter in CMake**

Inspect `benchmarks/CMakeLists.txt` for the `storm_anchors` target. It must link whatever target provides `bench_dashboard::install_storm_reporter` (the same one `storm_bench` uses for `reporter.cpp`) and have `benchmarks/` on its include path so `#include "dashboard/reporter.h"` resolves. If `storm_bench` links a `storm_bench_reporter`-style object/library, add it to `storm_anchors` too.

Run: `grep -n "storm_anchors\|reporter\|install_storm_reporter" benchmarks/CMakeLists.txt`
Then mirror the reporter linkage from the `storm_bench` target onto `storm_anchors`.

- [ ] **Step 7: Build `storm_anchors`**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release --target storm_anchors`
Expected: clean build.

- [ ] **Step 8: Smoke-run `storm_anchors` standalone (no dashboard)**

Run: `./build/release/benchmarks/storm_anchors`
Expected: 4 benchmarks run and report under names `Storm/WHERE/where_int_comparison_gt/10000`, `Storm/WHERE/where_bool_equality/10000`, `Storm/SELECT/select/10000`, `Storm/INSERT/insert/1`. No streaming (STORM_BENCH_SOCKET unset).

- [ ] **Step 9: Commit**

```bash
git add benchmarks/anchors_raw.cpp benchmarks/CMakeLists.txt benchmarks/dashboard/reporter.cpp
git commit -m "feat(bench): stream raw subset from storm_anchors with exact Storm names"
```

---

## Task 4: Add the `raw:` baseline selector (`args.hpp`)

**Files:**
- Modify: `benchmarks/dashboard/args.hpp` (`BaselineSelector` variant ~line 67; `parse_baseline_arg` ~line 166-197; `print_help` ~line 98-102)

> `--baseline raw:<id>` and `--baseline raw:last` are *read* flags — they only select an already-stored run, never run a benchmark (spec "Baseline reuse semantics").

- [ ] **Step 1: Add the `BaselineRaw` variant**

In `benchmarks/dashboard/args.hpp`, after the `BaselineBranch` struct (line 66) add a `BaselineRaw` type and include it in the variant. `raw:last` and `raw:<id>` collapse into one struct: `id == 0` means "last".

```cpp
    struct BaselineBranch {
        std::string name{};
    };
    struct BaselineRaw {
        std::int64_t id{}; // 0 => raw:last (most recent raw run, same branch+host)
    };
    using BaselineSelector = std::variant<BaselineAuto, BaselineNone, BaselineRunId, BaselineBranch, BaselineRaw>;
```

- [ ] **Step 2: Parse `raw:last` and `raw:<id>` in `parse_baseline_arg`**

In `parse_baseline_arg` (lines 166-197), before the final fall-through error block (line 190), add a `raw:` branch:

```cpp
        if (sel.starts_with("raw:")) {
            const auto rest = sel.substr(4);
            if (rest == "last") {
                opts.baseline = BaselineRaw{0};
                return;
            }
            std::int64_t id{};
            const auto   r = std::from_chars(rest.data(), rest.data() + rest.size(), id);
            if (r.ec != std::errc{} || id <= 0) {
                std::fprintf(stderr, "storm_bench_dashboard: --baseline raw: expects 'last' or a positive integer id\n");
                std::exit(1);
            }
            opts.baseline = BaselineRaw{id};
            return;
        }
```

- [ ] **Step 3: Document the selector in `print_help`**

In `print_help` (lines 98-102), the `--baseline SELECTOR` block, add two lines after the `branch:<name>` line (line 102):

```cpp
                "                            branch:<name>  most recent full run on named branch\n"
                "                            raw:<id>    specific raw run by id (efficiency labels)\n"
                "                            raw:last    most recent raw run, same branch+host\n"
```

- [ ] **Step 4: Build to confirm args parse compiles**

Run: `cmake --build --preset ninja-release --target storm_bench_dashboard`
Expected: clean build. (The variant now has 5 alternatives; `resolve_baseline` in db.hpp still compiles because `std::holds_alternative`/`get_if` over the wider variant is fine — the `BaselineRaw` case is handled in Task 5.)

- [ ] **Step 5: Commit**

```bash
git add benchmarks/dashboard/args.hpp
git commit -m "feat(bench-dashboard): add --baseline raw:<id>|raw:last selector"
```

---

## Task 5: Resolve raw baselines + render efficiency labels

This is the behavioural heart. We add: (a) `Session::is_raw` + `DashboardState::baseline_is_raw`, (b) raw resolution in `resolve_baseline`, (c) an `efficiency_pct` field + enrichment, (d) the render label + raw-aware summary.

**Files:**
- Modify: `benchmarks/dashboard/wire.hpp` + `wire.cppm` (add `efficiency_pct` to `ResultMsg`, both files)
- Modify: `benchmarks/dashboard/tui.cppm` (`Session::is_raw`; `DashboardState::baseline_is_raw`; raw-aware counters; `add_result`)
- Modify: `benchmarks/dashboard/db.hpp` (raw resolution in `resolve_baseline`; a `baseline_is_raw` out-param or helper)
- Modify: `benchmarks/dashboard/events.hpp` (compute `efficiency_pct` when baseline is raw)
- Modify: `benchmarks/dashboard/main.cpp` (`resolve_and_log_baseline` sets `state.baseline_is_raw`)
- Modify: `benchmarks/dashboard/tui_render.hpp` (`append_result_line` efficiency label; raw summary)

- [ ] **Step 1: Add `Session::is_raw` and `DashboardState::baseline_is_raw` in `tui.cppm`**

In `benchmarks/dashboard/tui.cppm`, `struct Session` (lines 74-88) add after `is_full_run{false};` (line 77):

```cpp
        bool                        is_full_run{false};
        bool                        is_raw{false};
```

And in `struct DashboardState` (lines 90-99) add after `baseline_run_id{0};` (line 96):

```cpp
        std::int64_t         baseline_run_id{0};
        bool                 baseline_is_raw{false};
```

This unblocks Task 2 Step 4 (`sess.is_raw = run.is_raw;`).

- [ ] **Step 2: Add `efficiency_pct` to `ResultMsg` in BOTH wire files**

In `wire.hpp` `struct ResultMsg`, after the `baseline_looked_up` field (line 67) add:

```cpp
        bool                  baseline_looked_up{false};
        // Set by the dashboard when the active baseline is a raw run (Issue #74):
        // raw_ns / current_ns * 100. nullopt = not a raw baseline / no raw match.
        std::optional<double> efficiency_pct; // NOLINT(readability-redundant-member-init)
```

Add the byte-identical lines to `wire.cppm`'s `ResultMsg` after its `baseline_looked_up` field.

(`efficiency_pct` is render-only state — it is NOT serialised in `build_result`/`parse`. It is computed in the dashboard and never crosses the wire, so no builder/parser change is needed. This keeps the wire byte-equivalence invariant trivially satisfied for this field.)

- [ ] **Step 3: Resolve raw baselines in `resolve_baseline` (db.hpp)**

In `benchmarks/dashboard/db.hpp`, `resolve_baseline` returns `(run_id, label)`. We need to also report whether the resolved run is raw. Change its return to a small struct so callers learn `is_raw`.

Add a result struct above `resolve_baseline` (before line 73):

```cpp
    struct ResolvedBaseline {
        std::int64_t run_id{0};
        std::string  label;
        bool         is_raw{false};
    };
```

Then change `resolve_baseline`'s signature to return `ResolvedBaseline` and add the `BaselineRaw` case. The existing `find_run` lambda already returns `(id, label)`; extend it to also surface `is_raw` by selecting `BenchRun` rows (it already does). Update `find_run` to build a `ResolvedBaseline`:

```cpp
        auto find_run = [&](auto qs) -> ResolvedBaseline {
            auto rows = qs.template order_by<^^bench_dashboard::BenchRun::id, false>()
                                .limit(1)
                                .select()
                                .execute()
                                .transform(hive_to_vector_lambda<bench_dashboard::BenchRun>());
            if (!rows || rows->empty())
                return {};
            const auto& r   = rows->front();
            const auto  lbl = std::format("Run #{} {} {}", r.id, r.branch, r.git_hash);
            return {.run_id = r.id, .label = lbl, .is_raw = r.is_raw};
        };
```

Update the existing `return {0, ""}` / `return find_run(...)` sites to return `ResolvedBaseline{}` / the struct. The `BaselineNone` early return becomes `return {};`. Then add the raw case before the final `return {};`:

```cpp
        if (const auto* rw = std::get_if<BaselineRaw>(&sel)) {
            if (rw->id > 0) {
                auto qs = storm::QuerySet<bench_dashboard::BenchRun>()
                                  .where(storm::orm::where::field<^^bench_dashboard::BenchRun::id>() ==
                                         static_cast<int>(rw->id));
                return find_run(std::move(qs));
            }
            // raw:last — most recent raw run, same branch+host (mirrors auto).
            auto qs = storm::QuerySet<bench_dashboard::BenchRun>()
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::is_raw>() == true)
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::branch>() ==
                                     std::string{current_branch})
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::hostname>() ==
                                     std::string{current_host});
            return find_run(std::move(qs));
        }
```

Change the function's declared return type from `std::pair<std::int64_t, std::string>` to `ResolvedBaseline`, and the `BaselineNone` line `return {0, ""};` to `return {};`.

- [ ] **Step 4: Set `state.baseline_is_raw` in `resolve_and_log_baseline` (main.cpp)**

In `benchmarks/dashboard/main.cpp`, `resolve_and_log_baseline` (lines 194-210) currently destructures a pair. Update to the struct:

```cpp
        auto resolved          = resolve_baseline(opts.baseline, current_branch, current_host);
        state.baseline_run_id  = resolved.run_id;
        state.baseline_label   = std::move(resolved.label);
        state.baseline_is_raw  = resolved.is_raw;
        if (resolved.run_id == 0 && std::holds_alternative<BaselineAuto>(opts.baseline)) {
```

(Update the `bid == 0` check on line 200 to `resolved.run_id == 0` as shown.)

- [ ] **Step 5: Compute `efficiency_pct` during enrichment (events.hpp)**

In `benchmarks/dashboard/events.hpp`, `enrich_measurement` (lines 21-27) already looks up the baseline ns. When the baseline is raw, also set `efficiency_pct`. Thread the `baseline_is_raw` flag in. Change `enrich_measurement`, `enrich_with_baseline`, and their callers to pass it.

`enrich_measurement`:

```cpp
    auto enrich_measurement(bench_dashboard::wire::ResultMsg& msg, std::int64_t baseline_run_id, bool baseline_is_raw)
            -> void {
        if (baseline_run_id == 0)
            return;
        msg.baseline_looked_up = true;
        if (auto base_ns = lookup_baseline_ns(baseline_run_id, msg.test_name, msg.dataset_size); base_ns.has_value()) {
            msg.delta_pct = compute_delta(msg.real_ns, *base_ns);
            if (baseline_is_raw && msg.real_ns > 0.0)
                msg.efficiency_pct = *base_ns / msg.real_ns * 100.0; // raw_ns / current_ns * 100
        }
    }
```

`enrich_bigo` is unaffected by raw (complexity rows have no efficiency label) — leave it, but it gains the extra param for signature uniformity OR keep its 2-arg form. Simplest: give `enrich_with_baseline` the flag and only forward to `enrich_measurement`:

```cpp
    auto enrich_with_baseline(bench_dashboard::wire::ResultMsg& msg, std::int64_t baseline_run_id, bool baseline_is_raw)
            -> void {
        if (msg.row_kind == bench_dashboard::wire::kRowKindMeasurement ||
            msg.row_kind == bench_dashboard::wire::kRowKindAggregate || msg.row_kind.empty())
            enrich_measurement(msg, baseline_run_id, baseline_is_raw);
        else if (msg.row_kind == bench_dashboard::wire::kRowKindBigO)
            enrich_bigo(msg, baseline_run_id);
    }
```

Update both `enrich_with_baseline` call sites to pass `state.baseline_is_raw`:
- `handle_result` (line 99): `enrich_with_baseline(enriched, state.baseline_run_id, state.baseline_is_raw);`
- `replay_run_results_into_session` (line 177): add a `bool baseline_is_raw` param and forward it; its caller `rebuild_state_from_db` (line 196) passes `state.baseline_is_raw`.

`replay_run_results_into_session` signature becomes:

```cpp
    auto replay_run_results_into_session(
            bench_dashboard::tui::Session&            sess,
            std::vector<bench_dashboard::BenchResult> rows,
            std::int64_t                              baseline_run_id,
            bool                                      baseline_is_raw,
            double                                    regression_threshold
    ) -> void {
        for (auto& r : rows) {
            auto m = build_result_msg_from_row(r);
            enrich_with_baseline(m, baseline_run_id, baseline_is_raw);
            bench_dashboard::tui::add_result(sess, m, regression_threshold);
        }
    }
```

and the call in `rebuild_state_from_db` (line 195-197):

```cpp
            replay_run_results_into_session(
                    sess, load_results_for_run(run.id), state.baseline_run_id, state.baseline_is_raw,
                    state.regression_threshold
            );
```

- [ ] **Step 6: Render the efficiency label in `append_result_line` (tui_render.hpp)**

In `benchmarks/dashboard/tui_render.hpp`, `append_result_line` (lines 137-147). The efficiency label takes precedence over `delta_pct` when present. Replace the body:

```cpp
inline auto efficiency_label(double pct) -> std::pair<std::string_view, std::string> {
    const std::string_view colour = pct >= 95.0 ? ansi::kFgGreen : ansi::kFgRed;
    return {colour, std::format("{:.1f}% of raw", pct)};
}

inline auto append_result_line(std::string& out, wire::ResultMsg const& r, double regression_threshold) -> void {
    out += format_result_prefix(r);
    if (r.efficiency_pct.has_value()) {
        const auto [ecol, etxt] = efficiency_label(*r.efficiency_pct);
        out += std::format("  {}{}{}\n", ecol, etxt, ansi::kReset);
    } else if (r.baseline_looked_up && r.delta_pct.has_value()) {
        const auto [dcol, dtxt] = format_delta(*r.delta_pct, regression_threshold);
        out += std::format("  {}{}{}\n", dcol, dtxt, ansi::kReset);
    } else if (r.baseline_looked_up) {
        // Active baseline but no match — for a raw baseline this reads "no raw".
        out += std::format("  {}— (no raw){}\n", ansi::kFgGrey, ansi::kReset);
    } else {
        out += '\n';
    }
}
```

Note: the old code showed a bare `—` for `baseline_looked_up` with no `delta_pct`. The spec wants `— (no raw)` for unmatched Storm rows under a raw baseline. Because this branch only fires when a baseline is active-but-unmatched, `— (no raw)` is acceptable for both raw and non-raw baselines (an unmatched regression row is also "no baseline row"). If you want to preserve the exact old `—` for the non-raw case, gate on a `bool is_raw` plumbed into the render — but the spec's wording and the partial-subset reality make `— (no raw)` the intended text. Keep it simple: `— (no raw)`.

- [ ] **Step 7: Raw-aware session summary (tui.cppm + tui_render.hpp)**

The spec's summary line: `N/M matched · avg N% of raw · target ≥95%`. Add raw counters to `Session` and bump them in `add_result`.

In `tui.cppm` `struct Session` add after `severe_count{0};` (line 87):

```cpp
        std::size_t                 severe_count{0};
        std::size_t                 raw_matched{0};   // rows with an efficiency_pct
        std::size_t                 raw_total{0};      // measurement rows seen while baseline is raw
        double                      raw_eff_sum{0.0};  // sum of efficiency_pct for the average
```

In `tui.cppm` `add_result` (lines 194-205), the measurement branch (after `++sess.result_count;`, before `bump_delta_counters`) — track raw stats when efficiency is present. Replace the measurement tail (lines 199-204) with:

```cpp
        ++sess.result_count;
        if (m.efficiency_pct.has_value()) {
            ++sess.raw_total;
            ++sess.raw_matched;
            sess.raw_eff_sum += *m.efficiency_pct;
        } else if (m.baseline_looked_up && !m.delta_pct.has_value()) {
            // Active baseline, this row didn't match — count toward the "/M" total
            // only in raw mode; harmless for non-raw (no efficiency summary shown).
            ++sess.raw_total;
        }
        if (m.delta_pct.has_value()) {
            bump_delta_counters(sess, *m.delta_pct, regression_threshold);
        }
        auto& bucket = find_or_create_bucket(sess, m.category);
        bucket.results.insert(bucket.results.begin(), m);
```

In `tui_render.hpp` `append_summary_line` (lines 239-253), prepend a raw-efficiency line when raw stats exist. Add at the top of the function body (after the `total` line, before the early return):

```cpp
inline auto append_summary_line(std::string& out, Session const& sess) -> void {
    if (sess.raw_total > 0) {
        const double avg = sess.raw_matched > 0 ? sess.raw_eff_sum / static_cast<double>(sess.raw_matched) : 0.0;
        out += std::format(
                "  {}session: {}/{} matched · avg {:.1f}% of raw · target ≥95%{}\n",
                ansi::kFgGrey, sess.raw_matched, sess.raw_total, avg, ansi::kReset
        );
        return; // raw efficiency summary replaces the regression summary
    }
    const std::size_t total = sess.ok_count + sess.regression_count + sess.improvement_count + sess.severe_count;
    if (total == 0)
        return;
    // ... unchanged regression summary below ...
```

(Keep the rest of `append_summary_line` unchanged.)

`push_session_summary` (lines 294-301) currently early-returns when `compared == 0`. Update its guard so a raw-only session (no delta counters) still emits the summary:

```cpp
inline auto push_session_summary(std::vector<std::string>& lines, Session const& sess) -> void {
    const std::size_t compared = sess.ok_count + sess.regression_count + sess.improvement_count + sess.severe_count;
    if (compared == 0 && sess.raw_total == 0)
        return;
    std::string sumline;
    append_summary_line(sumline, sess);
    lines.push_back(std::move(sumline));
}
```

- [ ] **Step 8: Build the dashboard**

Run: `cmake --build --preset ninja-release --target storm_bench_dashboard`
Expected: clean build.

- [ ] **Step 9: Commit**

```bash
git add benchmarks/dashboard/wire.hpp benchmarks/dashboard/wire.cppm benchmarks/dashboard/tui.cppm \
        benchmarks/dashboard/db.hpp benchmarks/dashboard/events.hpp benchmarks/dashboard/main.cpp \
        benchmarks/dashboard/tui_render.hpp
git commit -m "feat(bench-dashboard): resolve raw baseline + render % of raw efficiency labels"
```

---

## Task 6: Unit tests for the new logic

**Files:**
- Inspect: existing dashboard tests location. Run `grep -rln "wire::parse\|format_delta\|resolve_baseline\|append_result_line\|build_run_start" tests/ benchmarks/` to find the test TU(s). The wire round-trip and render helpers are pure and unit-testable.
- Create/Modify: the dashboard wire test TU (likely `tests/...bench_dashboard...` or a `benchmarks/dashboard/tests` file — confirm by inspection).

> CLAUDE.md mandates tests BEFORE implementation. Since this plan front-loaded the wire/render work, write these tests now and confirm they pass; if any fails, it has found a real bug — fix it.

- [ ] **Step 1: Locate the wire/render test TU**

Run: `grep -rln "bench_dashboard::wire::parse\|build_run_start\|format_delta\|efficiency" tests/`
Expected: a test file exercising `wire::parse` / `build_run_start`. Note its path as `<WIRE_TEST>`.

- [ ] **Step 2: Write a failing test: `build_run_start` round-trips `is_raw`**

Add to `<WIRE_TEST>`:

```cpp
TEST(WireRunStart, IsRawRoundTrips) {
    using namespace bench_dashboard::wire;
    const auto json = build_run_start("WHERE.*", /*is_full_run=*/false, /*is_raw=*/true);
    const auto msg  = parse(json);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, MessageKind::RunStart);
    EXPECT_EQ(msg->filter, "WHERE.*");
    EXPECT_FALSE(msg->is_full_run);
    EXPECT_TRUE(msg->is_raw);
}

TEST(WireRunStart, IsRawDefaultsFalseWhenAbsent) {
    using namespace bench_dashboard::wire;
    // Older producer: run_start without is_raw must parse with is_raw=false.
    const auto msg = parse(R"({"type":"run_start","filter":"","is_full_run":true})");
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->is_full_run);
    EXPECT_FALSE(msg->is_raw);
}
```

- [ ] **Step 3: Run; confirm they pass (the wire change from Task 1 already supports this)**

Run: `cmake --build --preset ninja-debug && ctest --preset ninja-debug -R Wire`
Expected: PASS. (If `is_raw` parsing were missing they would fail — they validate Task 1.)

- [ ] **Step 4: Write tests for `efficiency_label` and `append_result_line` efficiency path**

If `tui_render.hpp` helpers are reachable from a test TU (they are header-only `inline`), add:

```cpp
TEST(EfficiencyLabel, GreenAtOrAbove95) {
    auto [colour, text] = efficiency_label(96.6);
    EXPECT_EQ(text, "96.6% of raw");
    EXPECT_EQ(colour, ansi::kFgGreen);
}

TEST(EfficiencyLabel, RedBelow95) {
    auto [colour, text] = efficiency_label(94.9);
    EXPECT_EQ(text, "94.9% of raw");
    EXPECT_EQ(colour, ansi::kFgRed);
}

TEST(AppendResultLine, ShowsEfficiencyWhenPresent) {
    wire::ResultMsg r{};
    r.test_name        = "Storm/SELECT/select/10000";
    r.real_ns          = 4.12e6;
    r.efficiency_pct   = 96.6;
    std::string out;
    append_result_line(out, r, /*regression_threshold=*/5.0);
    EXPECT_NE(out.find("96.6% of raw"), std::string::npos);
}

TEST(AppendResultLine, ShowsNoRawWhenBaselineUnmatched) {
    wire::ResultMsg r{};
    r.test_name         = "Storm/WHERE/where_bool_equality/10000";
    r.baseline_looked_up = true; // active baseline, no efficiency, no delta
    std::string out;
    append_result_line(out, r, 5.0);
    EXPECT_NE(out.find("— (no raw)"), std::string::npos);
}
```

(If these helpers are not currently reachable from any test TU because they live inside `tui.cppm`'s export namespace, add the assertions to whatever TU already includes/imports the tui render symbols — find it via `grep -rln "format_delta\|append_result_line" tests/`. If none exists, skip Step 4's render tests and rely on the manual verification in Task 7; note this in the commit.)

- [ ] **Step 5: Run the full dashboard test set**

Run: `ctest --preset ninja-debug -R "Wire|Dashboard|Efficiency|Result"`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add <WIRE_TEST> <RENDER_TEST_IF_ANY>
git commit -m "test(bench-dashboard): cover is_raw wire round-trip + efficiency labels"
```

---

## Task 7: Driver script + end-to-end manual verification

**Files:**
- Create: `benchmarks/scripts/compare_against_raw.sh`

> Mirrors the existing `benchmarks/scripts/compare_against_baseline.sh`. Read that script first and match its style, shebang, error handling, and binary-path discovery.

- [ ] **Step 1: Read the existing baseline script to match style**

Run: `cat benchmarks/scripts/compare_against_baseline.sh`
Note its conventions (set -euo pipefail, build-dir detection, how it launches the dashboard + bench).

- [ ] **Step 2: Write `compare_against_raw.sh`**

Create `benchmarks/scripts/compare_against_raw.sh` following that style. Core flow (adapt paths/helpers to match the sibling script):

```bash
#!/usr/bin/env bash
# Storm-vs-raw SQLite efficiency comparison (Issue #74).
#
# 1. Start the dashboard with --baseline raw:last (reads the most recent raw run).
# 2. Run storm_anchors streaming → produces a fresh raw baseline run (is_raw=1).
# 3. Run storm_bench streaming → each subset row shows "N% of raw".
#
# Requires a release build: cmake --preset ninja-release && cmake --build --preset ninja-release
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build/release}"
BENCH_DIR="${BUILD_DIR}/benchmarks"
DASH="${BENCH_DIR}/dashboard/storm_bench_dashboard"
ANCHORS="${BENCH_DIR}/storm_anchors"
BENCH="${BENCH_DIR}/storm_bench"

for bin in "$DASH" "$ANCHORS" "$BENCH"; do
    [[ -x "$bin" ]] || { echo "missing: $bin — build the ninja-release preset first" >&2; exit 1; }
done

cat <<EOF
Storm-vs-raw comparison
-----------------------
This script streams a raw baseline then a Storm run to the dashboard.

Run the dashboard in another terminal FIRST:

    ${DASH} --baseline raw:last

Then press Enter here to: (1) stream storm_anchors as the raw baseline,
(2) stream storm_bench. The dashboard shows 'N% of raw' on the matched
subset rows.
EOF
read -r _

echo "==> streaming raw baseline (storm_anchors)…"
STORM_BENCH_SOCKET=1 "$ANCHORS"

echo "==> streaming Storm run (storm_bench)…"
STORM_BENCH_SOCKET=1 "$BENCH" --benchmark_filter='Storm/(WHERE|SELECT|INSERT)/.*'

echo "Done. Re-start the dashboard with --baseline raw:last to pin the new raw run."
```

- [ ] **Step 3: Make it executable**

Run: `chmod +x benchmarks/scripts/compare_against_raw.sh`

- [ ] **Step 4: End-to-end manual verification (the real acceptance test)**

In terminal A:
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
./build/release/benchmarks/dashboard/storm_bench_dashboard --baseline raw:last
```
(First run: there is no raw run yet, so it prints "no baseline found" — that's expected.)

In terminal B:
```bash
STORM_BENCH_SOCKET=1 ./build/release/benchmarks/storm_anchors
```
Expected in dashboard: a new session appears. Restart the dashboard with `--baseline raw:last`; it now resolves the raw run as baseline (label shows the raw run).

Then in terminal B:
```bash
STORM_BENCH_SOCKET=1 ./build/release/benchmarks/storm_bench --benchmark_filter='Storm/(WHERE|SELECT|INSERT)/.*'
```
Expected in dashboard:
- `Storm/WHERE/where_int_comparison_gt/10000` → `NN.N% of raw` (green if ≥95)
- `Storm/WHERE/where_bool_equality/10000` → `NN.N% of raw`
- `Storm/SELECT/select/10000` → `NN.N% of raw`
- `Storm/INSERT/insert/1` → `NN.N% of raw`
- every OTHER Storm row (other sizes/categories) → `— (no raw)`
- session summary → `session: 4/M matched · avg NN.N% of raw · target ≥95%`

- [ ] **Step 5: Commit**

```bash
git add benchmarks/scripts/compare_against_raw.sh
git commit -m "feat(bench): add compare_against_raw.sh driver for Storm-vs-raw dashboard"
```

---

## Task 8: Documentation

**Files:**
- Modify: `benchmarks/README.md` (add the Storm-vs-raw workflow next to Live dashboard / Baseline comparison)
- Modify: `docs/development/BENCHMARK_DASHBOARD.md` (`raw:` selector, `is_raw` schema column + migration, efficiency-label semantics)

> CLAUDE.md: code + docs commit together. Also check whether any `.claude/agents/*.md` describes the dashboard baseline selectors or benchmark presets; if so, update it too (CLAUDE.md rule 8).

- [ ] **Step 1: Update `benchmarks/README.md`**

Read the existing "Live dashboard" / "Baseline comparison" sections, then add a "Storm vs raw SQLite" subsection documenting: run `storm_anchors` with `STORM_BENCH_SOCKET=1` to produce a raw baseline; start the dashboard with `--baseline raw:last`; the subset is intentionally partial (4 rows); how to extend it (add a benchmark with the exact Storm gbench name + matching size in `anchors_raw.cpp`). Reference `benchmarks/scripts/compare_against_raw.sh`.

- [ ] **Step 2: Update `docs/development/BENCHMARK_DASHBOARD.md`**

Document: the `--baseline raw:<id>` / `raw:last` selectors; the `is_raw` column on `BenchRun` and its migration; the efficiency-label semantics (`raw_ns / current_ns * 100`, green ≥95%, `— (no raw)` for unmatched rows, the raw session summary); the reuse semantics (raw is selected, not re-run; refreshing is a manual re-run of `storm_anchors`).

- [ ] **Step 3: Check for agent-file references**

Run: `grep -rln "baseline\|storm_anchors\|raw:" .claude/agents/`
If any agent file describes the dashboard's baseline selectors, update it to include `raw:`. If none, no change.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/README.md docs/development/BENCHMARK_DASHBOARD.md .claude/agents/
git commit -m "docs(bench): document Storm-vs-raw baseline lane and raw: selector"
```

---

## Task 9: Verification gates + PR

- [ ] **Step 1: Release build + dashboard tests**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release && ctest --preset ninja-debug -R "Wire|Dashboard|Efficiency"`
Expected: clean build, all targeted tests PASS.

- [ ] **Step 2: Sanitizers — scope check**

Per memory `feedback_skip_sanitizers_for_bench_only`: if the diff is contained to `benchmarks/` (no `src/` or `tests/` changes), the release build + bench run is sufficient and sanitizer presets (which exercise `src/`+`tests/`) cannot change. Confirm the diff scope:

Run: `git diff --name-only develop... | grep -v '^benchmarks/' | grep -v '^docs/' || echo "bench/docs only"`
- If output is "bench/docs only" → skip sanitizers (documented rule).
- If any `src/`/`tests/` files changed (e.g. the wire test landed under `tests/`) → run `cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan && ctest --preset ninja-asan-ubsan` and `ninja-tsan` likewise; revert on any new violation.

- [ ] **Step 3: Re-glob debug preset if any bench file was renamed/removed**

Per memory `feedback_reglob_debug_after_bench_rename`: pre-commit clang-format uses the cached debug glob. Since `anchors_raw.cpp` changed substantially (and tests may be new), re-run:

Run: `cmake --preset ninja-debug`

- [ ] **Step 4: Confirm the full subset matches end-to-end one more time**

Re-run Task 7 Step 4 if any code changed since. Confirm 4/4 subset rows show `% of raw` and the summary line renders.

- [ ] **Step 5: Push the feature branch + open PR**

```bash
git push -u origin feature/74-storm-vs-raw-baseline
gh pr create --base develop --title "feat: Storm-vs-raw SQLite efficiency on the bench dashboard (#74)" \
  --body "Closes #74"
```

- [ ] **Step 6: SonarCloud + CI gate**

Wait 30s, run `/sonarcloud-status`. Fix any new issues / duplications on the feature branch until the gate is clean. Then `gh pr checks <PR#> --watch` — merge only after SonarCloud AND all CI jobs pass (`gh pr merge --squash`).

- [ ] **Step 7: Close issue + return to develop**

```bash
gh issue close 74
git checkout develop && git pull
```

---

## Self-review notes (cross-checked against the spec)

- **Spec §1 (grow storm_anchors)** → Task 3. Exact Storm names + matching sizes pinned in the subset table; streaming added; stays `.cpp`; `is_raw` flag carried (via `STORM_BENCH_RAW` env → reporter → run_start).
- **Spec §2 (mark raw runs)** → Task 2. `is_raw` column on `BenchRun` via Atlas migration; flows from `run_start`.
- **Spec §3 (raw: selector + efficiency)** → Task 4 (selector) + Task 5 (resolution + labels + summary). Efficiency = `baseline_ns / current_ns * 100`, green ≥95, `— (no raw)` for unmatched, raw session summary.
- **Spec §4 (driver script)** → Task 7.
- **Spec §5 (docs)** → Task 8.
- **Pitfall #1 (both wire files)** → Tasks 1 & 5 touch `wire.hpp` + `wire.cppm` together.
- **Pitfall #2 (Atlas migration)** → Task 2 Steps 5-6 generate via makemigrations, verify apply.
- **Pitfall #3 (exact sizes)** → subset table + the `/N` suffix insight (gbench appends via `ArgName("N")`).
- **Pitfall #4 (loop boundary)** → Task 3 fixtures keep setup/seed/prepare outside `for (auto _ : state)`.
- **Pitfall #5 (backward compat)** → `is_raw{false}` default + Task 6 Step 2's absent-field test.
- **Pitfall #6 (don't reuse delta_pct)** → `efficiency_pct` is a separate field computed from the two ns values in `enrich_measurement`.

**Type consistency:** `is_raw` (bool) consistent across wire/model/Session/DashboardState; `efficiency_pct` is `std::optional<double>` everywhere; `resolve_baseline` returns `ResolvedBaseline` and all call sites updated (db.hpp internal + main.cpp); `enrich_with_baseline`/`enrich_measurement`/`replay_run_results_into_session` all gain the `baseline_is_raw` param consistently.
