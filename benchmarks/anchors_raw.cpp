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
 *   - Storm/INSERT/insert/N:<n>            (insert() path)
 *   - Storm/INSERT/insert_no_return/N:<n>  (insert<ReturnId::No>() path)
 *     for every n in BATCH_STANDARD {1,10,100,500,1000,5000,10000,50000,100000}
 *     (mirrors benchmarks/sizes.cppm). Each iteration inserts n rows as Storm
 *     does: one multi-row VALUES statement per chunk of 249 rows (999 / 4-field
 *     BenchPerson = max_allowed), wrapped in a transaction only when n spans
 *     more than one chunk (insert.cppm execute_bulk vs execute_chunked_bulk_*).
 *     RETURNING id is read back only for insert/N:1 — the Storm fixture sends
 *     N=1 through single-row insert(obj).execute() (RETURNING) but N>1 through
 *     the bulk insert(span).execute() VOID path (no RETURNING), so at N>1 the
 *     two anchors are identical plain bulk inserts.
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

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace {

    // Plain INTEGER PRIMARY KEY mirrors Storm's schema generator, which now emits
    // "id INTEGER PRIMARY KEY" by default (#379) — AUTOINCREMENT became opt-in.
    // The raw anchor must NOT use AUTOINCREMENT, or it would pay the per-insert
    // sqlite_sequence bookkeeping that Storm no longer pays, making the comparison
    // unfair (see #345 fairness work).
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

    // Plain INTEGER PRIMARY KEY to mirror Storm's default schema (see kCreatePerson note, #379).
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

    // Storm chunks a bulk INSERT at MAX_DB_VARIABLES / field_count_ rows. For the
    // INSERT-benchmark model (BenchPerson: id + name + age + salary → field_count_
    // = 4), that is 999 / 4 = 249 rows per multi-row VALUES statement
    // (insert.cppm). N ≤ 249 → one bulk INSERT, no transaction (execute_bulk);
    // N > 249 → BEGIN, one bulk INSERT per chunk, COMMIT (execute_chunked_bulk_*).
    constexpr int kInsertChunkRows = 999 / 4;

    // Build "INSERT INTO person(name, age, salary) VALUES (?,?,?),(?,?,?),..." for
    // `rows` tuples, appending RETURNING id when `returning`. Mirrors Storm's
    // multi-row VALUES SQL (insert.cppm build_bulk_insert_body).
    auto build_bulk_insert_sql(int rows, bool returning) -> std::string {
        std::string sql = "INSERT INTO person(name, age, salary) VALUES ";
        for (int i = 0; i < rows; ++i) {
            sql += (i == 0) ? "(?,?,?)" : ",(?,?,?)";
        }
        if (returning) {
            sql += " RETURNING id";
        }
        return sql;
    }

    // Bind `rows` (name, age, salary) tuples into a prepared multi-row INSERT,
    // 1-based placeholder per column. Names carry a per-call counter so rows
    // accumulate without a UNIQUE collision (the table has no UNIQUE(name)),
    // matching the Storm fixture's stamp_unique_names.
    auto bind_bulk_rows(sqlite3_stmt* ins, int rows, int& counter) -> void {
        for (int i = 0; i < rows; ++i) {
            const std::string name = std::format("P{}", counter++);
            const int         base = i * 3;
            sqlite3_bind_text(ins, base + 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, base + 2, 30);
            sqlite3_bind_double(ins, base + 3, 50'000.0);
        }
    }

    // Step a bound multi-row INSERT to completion. RETURNING yields one row per
    // inserted tuple (read each id so the optimizer can't elide it) before
    // SQLITE_DONE; plain INSERT steps straight to SQLITE_DONE.
    auto step_bulk_insert(sqlite3* db, sqlite3_stmt* ins, bool returning) -> void {
        if (returning) {
            while (sqlite3_step(ins) == SQLITE_ROW) {
                benchmark::DoNotOptimize(sqlite3_column_int(ins, 0));
            }
        } else if (sqlite3_step(ins) != SQLITE_DONE) {
            die(db, "insert step");
        }
        sqlite3_reset(ins);
    }

    // How Storm splits an N-row bulk INSERT into chunks: `full_chunks` full
    // statements of kInsertChunkRows each, plus a `remainder_rows` tail (0 when
    // N divides evenly). `chunked` is true when N spans more than one chunk, the
    // condition under which Storm wraps the inserts in a transaction. The two
    // prepared statements (one per distinct chunk shape) are built once and
    // reused across timed iterations, mirroring Storm's cached prepares.
    struct InsertPlan {
        sqlite3_stmt* full_stmt;
        sqlite3_stmt* rem_stmt;
        int           full_chunks;
        int           remainder_rows;
        bool          returning;
        bool          chunked;
    };

    auto make_insert_plan(sqlite3* db, int n, bool returning) -> InsertPlan {
        int const  full_chunks    = n / kInsertChunkRows;
        int const  remainder_rows = n % kInsertChunkRows;
        bool const chunked        = (full_chunks + (remainder_rows > 0 ? 1 : 0)) > 1;
        return InsertPlan{
                .full_stmt = full_chunks > 0 ? prepare(db, build_bulk_insert_sql(kInsertChunkRows, returning).c_str())
                                             : nullptr,
                .rem_stmt  = remainder_rows > 0 ? prepare(db, build_bulk_insert_sql(remainder_rows, returning).c_str())
                                                : nullptr,
                .full_chunks    = full_chunks,
                .remainder_rows = remainder_rows,
                .returning      = returning,
                .chunked        = chunked,
        };
    }

    // One timed batch: step every chunk in the plan, wrapped in BEGIN/COMMIT only
    // when plan.chunked — matching Storm's execute_bulk (no txn) vs
    // execute_chunked_bulk_* (txn) split.
    auto run_insert_chunks(sqlite3* db, InsertPlan const& plan, int& counter) -> void {
        if (plan.chunked) {
            exec(db, "BEGIN");
        }
        for (int c = 0; c < plan.full_chunks; ++c) {
            bind_bulk_rows(plan.full_stmt, kInsertChunkRows, counter);
            step_bulk_insert(db, plan.full_stmt, plan.returning);
        }
        if (plan.rem_stmt != nullptr) {
            bind_bulk_rows(plan.rem_stmt, plan.remainder_rows, counter);
            step_bulk_insert(db, plan.rem_stmt, plan.returning);
        }
        if (plan.chunked) {
            exec(db, "COMMIT");
        }
    }

    // Shared batch INSERT driver for both anchors. Builds the table and the
    // chunk plan once, then per timed iteration inserts state.range(0) rows the
    // way Storm does (see run_insert_chunks). Only the bind + step + (optional)
    // BEGIN/COMMIT is timed, mirroring the Storm fixture (setup outside the loop,
    // cached prepares).
    //
    // `returning` selects the insert() vs insert_no_return() anchor, but RETURNING
    // is only actually emitted at N=1. The Storm fixture dispatches N=1 through
    // the single-row insert(obj).execute() path (RETURNING id, execute_single_*)
    // and N>1 through the bulk insert(span).execute() VOID path — which has no
    // RETURNING clause (queryset.cppm: the default bulk .execute() returns void).
    // So at N>1 both anchors are plain bulk inserts; only N=1 of the insert
    // anchor reads back an id.
    auto run_insert_benchmark(benchmark::State& state, bool returning) -> void {
        int const  n             = static_cast<int>(state.range(0));
        bool const use_returning = returning && n == 1;
        sqlite3*   db            = open_memory_db();
        exec(db, kCreatePerson);

        InsertPlan const plan = make_insert_plan(db, n, use_returning);

        int counter = 0;
        for (auto _ : state) {
            run_insert_chunks(db, plan, counter);
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n);
        state.SetComplexityN(state.range(0));

        if (plan.full_stmt != nullptr) {
            sqlite3_finalize(plan.full_stmt);
        }
        if (plan.rem_stmt != nullptr) {
            sqlite3_finalize(plan.rem_stmt);
        }
        sqlite3_close(db);
    }

    // BATCH_STANDARD from benchmarks/sizes.cppm — the exact sweep Storm's INSERT
    // benchmarks register, so the dashboard's (test_name, dataset_size) matcher
    // pairs every Storm INSERT row with this raw baseline.
    constexpr std::array kBatchStandard = {1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000};

    auto register_insert_anchor(char const* name, void (*fn)(benchmark::State&)) -> void {
        auto* bench = benchmark::RegisterBenchmark(name, fn);
        for (int n : kBatchStandard) {
            bench->Arg(n);
        }
        bench->Complexity(benchmark::oN)->ArgName("N");
    }

    // Storm/INSERT/insert_no_return/N:<n> — plain bulk INSERT, no RETURNING
    auto BM_Raw_Insert_No_Return(benchmark::State& state) -> void {
        run_insert_benchmark(state, /*returning=*/false);
    }

    // Storm/INSERT/insert/N:<n> — bulk INSERT … RETURNING id (mirrors Storm's insert() path)
    auto BM_Raw_Insert_Returning(benchmark::State& state) -> void {
        run_insert_benchmark(state, /*returning=*/true);
    }

} // namespace

auto main(int argc, char** argv) -> int { // NOLINT(bugprone-exception-escape)
    // INSERT anchors register at runtime (not via the BENCHMARK macro) so the
    // BATCH_STANDARD sweep can be applied programmatically. Must run before
    // benchmark::Initialize / RunSpecifiedBenchmarks.
    register_insert_anchor("Storm/INSERT/insert_no_return", &BM_Raw_Insert_No_Return);
    register_insert_anchor("Storm/INSERT/insert", &BM_Raw_Insert_Returning);

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
