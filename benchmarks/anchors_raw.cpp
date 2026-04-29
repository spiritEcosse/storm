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
 * Four anchors:
 *   - Anchor_Raw_InsertSingleRow   single-row INSERT throughput
 *   - Anchor_Raw_SelectByPK_1K     PK lookup over a 1K-row table
 *   - Anchor_Raw_BatchInsert_1000  multi-row INSERT, 1000 rows / iteration
 *   - Anchor_Raw_FullScan_10K      sequential SELECT over 10K rows
 *
 * Schema is hand-rolled — no Storm model coupling.
 *
 * Kept as a plain .cpp (not .cppm). Converting to a module unit segfaults
 * clang-p2996 inside ASTWriter::GenerateNameLookupTable when it tries to
 * serialize the BMI for the BENCHMARK(...) macro's static-init globals.
 */

#include <benchmark/benchmark.h>
#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

    constexpr auto kCreatePerson = "CREATE TABLE person ("
                                   "id INTEGER PRIMARY KEY,"
                                   "name TEXT NOT NULL,"
                                   "age INTEGER NOT NULL,"
                                   "salary REAL NOT NULL"
                                   ")";

    constexpr int kSelect1KRows  = 1'000;
    constexpr int kBatchSize1000 = 1'000;
    constexpr int kFullScan10K   = 10'000;

    auto die(sqlite3* db, char const* what) -> void {
        std::fprintf(stderr, "anchors_raw: %s: %s\n", what, sqlite3_errmsg(db));
        std::exit(1);
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
            std::fprintf(stderr, "anchors_raw: exec: %s\n", err);
            sqlite3_free(err);
            std::exit(1);
        }
    }

    auto prepare(sqlite3* db, char const* sql) -> sqlite3_stmt* {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            die(db, sql);
        }
        return stmt;
    }

    auto seed_person(sqlite3* db, int rows) -> void {
        exec(db, "BEGIN");
        sqlite3_stmt* ins = prepare(db, "INSERT INTO person(name, age, salary) VALUES(?,?,?)");
        for (int i = 0; i < rows; ++i) {
            std::string name = "Person" + std::to_string(i);
            sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, 20 + (i % 50));
            sqlite3_bind_double(ins, 3, 30'000.0 + static_cast<double>(i));
            if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "seed step");
            }
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
        exec(db, "COMMIT");
    }

    // ========================================================================
    // Anchor_Raw_InsertSingleRow — single-row INSERT throughput
    // ========================================================================
    auto BM_Anchor_Raw_InsertSingleRow(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        sqlite3_stmt* ins = prepare(db, "INSERT INTO person(name, age, salary) VALUES(?,?,?)");

        int counter = 0;
        for (auto _ : state) {
            std::string name = "P" + std::to_string(counter++);
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
    BENCHMARK(BM_Anchor_Raw_InsertSingleRow);

    // ========================================================================
    // Anchor_Raw_SelectByPK_1K — PK lookup over a 1K-row table
    // ========================================================================
    auto BM_Anchor_Raw_SelectByPK_1K(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        seed_person(db, kSelect1KRows);
        sqlite3_stmt* sel = prepare(db, "SELECT id, name, age, salary FROM person WHERE id = ?");

        int probe = 0;
        for (auto _ : state) {
            int const id = (probe++ % kSelect1KRows) + 1;
            sqlite3_bind_int(sel, 1, id);
            if (sqlite3_step(sel) != SQLITE_ROW) {
                die(db, "select step");
            }
            benchmark::DoNotOptimize(sqlite3_column_int(sel, 0));
            benchmark::DoNotOptimize(sqlite3_column_text(sel, 1));
            benchmark::DoNotOptimize(sqlite3_column_int(sel, 2));
            benchmark::DoNotOptimize(sqlite3_column_double(sel, 3));
            sqlite3_reset(sel);
        }
        state.SetItemsProcessed(state.iterations());

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Anchor_Raw_SelectByPK_1K);

    // ========================================================================
    // Anchor_Raw_BatchInsert_1000 — multi-row INSERT, 1000 rows / iteration
    // ========================================================================
    auto BM_Anchor_Raw_BatchInsert_1000(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        // Single multi-VALUES INSERT with 1000 rows × 3 columns = 3000 placeholders.
        std::string sql = "INSERT INTO person(name, age, salary) VALUES";
        for (int i = 0; i < kBatchSize1000; ++i) {
            sql += (i == 0 ? "(?,?,?)" : ",(?,?,?)");
        }
        sqlite3_stmt* ins = prepare(db, sql.c_str());

        int batch = 0;
        for (auto _ : state) {
            for (int i = 0; i < kBatchSize1000; ++i) {
                std::string name = "B" + std::to_string(batch) + "_" + std::to_string(i);
                sqlite3_bind_text(ins, (i * 3) + 1, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(ins, (i * 3) + 2, 25 + (i % 40));
                sqlite3_bind_double(ins, (i * 3) + 3, 40'000.0 + static_cast<double>(i));
            }
            if (sqlite3_step(ins) != SQLITE_DONE) {
                die(db, "batch step");
            }
            sqlite3_reset(ins);
            ++batch;
        }
        state.SetItemsProcessed(state.iterations() * kBatchSize1000);

        sqlite3_finalize(ins);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Anchor_Raw_BatchInsert_1000);

    // ========================================================================
    // Anchor_Raw_FullScan_10K — sequential SELECT over 10K rows
    // ========================================================================
    auto BM_Anchor_Raw_FullScan_10K(benchmark::State& state) -> void {
        sqlite3* db = open_memory_db();
        exec(db, kCreatePerson);
        seed_person(db, kFullScan10K);
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
            if (rows != kFullScan10K) {
                die(db, "full scan row count mismatch");
            }
            sqlite3_reset(sel);
        }
        state.SetItemsProcessed(state.iterations() * kFullScan10K);

        sqlite3_finalize(sel);
        sqlite3_close(db);
    }
    BENCHMARK(BM_Anchor_Raw_FullScan_10K);

} // namespace

BENCHMARK_MAIN();
