#pragma once

// Storm/UPDATE_PK and Storm/DELETE_PK raw anchors (#552 tier (a)) — split out
// of anchors_raw.cpp to keep that file under the project's per-file line
// budget. NOT a standalone compilation unit: textually included from inside
// anchors_raw.cpp's unnamed namespace, AFTER anchors_raw_insert.hpp (needs
// ChunkPlan/run_chunked_pass/kCreatePerson/seed_person/run_person_seed/
// kInsertPersonFull/kSelectPersonCols/drain_person_select/step_done_or_die,
// all declared earlier in anchors_raw.cpp or in anchors_raw_insert.hpp) — do
// not #include this file from anywhere else.
//
// Both anchors mirror storm::benchmark::CrudBenchmark<Person, ...>
// (crud_benchmark.cppm), which runs on the FULL 10-field Person model (same
// kCreatePerson schema the SELECT/WHERE anchors use), swept over
// BATCH_STANDARD like INSERT.
//
// Row shape mirrors CrudBenchmark::create_model for Person (1-based i =
// index + 1): name "Person{i}", age 20+(i%50), salary 30000+i*1000,
// is_active (i%2==0). Unlike seed_person (used by SELECT/WHERE),
// CrudBenchmark::create_model never touches years_experience/department/
// score/nickname/avatar, so those stay at Person's default-constructed
// values — 0 / "" / NULL / NULL / NULL — for every row, not seed_person's
// one-third-non-null score. A dedicated binder keeps that distinction
// explicit rather than silently reusing seed_person's row shape.

constexpr int kPersonNonPkCols = 9; // name,age,salary,is_active,years_experience,department,score,nickname,avatar

