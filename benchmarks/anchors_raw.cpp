// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
/**
 * Storm raw SQLite anchor benchmarks (Issue #235 — Phase 4).
 *
 * Sparse, intentional spot checks of raw SQLite throughput so the project's
 * "96–108% of raw SQLite" claim can be re-verified at release time without
 * pairing every Storm benchmark with a raw counterpart.
 *
 * Lives in its own binary (`storm_anchors`) — does NOT import storm and is
 * NOT invoked by the per-PR regression workflow. Release-time spot check only.
 *
 * Raw subset (exact Storm gbench names, so the dashboard's (test_name,
 * dataset_size) matcher slots them against the Storm rows they mirror):
 *   - Storm/WHERE/where_int_comparison_gt/N:10000
 *   - Storm/WHERE/where_bool_equality/N:10000
 *   - Storm/SELECT/select/N:10000
 *   - Storm/INSERT/insert/N:1           (INSERT … RETURNING id)
 *   - Storm/INSERT/insert_no_return/N:1 (plain INSERT)
 *
 * When STORM_BENCH_SOCKET is set, streams over the dashboard wire with
 * is_raw=true (run_start), producing the Storm-vs-raw baseline run.
 *
 * Schema is hand-rolled — no Storm model coupling.
 *
 * Kept as a plain .cpp (not .cppm). Converting to a module unit segfaults
 * clang-p2996 inside ASTWriter::GenerateNameLookupTable when it tries to
 * serialize the BMI for the BENCHMARK(...) macro's static-init globals.
 */

#include <benchmark/benchmark.h>
#include <sqlite3.h>

