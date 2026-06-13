# Fair INSERT Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Storm's INSERT benchmark measure the same workload as the raw SQLite anchor so `% of raw` reflects real ORM overhead, not DELETE-per-iteration noise.

**Architecture:** Introduce a `BenchPerson` model (no UNIQUE, no indexes) that mirrors the raw anchor schema exactly. Switch INSERT tests to use it. Replace `clear_table()` in the hot path with a per-instance iteration counter that generates unique names. Add a symmetric `RETURNING id` raw anchor for the `insert` variant; rename the existing plain anchor to match `insert_no_return`.

**Tech Stack:** C++26 modules, Google Benchmark, SQLite3, storm ORM reflection, JSON benchmark test corpus.

**GitHub Issue:** #349

---

## File Map

| File | Change |
|---|---|
| `benchmarks/models.hpp` | Add `BenchPerson` struct |
| `benchmarks/tests/benchmark_tests.json` | Switch `insert`, `insert_no_return`, `insert_edge` to `"model": "BenchPerson"` |
| `benchmarks/registry.cppm` | Add `BenchPerson` re-export + `with_base_model` branch |
| `benchmarks/register.cpp` | Create `BenchPerson` table in `initialize_db()` |
| `benchmarks/crud_benchmark.cppm` | Add `BenchPerson` `create_model`; add `counter_` member; drop `clear_table()` from INSERT hot path |
| `benchmarks/anchors_raw.cpp` | Rename existing anchor → `insert_no_return`; add new `RETURNING id` anchor for `insert` |

---

### Task 1: Add `BenchPerson` model

**Files:**
- Modify: `benchmarks/models.hpp`

`BenchPerson` has exactly 4 fields matching the raw anchor schema (`kCreatePerson`): `id`, `name`, `age`, `salary`. No `[[= FieldAttr::unique]]`, no `storm::Indexes<>` specialisation.

- [ ] **Step 1: Add the struct to `benchmarks/models.hpp`**

Add after the closing brace of `FKMessage` and before the closing `}` of `storm::benchmark`:

```cpp
struct BenchPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
    double                                    salary;
};
```

