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
 *   - Storm/WHERE/where_double_comparison/N:10000
 *   - Storm/WHERE/where_int_less_than/N:10000
 *   - Storm/SELECT/select/N:<n> for every n in dataset_standard
 *     {100,1000,10000,100000} (RangeMultiplier(10)->Range(100,100000), the
 *     same registration shape Storm's own "select" test uses)
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
 *   - Storm/UPDATE_PK/update_pk/N:<n> — UPDATE-by-PK over the full Person model,
 *     same BATCH_STANDARD sweep. n==1 binds+executes once, no transaction; n>1
 *     reuses one prepared statement across the whole batch inside a single
 *     BEGIN/COMMIT (update.cppm UpdateStatement::execute(span)).
 *   - Storm/DELETE_PK/delete_pk/N:<n> — DELETE-by-PK, same sweep. Storm's
 *     fixture re-seeds the table on EVERY iteration before erasing (see the
 *     DELETE_PK section below for why), so this anchor times clear+reinsert+
 *     reselect+delete, not a bare DELETE — anchoring only the DELETE would
 *     produce a meaningless ratio against what "delete_pk" actually measures.
 *     n==1 issues a single "id = ?" DELETE (no transaction); 2..799 issues one
 *     "id IN (...)" statement (no transaction); 800+ chunks the IN-list at 799
 *     rows/statement inside a single BEGIN/COMMIT (erase.cppm EraseStatement,
 *     single-PK MAX_CHUNK_ROWS = 999*4/5 = 799).
 *   - Storm/INSERT_EDGE/insert_edge/N:<n> and Storm/UPDATE_PK_EDGE/
 *     update_pk_edge/N:<n> for n in BATCH_INSERT_EDGE {248,249,250} and
 *     BATCH_UPDATE_EDGE {198,199,200} respectively — both categories sweep
 *     the same "insert"/"update_pk" operation as the plain anchors above
 *     (benchmark_tests.yaml), just at boundary sizes, so they reuse
 *     BM_Raw_Insert_Returning / BM_Raw_Update_Pk verbatim under a second
 *     registered name rather than duplicating logic. insert_edge's 249 is a
 *     real boundary (999/4 fields, insert.cppm); update_pk_edge's 199 is NOT
 *     — UpdateStatement::execute(span) has no chunk arithmetic at all (see
 *     kBatchUpdateEdge's comment in anchors_raw_crud.hpp) — it's swept only
 *     so that Storm benchmark gets a raw counterpart at all.
 *   (#552 tier (a) — UPDATE_PK/DELETE_PK/their _EDGE sweeps, plus completing
 *   WHERE to all 4 benchmarks. Tier (b) — the JOIN family — and the rest of
 *   the ~53-category suite are deliberately left unanchored; see #552 for the
 *   coverage analysis and the wall-clock-vs-work-covered tradeoff that ruled
 *   out AGGREGATE and SETOP despite their size.)
 *
 * When STORM_BENCH_SOCKET is set, streams over the dashboard wire with
 * is_raw=true (run_start), producing the Storm-vs-raw baseline run.
 *
 * Schema is hand-rolled — no Storm model coupling — but mirrors the model each
 * benchmark uses: the SELECT/WHERE anchors run on the FULL 10-field Person
 * table and materialize every row into a plf::hive<PersonRow>, exactly as
 * Storm's select().execute() builds a plf::hive<Person>, so both sides pay the
 * same per-row construct + container cost (fairness audit #68). The INSERT
 * anchors keep a narrow 4-field BenchPerson table.
 *
 * Kept as a plain .cpp (not .cppm). Converting to a module unit segfaults
 * clang-p2996 inside ASTWriter::GenerateNameLookupTable when it tries to
 * serialize the BMI for the BENCHMARK(...) macro's static-init globals.
 */

#include <benchmark/benchmark.h>
#include <plf_hive/plf_hive.h>
#include <sqlite3.h>

#include "dashboard/reporter.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    // SELECT-family anchor schema. The SELECT/WHERE benchmarks run on the FULL
    // 10-field Person model (shared/models.h) — Storm's select().execute()
    // materializes a plf::hive<Person> where every row carries two strings, two
    // optionals and a BLOB vector. The raw anchor must model the same columns,
    // or it would read a far lighter row than Storm and the comparison would be
    // unfair (#68). This byte-mirrors Storm's SQLite schema generator output for
    // Person: plain "id INTEGER PRIMARY KEY" (no AUTOINCREMENT — that became
    // opt-in in #379 and adds per-insert sqlite_sequence cost Storm no longer
    // pays), "name TEXT NOT NULL UNIQUE" (storm::unique), "is_active INTEGER
    // NOT NULL DEFAULT 0" (bool default clause), nullable score/nickname/avatar.
    // The INSERT anchors keep their own narrow BenchPerson table (kCreateBenchPerson).
    constexpr auto kCreatePerson = "CREATE TABLE person ("
                                   "id INTEGER PRIMARY KEY,"
                                   "name TEXT NOT NULL UNIQUE,"
                                   "age INTEGER NOT NULL,"
                                   "salary REAL NOT NULL,"
                                   "is_active INTEGER NOT NULL DEFAULT 0,"
                                   "years_experience INTEGER NOT NULL,"
                                   "department TEXT NOT NULL,"
                                   "score INTEGER,"
                                   "nickname TEXT,"
                                   "avatar BLOB"
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

    // Step a bound (non-RETURNING, non-SELECT) statement once to SQLITE_DONE,
    // dying with `what` on any other result. Shared by every plain insert/
    // update/delete step site below (seed loops, bulk chunk runners) so the
    // step→check→die idiom appears once instead of once per call site.
    auto step_done_or_die(sqlite3* db, sqlite3_stmt* stmt, char const* what) -> void {
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            die(db, what);
        }
    }

    // Column list for the full-Person INSERT used to seed the SELECT/WHERE
    // anchors. Mirrors the field set the Storm SELECT/WHERE benchmark inserts
    // (QueryBenchmark::create_model for Person): name/age/salary/is_active/score
    // are populated; years_experience defaults to 0, department to '', nickname
    // and avatar stay NULL — so the seeded NULL distribution (and thus the
    // per-row optional/blob extraction cost) matches Storm.
    constexpr auto kInsertPersonFull =
            "INSERT INTO person(name, age, salary, is_active, years_experience, department, score, nickname, avatar) "
            "VALUES(?,?,?,?,?,?,?,?,?)";

    // Shared seed-loop shape behind every full-Person INSERT seeder below:
    // BEGIN, prepare kInsertPersonFull once, bind+step+reset `rows` times,
    // finalize, COMMIT — the row content is entirely `bind_row`'s decision.
    // Setup only (outside any timed loop) in every caller.
    template <typename BindRow>
    auto run_person_seed(sqlite3* db, int rows, char const* step_what, BindRow&& bind_row) -> void {
        exec(db, "BEGIN");
        sqlite3_stmt* ins = prepare(db, kInsertPersonFull);
        for (int idx = 0; idx < rows; ++idx) {
            bind_row(ins, idx + 1);
            step_done_or_die(db, ins, step_what);
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
        exec(db, "COMMIT");
    }

    // Seed `rows` full-Person records, mirroring QueryBenchmark::create_model
    // (i = index + 1): name "Person{i}", age 20+(i%50), salary 30000+(i*1000),
    // is_active (i%2==0), score non-null only when (i%3==0). nickname and avatar
    // bind NULL.
    auto seed_person(sqlite3* db, int rows) -> void {
        run_person_seed(db, rows, "seed step", [](sqlite3_stmt* ins, int i) {
            const std::string name = std::format("Person{}", i);
            sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, 20 + (i % 50));
            sqlite3_bind_double(ins, 3, 30'000.0 + (static_cast<double>(i) * 1000.0));
            sqlite3_bind_int(ins, 4, (i % 2 == 0) ? 1 : 0);
            sqlite3_bind_int(ins, 5, 0);                      // years_experience (default)
            sqlite3_bind_text(ins, 6, "", -1, SQLITE_STATIC); // department (default "")
            if (i % 3 == 0) {
                sqlite3_bind_int(ins, 7, 60 + (i % 40)); // score (optional, set)
            } else {
                sqlite3_bind_null(ins, 7); // score (optional, nullopt)
            }
            sqlite3_bind_null(ins, 8); // nickname (optional, nullopt)
            sqlite3_bind_null(ins, 9); // avatar (BLOB, empty/NULL)
        });
    }

    // Row materialized per SELECT result, mirroring the full Person struct
    // (shared/models.h) that Storm's select().execute() builds. Storm collects
    // these into a plf::hive<Person>; the raw anchor must materialize the same
    // 10 fields into a plf::hive so both sides pay the identical per-row
    // construct (strings, optionals, blob vector) + hive-insert cost (fairness
    // rule "same container types", #68).
    struct PersonRow {
        int                        id;
        std::string                name;
        int                        age;
        double                     salary;
        bool                       is_active;
        int                        years_experience;
        std::string                department;
        std::optional<int>         score;
        std::optional<std::string> nickname;
        std::vector<std::uint8_t>  avatar;
    };

    // Read column `col` as a std::string (TEXT NOT NULL — never NULL).
    auto column_string(sqlite3_stmt* sel, int col) -> std::string {
        return reinterpret_cast<char const*>(sqlite3_column_text(sel, col));
    }

    // Read a nullable INTEGER column into std::optional<int>, mirroring Storm's
    // extract_column_value<optional<T>> (nullopt on SQLITE_NULL).
    auto column_optional_int(sqlite3_stmt* sel, int col) -> std::optional<int> {
        if (sqlite3_column_type(sel, col) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int(sel, col);
    }

    // Read a nullable TEXT column into std::optional<std::string>.
    auto column_optional_string(sqlite3_stmt* sel, int col) -> std::optional<std::string> {
        if (sqlite3_column_type(sel, col) == SQLITE_NULL) {
            return std::nullopt;
        }
        return column_string(sel, col);
    }

    // Read a BLOB column into std::vector<uint8_t>, mirroring Storm's
    // extract_blob_like (empty vector when NULL / zero-length).
    auto column_blob(sqlite3_stmt* sel, int col) -> std::vector<std::uint8_t> {
        void const* blob = sqlite3_column_blob(sel, col);
        int const   size = sqlite3_column_bytes(sel, col);
        if (blob == nullptr || size <= 0) {
            return {};
        }
        auto const* data = static_cast<std::uint8_t const*>(blob);
        return {data, data + size};
    }

    // Step a prepared SELECT to exhaustion, materializing each row into a
    // plf::hive<PersonRow> exactly as Storm's execute_query_loop does
    // (`T obj; extractor(stmt, obj); results.insert(std::move(obj));`,
    // select.cppm). All 10 Person columns are extracted with the same
    // null-handling Storm uses, so both sides build byte-comparable rows.
    // Returns the hive count, then resets the statement for the next iteration.
    // The hive is kept alive past the loop via DoNotOptimize so the optimizer
    // can't elide the inserts.
    auto drain_person_select(sqlite3_stmt* sel) -> int {
        plf::hive<PersonRow> results;
        while (sqlite3_step(sel) == SQLITE_ROW) {
            PersonRow obj{
                    .id               = sqlite3_column_int(sel, 0),
                    .name             = column_string(sel, 1),
                    .age              = sqlite3_column_int(sel, 2),
                    .salary           = sqlite3_column_double(sel, 3),
                    .is_active        = sqlite3_column_int(sel, 4) != 0,
                    .years_experience = sqlite3_column_int(sel, 5),
                    .department       = column_string(sel, 6),
                    .score            = column_optional_int(sel, 7),
                    .nickname         = column_optional_string(sel, 8),
                    .avatar           = column_blob(sel, 9),
            };
            results.insert(std::move(obj));
        }
        benchmark::DoNotOptimize(results);
        int const rows = static_cast<int>(results.size());
        sqlite3_reset(sel);
        return rows;
    }

    constexpr int kWhereSeedRows = 10'000;

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

        // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) -- Google Benchmark loop idiom: `_` is the iteration token, never read
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

    // Full-Person column list — Storm's SELECT materializes every Person field,
    // so the raw anchors select all 10 columns (struct order) into PersonRow.
    constexpr auto kSelectPersonCols = "SELECT id, name, age, salary, is_active, years_experience, department, score, "
                                       "nickname, avatar FROM person";

    // Storm/WHERE/where_int_comparison_gt/N:10000 — SELECT … WHERE age > 30
    auto BM_Raw_Where_IntGt(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary, is_active, years_experience, department, score, nickname, avatar FROM "
                "person WHERE age > 30",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_IntGt)->Name("Storm/WHERE/where_int_comparison_gt")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/WHERE/where_bool_equality/N:10000 — SELECT … WHERE is_active = 1
    auto BM_Raw_Where_BoolEq(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary, is_active, years_experience, department, score, nickname, avatar FROM "
                "person WHERE is_active = 1",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_BoolEq)->Name("Storm/WHERE/where_bool_equality")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/WHERE/where_double_comparison/N:10000 — SELECT … WHERE salary >= 50000.0
    auto BM_Raw_Where_DoubleGe(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary, is_active, years_experience, department, score, nickname, avatar FROM "
                "person WHERE salary >= 50000.0",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_DoubleGe)->Name("Storm/WHERE/where_double_comparison")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/WHERE/where_int_less_than/N:10000 — SELECT … WHERE age < 25
    auto BM_Raw_Where_IntLt(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                "SELECT id, name, age, salary, is_active, years_experience, department, score, nickname, avatar FROM "
                "person WHERE age < 25",
                /*assert_full_count=*/false,
                1
        );
    }
    BENCHMARK(BM_Raw_Where_IntLt)->Name("Storm/WHERE/where_int_less_than")->Arg(kWhereSeedRows)->ArgName("N");

    // Storm/SELECT/select/N:<n> — sequential SELECT over n rows, n in
    // dataset_standard {100, 1000, 10000, 100000} (benchmark_tests.yaml;
    // register.cpp's range_for: RangeMultiplier(10)->Range(100, 100000) —
    // the same registration shape Storm's own "select" test uses). Was
    // pinned to a single N:10000 point; widened so all four Storm/SELECT/
    // select/N:<n> rows get a raw counterpart, not just one of the four.
    auto BM_Raw_Select_All(benchmark::State& state) -> void {
        run_select_benchmark(
                state,
                kCreatePerson,
                seed_person,
                kSelectPersonCols,
                /*assert_full_count=*/true,
                state.range(0)
        );
    }
    BENCHMARK(BM_Raw_Select_All)
            ->Name("Storm/SELECT/select")
            ->RangeMultiplier(10)
            ->Range(100, 100'000)
            ->Complexity(benchmark::oN)
            ->ArgName("N");

    // INSERT anchors — split into anchors_raw_insert.hpp to keep this file under
    // the project's per-file line budget (#552). Included here (not at the top
    // of the file) so it lands after die()/open_memory_db()/exec()/prepare()/
    // step_done_or_die(), which it depends on.
#include "anchors_raw_insert.hpp"

    // UPDATE_PK / DELETE_PK anchors (#552 tier (a)) — split into
    // anchors_raw_crud.hpp for the same reason, included after
    // anchors_raw_insert.hpp since it depends on ChunkPlan/run_chunked_pass and
    // on kCreatePerson/seed_person/kInsertPersonFull/kSelectPersonCols/
    // drain_person_select/run_person_seed defined earlier in this file.
#include "anchors_raw_crud.hpp"

} // namespace

auto main(int argc, char** argv) -> int { // NOLINT(bugprone-exception-escape)
    // INSERT/UPDATE_PK/DELETE_PK anchors (plus their _EDGE boundary-size
    // counterparts) register at runtime (not via the BENCHMARK macro) so
    // per-category sweeps can be applied programmatically. Must run before
    // benchmark::Initialize / RunSpecifiedBenchmarks.
    register_batch_standard_anchor("Storm/INSERT/insert_no_return", &BM_Raw_Insert_No_Return);
    register_batch_standard_anchor("Storm/INSERT/insert", &BM_Raw_Insert_Returning);
    register_batch_standard_anchor("Storm/UPDATE_PK/update_pk", &BM_Raw_Update_Pk);
    register_batch_standard_anchor("Storm/DELETE_PK/delete_pk", &BM_Raw_Delete_Pk);
    // insert_edge/update_pk_edge reuse the plain insert/update_pk anchors
    // verbatim (same operation, benchmark_tests.yaml) under the edge-size
    // sweeps instead of BATCH_STANDARD.
    register_sized_anchor("Storm/INSERT_EDGE/insert_edge", &BM_Raw_Insert_Returning, kBatchInsertEdge);
    register_sized_anchor("Storm/UPDATE_PK_EDGE/update_pk_edge", &BM_Raw_Update_Pk, kBatchUpdateEdge);

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