#include "dashboard/reporter.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace {

    constexpr auto kCreatePerson = "CREATE TABLE person ("
                                   "id INTEGER PRIMARY KEY,"
                                   "name TEXT NOT NULL,"
                                   "age INTEGER NOT NULL,"
                                   "salary REAL NOT NULL"
                                   ")";

    auto die(sqlite3* db, char const* what) -> void {
        std::
                fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg)
                        stderr,
                        "anchors_raw: %s: %s\n",
                        what,
                        sqlite3_errmsg(db)
                );
        std::exit(1); // NOLINT(concurrency-mt-unsafe)
    }

    auto open_memory_db() -> sqlite3* {
        sqlite3* db = nullptr;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
            die(db, "sqlite3_open");
        }
        return db;
    }

    auto exec(sqlite3* db, char const* sql) -> void {
        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::fprintf(stderr, "anchors_raw: exec: %s\n", err); // NOLINT(cppcoreguidelines-pro-type-vararg)
            sqlite3_free(err);
            std::exit(1); // NOLINT(concurrency-mt-unsafe)
        }
    }

    auto prepare(sqlite3* db, char const* sql) -> sqlite3_stmt* {
        sqlite3_stmt* stmt = nullptr; // NOLINT(misc-const-correctness) — written by sqlite3_prepare_v2
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            die(db, sql);
        }
        return stmt;
    }

    // Bind the shared (name, age, salary) columns for row i. is_active, when
    // present, is bound separately by the caller (kept out of the helper to
    // keep both seeders sharing a single bind path). SQLITE_TRANSIENT copies
    // the name immediately, so the local string need not outlive the bind.
    auto bind_person_base(sqlite3_stmt* ins, int i) -> void {
        const std::string name = std::format("Person{}", i);
        sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 2, 20 + (i % 50));
        sqlite3_bind_double(ins, 3, 30'000.0 + static_cast<double>(i));
    }

    // One seed driver for both schemas. When with_active is true the prepared
    // INSERT has a fourth (is_active) placeholder, bound to i % 2.
    auto seed_person_table(sqlite3* db, int rows, char const* insert_sql, bool with_active) -> void {
        exec(db, "BEGIN");
        sqlite3_stmt* ins = prepare(db, insert_sql);
        for (int i = 0; i < rows; ++i) {
            bind_person_base(ins, i);
            if (with_active) {
                sqlite3_bind_int(ins, 4, i % 2);
            }
            if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "seed step");
            }
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
        exec(db, "COMMIT");
    }

    auto seed_person(sqlite3* db, int rows) -> void {
        seed_person_table(db, rows, "INSERT INTO person(name, age, salary) VALUES(?,?,?)", /*with_active=*/false);
    }

    constexpr auto kCreatePersonBool = "CREATE TABLE person ("
                                       "id INTEGER PRIMARY KEY,"
                                       "name TEXT NOT NULL,"
                                       "age INTEGER NOT NULL,"
                                       "salary REAL NOT NULL,"
                                       "is_active INTEGER NOT NULL"
                                       ")";

    auto seed_person_bool(sqlite3* db, int rows) -> void {
        seed_person_table(
                db, rows, "INSERT INTO person(name, age, salary, is_active) VALUES(?,?,?,?)", /*with_active=*/true
        );
    }

    // Step a prepared SELECT to exhaustion, touching all four (id, name, age,
    // salary) columns so the optimizer can't elide the read. Returns the row
    // count, then resets the statement for the next iteration.
    auto drain_person_select(sqlite3_stmt* sel) -> int {
        int rows = 0;
        while (sqlite3_step(sel) == SQLITE_ROW) {
            benchmark::DoNotOptimize(sqlite3_column_int(sel, 0));
            benchmark::DoNotOptimize(sqlite3_column_text(sel, 1));
            benchmark::DoNotOptimize(sqlite3_column_int(sel, 2));
            benchmark::DoNotOptimize(sqlite3_column_double(sel, 3));
            ++rows;
        }
        sqlite3_reset(sel);
        return rows;
    }

    constexpr int kWhereSeedRows = 10'000;
    constexpr int kSelectRows    = 10'000;

    // Shared SELECT driver: build the table via create_sql, seed it with
    // state.range(0) rows (the single source of truth gbench also reports as
    // /N), then time query_sql to exhaustion. assert_full_count gates the
    // row-count check against the seeded count (filtered queries return a
    // variable count, so they pass false); items_per_row scales
    // SetItemsProcessed (1 for filtered scans, the full result size for the
    // full-table scan). Setup is outside the timed loop to mirror the Storm
    // fixtures — only the query is measured.
    auto run_select_benchmark(
            benchmark::State& state,
            char const*       create_sql,
            void (*seeder)(sqlite3*, int),
            char const*  query_sql,
            bool         assert_full_count,
            std::int64_t items_per_row
    ) -> void {
        int const seed_rows = static_cast<int>(state.range(0));
        sqlite3*  db        = open_memory_db();
        exec(db, create_sql);
        seeder(db, seed_rows);
        sqlite3_stmt* sel = prepare(db, query_sql);

        for (auto _ : state) {
            int const rows = drain_person_select(sel);
            if (assert_full_count && rows != seed_rows) {
                die(db, "select row count mismatch");
            }
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * items_per_row);
        state.SetComplexityN(state.range(0));

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }

    // Storm/WHERE/where_int_comparison_gt/N:10000 — SELECT … WHERE age > 30
    auto BM_Raw_Where_IntGt(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary FROM person WHERE age > 30",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_IntGt)->Name("Storm/WHERE/where_int_comparison_gt")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/WHERE/where_bool_equality/N:10000 — SELECT … WHERE is_active = 1
    auto BM_Raw_Where_BoolEq(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePersonBool,
                seed_person_bool,
                "SELECT id, name, age, salary FROM person WHERE is_active = 1",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_BoolEq)->Name("Storm/WHERE/where_bool_equality")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/SELECT/select/N:10000 — sequential SELECT over 10K rows
    auto BM_Raw_Select_All(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary FROM person",
                /*assert_full_count=*/true,
                state.range(0)
        );
    }
    BENCHMARK(BM_Raw_Select_All)->Name("Storm/SELECT/select")->Arg(kSelectRows)->ArgName("N");

    // Shared single-row INSERT driver for both anchors. Builds the table, then
    // times insert_sql to a counter-named row per iteration (rows accumulate —
    // no DELETE, no UNIQUE on name). step_row mirrors the dialect: plain INSERT
    // steps once to SQLITE_DONE; RETURNING steps to SQLITE_ROW, reads the id,
    // then steps again to SQLITE_DONE. Setup is outside the timed loop to match
    // the Storm fixtures — only the bind + step is measured.
    auto run_insert_benchmark(benchmark::State& state, char const* insert_sql, bool returning) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        sqlite3_stmt* ins = prepare(db, insert_sql);

        int counter = 0;
        for (auto _ : state) {
            const std::string name = std::format("P{}", counter++);
            sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, 30);
            sqlite3_bind_double(ins, 3, 50'000.0);
            if (returning) {
                if (sqlite3_step(ins) != SQLITE_ROW) {
                    die(db, "insert returning step");
                }
                benchmark::DoNotOptimize(sqlite3_column_int(ins, 0));
                if (sqlite3_step(ins) != SQLITE_DONE) {
                    die(db, "insert returning done");
                }
            } else if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "insert step");
            }
            sqlite3_reset(ins);
        }
        state.SetItemsProcessed(state.iterations());
        state.SetComplexityN(state.range(0));

        sqlite3_finalize(ins);
        sqlite3_close(db);
    }

    // Storm/INSERT/insert_no_return/N:1 — plain INSERT, no RETURNING
    auto BM_Raw_Insert_No_Return(benchmark::State& state) -> void {
        run_insert_benchmark(state, "INSERT INTO person(name, age, salary) VALUES(?,?,?)", /*returning=*/false);
    }
    BENCHMARK(BM_Raw_Insert_No_Return)->Name("Storm/INSERT/insert_no_return")->Arg(1)->ArgName("N");

    // Storm/INSERT/insert/N:1 — INSERT with RETURNING id (mirrors Storm's insert() path)
    auto BM_Raw_Insert_Returning(benchmark::State& state) -> void {
        run_insert_benchmark(
                state, "INSERT INTO person(name, age, salary) VALUES(?,?,?) RETURNING id", /*returning=*/true
        );
    }
    BENCHMARK(BM_Raw_Insert_Returning)->Name("Storm/INSERT/insert")->Arg(1)->ArgName("N");

} // namespace

auto main(int argc, char** argv) -> int { // NOLINT(bugprone-exception-escape)
    // Stream to the dashboard when STORM_BENCH_SOCKET is set, marking this run
    // raw (is_raw=true via STORM_BENCH_RAW, read by the reporter) so the
    // dashboard treats it as a Storm-vs-raw baseline. STORM_BENCH_SOCKET unset →
    // default text reporter, no network calls (release-time spot check).
    ::benchmark::BenchmarkReporter* dashboard_reporter = nullptr;
    if (std::getenv("STORM_BENCH_SOCKET") != nullptr) {    // NOLINT(concurrency-mt-unsafe)
        ::setenv("STORM_BENCH_RAW", "1", /*overwrite=*/1); // NOLINT(concurrency-mt-unsafe)
        // Empty filter is intentional: this fixed raw-subset binary always runs
        // its full set, so run_start reports is_full_run=true (not a bug).
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
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