- [ ] **Step 2: Verify the file compiles (release build, module scan only)**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release --target storm_benchmark_registry 2>&1 | tail -20
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add benchmarks/models.hpp
git commit -m "feat(bench): add BenchPerson model — no UNIQUE, matches raw anchor schema"
```

---

### Task 2: Switch INSERT tests to `BenchPerson` in JSON

**Files:**
- Modify: `benchmarks/tests/benchmark_tests.json`

Three test entries currently use `"model": "Person"` for INSERT: `insert`, `insert_no_return`, `insert_edge`. Change all three to `"model": "BenchPerson"`.

- [ ] **Step 1: Edit `benchmarks/tests/benchmark_tests.json`**

Find and update these three objects (change only the `"model"` field):

```json
{
    "test_name": "insert",
    "test_category": "INSERT",
    "operation": "insert",
    "size_profile": "batch_standard",
    "description": "INSERT operations with various batch sizes",
    "model": "BenchPerson"
},
{
    "test_name": "insert_no_return",
    "test_category": "INSERT",
    "operation": "insert_no_return",
    "size_profile": "batch_standard",
    "description": "INSERT without RETURNING clause (no ID retrieval)",
    "model": "BenchPerson"
},
{
    "test_name": "insert_edge",
    "test_category": "INSERT_EDGE",
    "operation": "insert",
    "size_profile": "batch_insert_edge",
    "description": "INSERT at SQLite chunk boundary (999/4 fields = 249)",
    "model": "BenchPerson"
}
```

- [ ] **Step 2: Commit**

```bash
git add benchmarks/tests/benchmark_tests.json
git commit -m "feat(bench): switch insert/insert_no_return/insert_edge to BenchPerson model"
```

---

### Task 3: Wire `BenchPerson` into the registry

**Files:**
- Modify: `benchmarks/registry.cppm`

`with_base_model` currently dispatches `"FKMessage"` → `FKMessage`, `"User"` → `User`, everything else → `Person`. Add a `"BenchPerson"` branch before the fallthrough.

- [ ] **Step 1: Re-export `BenchPerson` and add dispatch branch in `benchmarks/registry.cppm`**

In the `export namespace storm::benchmark` block, add:
```cpp
using ::storm::benchmark::BenchPerson;
```

In `with_base_model`, add before the `else` fallthrough:
```cpp
} else if constexpr (test.model == "BenchPerson") {
    return fn.template operator()<BenchPerson>();
```

Full updated function:
```cpp
template <auto const& test, typename F> auto with_base_model(F fn) {
    if constexpr (test.model == "FKMessage") {
        return fn.template operator()<FKMessage>();
    } else if constexpr (test.model == "User") {
        return fn.template operator()<User>();
    } else if constexpr (test.model == "BenchPerson") {
        return fn.template operator()<BenchPerson>();
    } else {
        return fn.template operator()<Person>();
    }
}
```

- [ ] **Step 2: Verify registry compiles**

```bash
cmake --build --preset ninja-release --target storm_benchmark_registry 2>&1 | tail -20
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add benchmarks/registry.cppm
git commit -m "feat(bench): add BenchPerson to benchmark model registry dispatch"
```

---

### Task 4: Create `BenchPerson` table in `initialize_db()`

**Files:**
- Modify: `benchmarks/register.cpp`

`initialize_db()` currently creates tables for `Person`, `User`, `FKMessage`. Add `BenchPerson`.

- [ ] **Step 1: Add `SchemaStatement<BenchPerson>` in `benchmarks/register.cpp`**

After the `Person` table creation block, add:
```cpp
if (!storm::orm::schema::SchemaStatement<BenchPerson>::create_table_if_not_exists(conn).has_value()) {
    return false;
}
```

Full updated `initialize_db()`:
```cpp
auto initialize_db() -> bool {
    auto open = QuerySet<Person>::set_default_connection(":memory:");
    if (!open.has_value()) {
        return false;
    }
    const auto& conn = QuerySet<Person>::get_default_connection();
    if (!storm::orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn).has_value()) {
        return false;
    }
    if (!storm::orm::schema::SchemaStatement<BenchPerson>::create_table_if_not_exists(conn).has_value()) {
        return false;
    }
    if (!storm::orm::schema::SchemaStatement<User>::create_table_if_not_exists(conn).has_value()) {
        return false;
    }
    if (!storm::orm::schema::SchemaStatement<FKMessage>::create_table_if_not_exists(conn).has_value()) {
        return false;
    }
    return true;
}
```

Note: `BenchPerson` shares the same connection as `Person` (`:memory:`). That is fine — both tables live in the same in-memory database.

- [ ] **Step 2: Verify register.cpp compiles**

```bash
cmake --build --preset ninja-release 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add benchmarks/register.cpp
git commit -m "feat(bench): create BenchPerson table in initialize_db()"
```

---

### Task 5: Fix `CrudBenchmark` — counter-based names, drop `clear_table()` from INSERT

**Files:**
- Modify: `benchmarks/crud_benchmark.cppm`

Two changes:
1. Add a `counter_` member to `CrudBenchmark` incremented each `run_once()` for INSERT ops, used to generate unique names.
2. Remove `clear_table()` calls from the INSERT branches in `run_once()`.
3. Add a `create_model` specialisation for `BenchPerson` that uses the counter.

- [ ] **Step 1: Add `counter_` member and update `run_once()` in `benchmarks/crud_benchmark.cppm`**

Add `counter_` after the `using Base = ...` line:
```cpp
int counter_ = 0;
```

Replace the `is_insert_no_return_op` and `is_insert_op` branches in `run_once()`:

```cpp
auto run_once() -> void {
    if constexpr (is_insert_no_return_op()) {
        if (Base::batch_size() == 1) {
            Base::data()[0].name = std::format("P{}", counter_++);
            (void)Base::qs().template insert<storm::orm::statements::ReturnId::No>(Base::data()[0]).execute();
        } else {
            for (auto& obj : Base::data()) {
                obj.name = std::format("P{}", counter_++);
            }
            (void)Base::qs().template insert<storm::orm::statements::ReturnId::No>(Base::data()).execute();
        }
    } else if constexpr (is_insert_op()) {
        if (Base::batch_size() == 1) {
            Base::data()[0].name = std::format("P{}", counter_++);
            (void)Base::qs().insert(Base::data()[0]).execute();
        } else {
            for (auto& obj : Base::data()) {
                obj.name = std::format("P{}", counter_++);
            }
            (void)Base::qs().insert(Base::data()).execute();
        }
    } else if constexpr (is_update_op()) {
        // ... unchanged
    } else if constexpr (is_delete_op()) {
        // ... unchanged
    }
}
```

- [ ] **Step 2: Add `create_model` for `BenchPerson`**

In the `create_model` static method, add a branch:
```cpp
} else if constexpr (std::is_same_v<Model, BenchPerson>) {
    return Model{
            .id     = 0,
            .name   = std::format("P{}", index),
            .age    = 20 + (index % 50),
            .salary = 30000.0 + (index * 1000.0),
    };
}
```

- [ ] **Step 3: Remove `clear_table()` — it is now dead for INSERT ops. Keep it for DELETE (reinsert path still uses it indirectly via `prepare_with_insert`). Verify no remaining calls from INSERT branches.**

```bash
grep -n "clear_table" benchmarks/crud_benchmark.cppm
```

Expected: only the function definition + call inside `reinsert_for_delete()` remain (or zero if DELETE doesn't use it either — but DELETE does call `reinsert_for_delete` which calls `prepare_with_insert` which does the raw DELETE internally, so `clear_table()` can be removed entirely from the class if its only callers were the INSERT branches).

Check remaining callers:
```bash
grep -n "clear_table\|reinsert_for_delete" benchmarks/crud_benchmark.cppm
```

If `clear_table()` has no remaining callers after removing from INSERT, delete the function body too.

- [ ] **Step 4: Full release build**

```bash
cmake --build --preset ninja-release 2>&1 | grep -E "error:" | head -20
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add benchmarks/crud_benchmark.cppm
git commit -m "fix(bench): drop clear_table() from INSERT hot path, use counter-based unique names"
```

---

### Task 6: Fix raw anchors — add RETURNING variant, rename plain to insert_no_return

**Files:**
- Modify: `benchmarks/anchors_raw.cpp`

Current state: one anchor `BM_Raw_Insert_Single` registered as `"Storm/INSERT/insert"` with plain INSERT, counter-based names.

Target state:
- `BM_Raw_Insert_No_Return` → `Name("Storm/INSERT/insert_no_return")`, plain INSERT, counter-based names
- `BM_Raw_Insert_Returning` → `Name("Storm/INSERT/insert")`, `INSERT … RETURNING id`, counter-based names

Both use `kCreatePerson` schema (already no UNIQUE — matches `BenchPerson`).

- [ ] **Step 1: Replace the single anchor with two in `benchmarks/anchors_raw.cpp`**

Remove the existing `BM_Raw_Insert_Single` function and its `BENCHMARK(...)` line. Replace with:

```cpp
// Storm/INSERT/insert_no_return/N:1 — plain INSERT, no RETURNING
auto BM_Raw_Insert_No_Return(benchmark::State& state) -> void {
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
            die(db, "insert_no_return step");
        }
        sqlite3_reset(ins);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetComplexityN(state.range(0));

    sqlite3_finalize(ins);
    sqlite3_close(db);
}
BENCHMARK(BM_Raw_Insert_No_Return)->Name("Storm/INSERT/insert_no_return")->Arg(1)->ArgName("N");