// Bind one CrudBenchmark::create_model(index) row (1-based `i` = index+1)
// into a prepared statement at 1-based placeholders `base+1..base+9`, in
// Person's non-PK declaration order — the exact column order both the
// full-Person INSERT (kInsertPersonFull) and the UPDATE SET clause use.
auto bind_person_crud_row(sqlite3_stmt* stmt, int base, int i) -> void {
    const std::string name = std::format("Person{}", i);
    sqlite3_bind_text(stmt, base + 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, base + 2, 20 + (i % 50));
    sqlite3_bind_double(stmt, base + 3, 30'000.0 + (static_cast<double>(i) * 1000.0));
    sqlite3_bind_int(stmt, base + 4, (i % 2 == 0) ? 1 : 0);
    sqlite3_bind_int(stmt, base + 5, 0);                      // years_experience (default)
    sqlite3_bind_text(stmt, base + 6, "", -1, SQLITE_STATIC); // department (default "")
    sqlite3_bind_null(stmt, base + 7);                        // score (default nullopt)
    sqlite3_bind_null(stmt, base + 8);                        // nickname (default nullopt)
    sqlite3_bind_null(stmt, base + 9);                        // avatar (default empty)
}

// One-shot seed for UPDATE_PK: `rows` CrudBenchmark rows via kInsertPersonFull,
// wrapped in a single transaction (setup, outside any timed loop) — mirrors
// Base::prepare_with_insert's seed step. IDs are 1..rows by construction: a
// fresh table, plain `id INTEGER PRIMARY KEY`, no AUTOINCREMENT — rowids
// assign 1..rows in insertion order, so no SELECT-back is needed here.
auto seed_person_crud(sqlite3* db, int rows) -> void {
    run_person_seed(db, rows, "seed_person_crud step", [](sqlite3_stmt* ins, int i) {
        bind_person_crud_row(ins, /*base=*/0, i);
    });
}

// Storm's schema generator creates FOUR index b-trees on `person` beyond the
// implicit sqlite_autoindex from the inline "name TEXT ... UNIQUE" column
// constraint (kCreatePerson, shared with the SELECT/WHERE anchors above):
// `[[= storm::unique]] name`, `[[= storm::indexed]] department`, and the
// `storm_indexes` typedef's `Index<department, age>` / `UniqueIndex<name,
// department>` (shared/models.h; emitted by SchemaStatement::
// create_table_if_not_exists -> create_indexes_if_not_exist, schema.cppm).
// The SELECT/WHERE anchors don't need these — none of their predicates
// (age/salary/is_active) leads any of these indexes, so both sides full-scan
// regardless, and seeding is untimed. UPDATE_PK and DELETE_PK are different:
// they're WRITE-timed, and every INSERT/UPDATE/DELETE on an indexed table
// pays index-maintenance cost. Omitting these four indexes would have Storm
// maintaining 5 b-trees per row against the raw anchor's 1, understating
// Storm's efficiency — the same class of bug as the AUTOINCREMENT schema
// divergence CLAUDE.md's "Same SCHEMA" rule exists to prevent, just pointed
// the other way. Scoped to these two anchors (not added to kCreatePerson
// itself) so the published SELECT/WHERE baseline numbers don't move.
auto create_person_write_indexes(sqlite3* db) -> void {
    exec(db, "CREATE UNIQUE INDEX idx_person_name ON person(name)");
    exec(db, "CREATE INDEX idx_person_department ON person(department)");
    exec(db, "CREATE INDEX idx_person_department_age ON person(department, age)");
    exec(db, "CREATE UNIQUE INDEX idx_person_name_department ON person(name, department)");
}

// --- UPDATE_PK -------------------------------------------------------

// "UPDATE person SET name=?,age=?,salary=?,is_active=?,years_experience=?,
// department=?,score=?,nickname=?,avatar=? WHERE id=?" — the SQL
// UpdateGrammar<Person> generates (update_grammar.cppm build_update_sql_array):
// every non-PK member in declaration order, PK last. 10 placeholders: 9 SET
// values + 1 WHERE id.
constexpr auto kUpdatePersonByIdSql =
        "UPDATE person SET name=?, age=?, salary=?, is_active=?, years_experience=?, department=?, score=?, "
        "nickname=?, avatar=? WHERE id=?";

// One target row for the UPDATE_PK anchor: the mutated values CrudBenchmark's
// prepare() writes into Base::data()[idx] before any iteration runs (name/
// age/salary/is_active only — the rest stay at their seeded defaults, see
// bind_person_crud_row). Computed ONCE outside the timed loop, exactly as
// Storm rebinds the SAME in-memory object on every iteration (no
// per-iteration recomputation on either side).
struct UpdateTargetRow {
    int         id;
    std::string name;
    int         age;
    double      salary;
    bool        is_active;
};

// Build the n update targets for run_update_pk_benchmark. `idx` is 0-based
// (matches CrudBenchmark::prepare's `for (size_t i = 0; i < data().size(); ++i)`);
// `i = idx + 1` is the same 1-based row identity seed_person_crud used, so
// target ids line up with the seeded rows 1..n.
auto build_update_targets(int n) -> std::vector<UpdateTargetRow> {
    std::vector<UpdateTargetRow> targets;
    targets.reserve(n);
    for (int idx = 0; idx < n; ++idx) {
        int const    i          = idx + 1;
        int const    seeded_age = 20 + (i % 50);
        double const seeded_sal = 30'000.0 + (static_cast<double>(i) * 1000.0);
        bool const   seeded_act = (i % 2 == 0);
        targets.push_back(
                UpdateTargetRow{
                        .id        = i,
                        .name      = std::format("Updated{}", idx),
                        .age       = seeded_age + 5,
                        .salary    = seeded_sal * 1.1,
                        .is_active = !seeded_act,
                }
        );
    }
    return targets;
}

// Storm/UPDATE_PK/update_pk/N:<n> — UPDATE Person rows by primary key.
// n==1: single reset+bind+execute, no transaction (UpdateStatement::
// execute_single_row). n>1: one prepared statement reused across the whole
// batch, wrapped in a single BEGIN/COMMIT (UpdateStatement::execute(span)).
auto BM_Raw_Update_Pk(benchmark::State& state) -> void {
    int const n  = static_cast<int>(state.range(0));
    sqlite3*  db = open_memory_db();
    exec(db, kCreatePerson);
    create_person_write_indexes(db);
    seed_person_crud(db, n);

    const std::vector<UpdateTargetRow> targets = build_update_targets(n);
    sqlite3_stmt*                      upd     = prepare(db, kUpdatePersonByIdSql);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) -- Google Benchmark loop idiom: `_` is the iteration token, never read
    for (auto _ : state) {
        if (n > 1) {
            exec(db, "BEGIN");
        }
        for (const auto& row : targets) {
            sqlite3_reset(upd);
            sqlite3_bind_text(upd, 1, row.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(upd, 2, row.age);
            sqlite3_bind_double(upd, 3, row.salary);
            sqlite3_bind_int(upd, 4, row.is_active ? 1 : 0);
            sqlite3_bind_int(upd, 5, 0);
            sqlite3_bind_text(upd, 6, "", -1, SQLITE_STATIC);
            sqlite3_bind_null(upd, 7);
            sqlite3_bind_null(upd, 8);
            sqlite3_bind_null(upd, 9);
            sqlite3_bind_int(upd, 10, row.id);
            step_done_or_die(db, upd, "update_pk step");
        }
        if (n > 1) {
            exec(db, "COMMIT");
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n);
    state.SetComplexityN(state.range(0));

    sqlite3_finalize(upd);
    sqlite3_close(db);
}

// BATCH_UPDATE_EDGE from sizes.cppm: {198, 199, 200}. benchmark_tests.yaml
// labels this "UPDATE at SQLite chunk boundary (999/5 fields = 199)", but
// UpdateStatement::execute(span) (update.cppm) has NO chunk arithmetic — it
// binds and steps one row at a time on a single prepared statement inside one
// transaction, with no per-statement variable-count limit to hit. Verified:
// no "chunk" token anywhere in update.cppm/update_grammar.cppm. So this sweep
// exercises no boundary BM_Raw_Update_Pk needs to special-case; it's swept
// here only so Storm/UPDATE_PK_EDGE/update_pk_edge has a raw counterpart at
// all, reusing BM_Raw_Update_Pk verbatim under a second registered name.
constexpr std::array kBatchUpdateEdge = {198, 199, 200};

// --- DELETE_PK ---------------------------------------------------------
//
// Storm's DELETE_PK benchmark (CrudBenchmark::run_once, is_delete_op branch)
// re-seeds the table on EVERY iteration via reinsert_for_delete() —
// Base::prepare_with_insert(n): clear table, bulk-INSERT n rows (no
// RETURNING), SELECT the whole table back to recover the auto-assigned ids
// — BEFORE erasing. That reinsert runs INSIDE the timed loop
// (bench_register.h: run() is invoked once per `for (auto _ : state)` step,
// no PauseTiming), so what "Storm/DELETE_PK/delete_pk" actually measures is
// clear+reinsert+reselect+delete, not a bare DELETE. The raw anchor
// replicates all four steps — anchoring only the DELETE would read as a
// nonsensical (wildly >100%-of-raw) ratio against what that name measures.
//
// Deliberately NOT mirrored, both negligible next to n SQLite statements:
// Base::prepare(n) (base.cppm) default-constructs and destroys n Person
// objects via std::vector::clear()+push_back() before the reinsert, and
// Base::prepare_with_insert's id write-back walks the n selected rows
// copying `row.id` into `data()[i].id`. Both bias toward the raw anchor
// doing slightly LESS work than Storm, same direction as (and dwarfed by)
// the index-maintenance cost below.

// Bulk-INSERT chunk size for the reinsert step: MAX_DB_VARIABLES / field_count_
// with field_count_ = 10 for Person (ALL non-relation members, INCLUDING the
// primary key — insert.cppm's chunk divisor counts it even though a
// DB-generated PK is never actually bound). 999 / 10 = 99. Distinct from the
// BenchPerson INSERT anchors' 249 (999/4) in anchors_raw_insert.hpp — different
// model, different field_count_.
constexpr int kCrudInsertChunkRows = 999 / 10;

// "INSERT INTO person(name,...,avatar) VALUES (?,...,?),(?,...,?),..." for
// `rows` 9-column tuples, no RETURNING — mirrors the bulk `insert(vector)`
// VOID path prepare_with_insert always takes (insert.cppm's non-templated
// `query(span, opts)` overload), regardless of row count.
auto build_bulk_insert_person_sql(int rows) -> std::string {
    std::string sql = "INSERT INTO person(name, age, salary, is_active, years_experience, department, score, "
                      "nickname, avatar) VALUES ";
    for (int i = 0; i < rows; ++i) {
        sql += (i == 0) ? "(?,?,?,?,?,?,?,?,?)" : ",(?,?,?,?,?,?,?,?,?)";
    }
    return sql;
}

// Chunk plan for the reinsert step: full_stmt handles kCrudInsertChunkRows-row
// chunks, rem_stmt (if any) the remainder tail — same ChunkPlan shape as
// InsertPlan (anchors_raw_insert.hpp) but 9 columns wide and never RETURNING.
// Built once per benchmark instance (fixed n → fixed chunk shape), reused
// every iteration exactly as Storm's prepare_cached would.
using ReinsertPlan = ChunkPlan;

auto make_reinsert_plan(sqlite3* db, int n) -> ReinsertPlan {
    int const  full_chunks    = n / kCrudInsertChunkRows;
    int const  remainder_rows = n % kCrudInsertChunkRows;
    bool const chunked        = (full_chunks + (remainder_rows > 0 ? 1 : 0)) > 1;
    return ReinsertPlan{
            .full_stmt =
                    full_chunks > 0 ? prepare(db, build_bulk_insert_person_sql(kCrudInsertChunkRows).c_str()) : nullptr,
            .rem_stmt =
                    remainder_rows > 0 ? prepare(db, build_bulk_insert_person_sql(remainder_rows).c_str()) : nullptr,
            .full_chunks    = full_chunks,
            .remainder_rows = remainder_rows,
            .chunked        = chunked,
    };
}

// Run one reinsert via the shared run_chunked_pass driver: bind + step every
// chunk of `plan`, row identities 1..n (fresh table, plain INTEGER PRIMARY
// KEY → rowids assign 1..n in insertion order — see seed_person_crud).
auto run_reinsert(sqlite3* db, ReinsertPlan const& plan) -> void {
    int i = 1;
    run_chunked_pass(
            db,
            plan,
            [&](sqlite3_stmt* stmt) {
                for (int r = 0; r < kCrudInsertChunkRows; ++r) {
                    bind_person_crud_row(stmt, r * kPersonNonPkCols, i);
                    ++i;
                }
                step_done_or_die(db, stmt, "reinsert step");
            },
            [&](sqlite3_stmt* stmt) {
                for (int r = 0; r < plan.remainder_rows; ++r) {
                    bind_person_crud_row(stmt, r * kPersonNonPkCols, i);
                    ++i;
                }
                step_done_or_die(db, stmt, "reinsert step");
            }
    );
}

// Rows per bulk-DELETE IN-clause chunk for a single-column PK:
// EraseGrammar<Person>::MAX_CHUNK_ROWS = (999 * 4 / 5) / 1 = 799
// (erase_grammar.cppm — "single-PK models keep the historical 799").
constexpr int kDeleteChunkRows = (999 * 4 / 5) / 1;

auto build_delete_in_sql(int rows) -> std::string {
    std::string sql = "DELETE FROM person WHERE id IN (";
    for (int i = 0; i < rows; ++i) {
        sql += (i == 0) ? "?" : ",?";
    }
    sql += ")";
    return sql;
}

// Delete-by-PK plan, mirroring EraseStatement::execute(span)'s three-way
// dispatch (erase.cppm): n==1 uses `single_stmt` ("id = ?", no IN-list, no
// transaction) and leaves the inherited ChunkPlan empty; 2..799 uses
// `full_stmt` as a single IN-clause statement (no transaction, chunked=false,
// full_chunks=1); 800+ chunks `full_stmt` (kDeleteChunkRows-row chunks) plus
// a `rem_stmt` remainder tail, wrapped in one BEGIN/COMMIT.
struct DeletePlan : ChunkPlan {
    sqlite3_stmt* single_stmt = nullptr;
};

auto make_delete_plan(sqlite3* db, int n) -> DeletePlan {
    if (n == 1) {
        return DeletePlan{
                ChunkPlan{
                        .full_stmt      = nullptr,
                        .rem_stmt       = nullptr,
                        .full_chunks    = 0,
                        .remainder_rows = 0,
                        .chunked        = false,
                },
                prepare(db, "DELETE FROM person WHERE id=?"),
        };
    }
    if (n <= kDeleteChunkRows) {
        return DeletePlan{
                ChunkPlan{
                        .full_stmt      = prepare(db, build_delete_in_sql(n).c_str()),
                        .rem_stmt       = nullptr,
                        .full_chunks    = 1,
                        .remainder_rows = 0,
                        .chunked        = false,
                },
                nullptr,
        };
    }
    int const full_chunks    = n / kDeleteChunkRows;
    int const remainder_rows = n % kDeleteChunkRows;
    return DeletePlan{
            ChunkPlan{
                    .full_stmt = prepare(db, build_delete_in_sql(kDeleteChunkRows).c_str()),
                    .rem_stmt = remainder_rows > 0 ? prepare(db, build_delete_in_sql(remainder_rows).c_str()) : nullptr,
                    .full_chunks    = full_chunks,
                    .remainder_rows = remainder_rows,
                    .chunked        = true,
            },
            nullptr,
    };
}

// Run one DELETE-by-PK pass over ids 1..n against `plan`.
auto run_delete_by_pk(sqlite3* db, DeletePlan const& plan, int n) -> void {
    if (plan.single_stmt != nullptr) {
        sqlite3_reset(plan.single_stmt);
        sqlite3_bind_int(plan.single_stmt, 1, 1);
        step_done_or_die(db, plan.single_stmt, "delete_pk single step");
        return;
    }
    int id = 1;
    run_chunked_pass(
            db,
            plan,
            [&](sqlite3_stmt* stmt) {
                // `id <= n` guard: in the 2..kDeleteChunkRows plan (make_delete_plan's
                // middle branch) `full_stmt` is bound for exactly `n` placeholders with
                // full_chunks==1, narrower than kDeleteChunkRows — this bounds the loop
                // to that statement's real width instead of overrunning it. The 800+
                // chunked branch never needs the guard (n is always a multiple of
                // kDeleteChunkRows here plus a separate remainder statement), but the
                // shared lambda has no other way to know its statement's width.
                for (int p = 1; p <= kDeleteChunkRows && id <= n; ++p, ++id) {
                    sqlite3_bind_int(stmt, p, id);
                }
                step_done_or_die(db, stmt, "delete_pk step");
            },
            [&](sqlite3_stmt* stmt) {
                for (int p = 1; p <= plan.remainder_rows; ++p, ++id) {
                    sqlite3_bind_int(stmt, p, id);
                }
                step_done_or_die(db, stmt, "delete_pk step");
            }
    );
}

// Storm/DELETE_PK/delete_pk/N:<n> — clear + reinsert + reselect + delete,
// all inside the timed loop (see the DELETE_PK header comment above). The
// table is pre-seeded once before the loop starts too, mirroring
// Trampoline::setup() calling CrudBenchmark::prepare() (which itself calls
// prepare_with_insert) once before any timed iteration runs.
auto BM_Raw_Delete_Pk(benchmark::State& state) -> void {
    int const n  = static_cast<int>(state.range(0));
    sqlite3*  db = open_memory_db();
    exec(db, kCreatePerson);
    create_person_write_indexes(db);

    const ReinsertPlan reinsert_plan = make_reinsert_plan(db, n);
    const DeletePlan   delete_plan   = make_delete_plan(db, n);
    sqlite3_stmt*      sel           = prepare(db, kSelectPersonCols);

    run_reinsert(db, reinsert_plan); // pre-loop seed, mirrors setup()

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) -- Google Benchmark loop idiom: `_` is the iteration token, never read
    for (auto _ : state) {
        exec(db, "DELETE FROM person");
        run_reinsert(db, reinsert_plan);
        int const rows = drain_person_select(sel);
        if (rows != n) {
            die(db, "delete_pk reinsert row count mismatch");
        }
        run_delete_by_pk(db, delete_plan, n);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n);
    state.SetComplexityN(state.range(0));

    // sqlite3_finalize(nullptr) is a documented no-op — no null guard needed
    // for whichever of these each plan shape left unset.
    sqlite3_finalize(sel);
    sqlite3_finalize(reinsert_plan.full_stmt);
    sqlite3_finalize(reinsert_plan.rem_stmt);
    sqlite3_finalize(delete_plan.single_stmt);
    sqlite3_finalize(delete_plan.full_stmt);
    sqlite3_finalize(delete_plan.rem_stmt);
    sqlite3_close(db);
}