// Storm/INSERT/insert/N:1 — INSERT with RETURNING id (mirrors Storm's insert() path)
auto BM_Raw_Insert_Returning(benchmark::State& state) -> void {
    sqlite3* db = open_memory_db();
    exec(db, kCreatePerson);
    sqlite3_stmt* ins = prepare(db, "INSERT INTO person(name, age, salary) VALUES(?,?,?) RETURNING id");

    int counter = 0;
    for (auto _ : state) {
        const std::string name = std::format("P{}", counter++);
        sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 2, 30);
        sqlite3_bind_double(ins, 3, 50'000.0);
        if (sqlite3_step(ins) != SQLITE_ROW) {
            die(db, "insert returning step");
        }
        benchmark::DoNotOptimize(sqlite3_column_int(ins, 0)); // consume the returned id
        if (sqlite3_step(ins) != SQLITE_DONE) {
            die(db, "insert returning done");
        }
        sqlite3_reset(ins);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetComplexityN(state.range(0));

    sqlite3_finalize(ins);
    sqlite3_close(db);
}
BENCHMARK(BM_Raw_Insert_Returning)->Name("Storm/INSERT/insert")->Arg(1)->ArgName("N");
```

Note: `RETURNING id` changes the step sequence — the first `sqlite3_step` returns `SQLITE_ROW` (the returned id row), a second `sqlite3_step` returns `SQLITE_DONE`. Use `DoNotOptimize` on the id to prevent the compiler eliding the read.

- [ ] **Step 2: Update the file-top comment to reflect both anchors**

Find the comment block near line 17 that lists `Storm/INSERT/insert/N:1` and update to:
```
 *   - Storm/INSERT/insert/N:1          (INSERT … RETURNING id)
 *   - Storm/INSERT/insert_no_return/N:1 (plain INSERT)
```

- [ ] **Step 3: Full release build**

```bash
cmake --build --preset ninja-release 2>&1 | grep -E "error:" | head -20
```

Expected: no errors.

- [ ] **Step 4: Run anchors binary to verify both benchmarks appear and complete**

```bash
./build/release/benchmarks/storm_anchors --benchmark_filter="Storm/INSERT"
```

Expected output contains two rows:
```
Storm/INSERT/insert/N:1          ...
Storm/INSERT/insert_no_return/N:1 ...
```

- [ ] **Step 5: Commit**

```bash
git add benchmarks/anchors_raw.cpp
git commit -m "fix(bench): add RETURNING anchor for insert, rename plain anchor to insert_no_return"
```

---

### Task 7: End-to-end verification

- [ ] **Step 1: Full release build (clean)**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
```

Expected: no errors.

- [ ] **Step 2: Run Storm INSERT benchmarks**

```bash
./build/release/benchmarks/storm_bench --benchmark_filter="Storm/INSERT/insert/N" --benchmark_repetitions=3
```

Record the `items_per_second` value.

- [ ] **Step 3: Run raw INSERT anchors**

```bash
./build/release/benchmarks/storm_anchors --benchmark_filter="Storm/INSERT/insert/N"
```

Record the `items_per_second` value.

- [ ] **Step 4: Verify efficiency ≥ 95%**

Divide Storm ips / raw ips. Expected: ≥ 0.95 (95%). If lower, investigate remaining asymmetry before proceeding.

- [ ] **Step 5: Run debug tests (no regressions)**

```bash
cmake --preset ninja-debug && cmake --build --preset ninja-debug && ctest --preset ninja-debug
```

Expected: all tests pass.

- [ ] **Step 6: Create PR**

```bash
gh pr create --base develop --title "fix(bench): fair INSERT benchmark — BenchPerson, accumulate rows, symmetric RETURNING" --body "$(cat <<'EOF'
Closes #349

## Changes
- `BenchPerson` model added to `benchmarks/models.hpp` — no UNIQUE, no indexes, matches raw anchor schema
- INSERT / insert_no_return / insert_edge tests switched to `BenchPerson` in JSON corpus
- Registry + initialize_db wired for `BenchPerson`
- `CrudBenchmark`: counter-based unique names per iteration, `clear_table()` removed from INSERT hot path — rows now accumulate like the raw anchor
- Raw anchors: plain INSERT renamed to `insert_no_return`; new `RETURNING id` anchor added for `insert`

Both pairs (insert/insert_no_return) now measure identical workloads on Storm and raw sides.
EOF
)"
```

- [ ] **Step 7: Wait 30s then check SonarCloud**

```bash
sleep 30
```

Then run `/sonarcloud-status` skill.

---

## Self-Review

**Spec coverage:**
- ✅ `BenchPerson` model — Task 1
- ✅ JSON model switch — Task 2
- ✅ Registry dispatch — Task 3
- ✅ `initialize_db` — Task 4
- ✅ `create_model` for `BenchPerson` + counter + no `clear_table()` — Task 5
- ✅ Two symmetric raw anchors — Task 6
- ✅ End-to-end ≥ 95% verification — Task 7

**Placeholder scan:** None found.

**Type consistency:** `BenchPerson` defined in Task 1, used consistently in Tasks 2–5. `counter_` is `int`, `std::format("P{}", counter_++)` produces unique strings matching the raw anchor's `std::format("P{}", counter++)`.
