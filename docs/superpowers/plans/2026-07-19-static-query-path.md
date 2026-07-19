# Static Query Path (#462) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compile-time (consteval) SQL strings for statically-shaped SELECT queries plus slot-indexed prepared-statement lookup, opt-in via `storm::static_query<Model>()` — hard-gated on a spike showing ≥2% steady-state win.

**Architecture:** A typed expression layer (`sf<^^M::field>()` → `StaticCmp<M, Op, V>` / `StaticAnd<L,R>` …) keeps query shape in the type and bound values at runtime; a consteval renderer walks the types into a `ConstexprString` full SQL per backend; each static query type gets a process-wide slot index into a per-Connection vector of owned statements. Existing `QuerySet` is untouched and remains the dynamic path.

**Tech Stack:** C++26 reflection (clang-p2996), C++ modules (.cppm), SQLite3 + libpq, GoogleTest (TYPED_TEST), Google Benchmark.

## Global Constraints

- **Spike gate**: slot variant must be ≥2% faster than baseline `QuerySet.where().select()` steady-state (1-row workload), interleaved runs, ≥10 repetitions, cv < 5%, Release with verified `-O3`. Gate fails → close #462 with data, delete spike branch, STOP (Tasks 3+ never run).
- Work happens in the worktree `.claude/worktrees/feature-462-static-query-path/` on branch `feature/462-static-query-path` (spike on `spike/462-static-query`, never merged).
- Rule 9 (TDD): tests written and failing BEFORE implementation, per task.
- Rule 13: run `storm-code-reviewer` agent on the staged diff before EVERY code commit (docs-only commits exempt).
- Rule 7: NEVER run sanitizer presets locally — CI runs them on the PR.
- Rule 6: full benchmark re-run after implementation; revert if ANY slowdown.
- No TODO/FIXME comments (Sonar S1135). No commented-out code (S125). GTest fixture members `public:` (S3656). `std::format` over concatenation (S6185). New .cppm files need NO CMake changes (GLOB auto-discovery), same for test .cpp files.
- Module gotchas: build twice on PCM corruption; first bench build with `-j1`; no `std::format` in module purview if wchar_t mis-deduction appears; reflection-splice instantiation at namespace scope in purview can segfault BMI — keep splices inside templates (as `where.cppm` already does).
- `import std;` everywhere; `#include <meta>` textually BEFORE imports in non-module TUs.
- All commands below run from the worktree root unless stated. Commit messages end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` and the Claude-Session line used in prior commits on this branch.

---

### Task 1: Spike benchmark (Phase 0 — the gate)

**Files:**
- Create: `benchmarks/spike_static_query.cpp` (throwaway, spike branch only)
- Modify: `benchmarks/CMakeLists.txt` (add `storm_spike` target, mirroring `storm_anchors`)

**Interfaces:**
- Consumes: `storm::QuerySet`, `storm::f`, `Connection::prepare`/`prepare_cached`, `Statement::{reset,bind_int,step,handle}`, existing bench model/schema setup from `benchmarks/query_benchmark.cppm`.
- Produces: numbers posted on #462 + a go/no-go decision. No production interfaces.

- [ ] **Step 1: Create the spike branch in the worktree**

```bash
git checkout -b spike/462-static-query
```

- [ ] **Step 2: Read the two reference files**

Read `benchmarks/anchors_raw.cpp` (plain-.cpp Google Benchmark pattern, schema fairness comments) and `benchmarks/query_benchmark.cppm` (how the Storm-side fixture seeds data and which model it uses). Reuse the same model + seeding so baseline and variants share one schema created by Storm itself (AUTOINCREMENT fairness rule).

- [ ] **Step 3: Write `benchmarks/spike_static_query.cpp`**

Three benchmarks over the same Storm-created `:memory:` database, one result-size arg (`Rows`: 1 and 100). The static SQL literal is asserted byte-equal to the dynamic path's SQL at fixture setup — if they diverge the spike is unfair and must abort.

```cpp
// Spike for #462 — throwaway; never merged. Three-way comparison:
//   Baseline : QuerySet.where().select()        (runtime SQL build + string-keyed cache)
//   VariantB : static SQL literal + prepare_cached (kills SQL build, keeps hash lookup)
//   VariantA : static SQL literal + statement held in a slot vector (kills both)
#include <benchmark/benchmark.h>
#include <cassert>
import std;
import storm;

namespace {

struct SpikePerson {
    [[= storm::FieldAttr::primary]] int id{};
    std::string                         name;
    int                                 age{};
};

using QS   = storm::QuerySet<SpikePerson>;
using Conn = storm::db::sqlite::Connection;

constexpr std::string_view kStaticSql = "SELECT id, name, age FROM spike_persons WHERE age > ?";
constexpr int kTableRows = 10'000;

// One-time setup shared by all three benchmarks (static init inside a helper —
// google benchmark fixtures re-run SetUp per benchmark, we want ONE db).
auto shared_conn() -> std::shared_ptr<Conn> {
    static std::shared_ptr<Conn> conn = [] {
        QS::set_default_connection(":memory:");
        auto c = QS::get_default_connection();
        // Storm creates the schema → identical schema for every variant.
        auto created = QS{}.create_table().execute();
        assert(created.has_value());
        QS qs;
        for (int i = 0; i < kTableRows; ++i) {
            auto r = qs.insert(SpikePerson{.name = std::format("p{}", i), .age = i}).execute();
            assert(r.has_value());
        }
        // Fairness check: dynamic SQL must byte-equal the static literal.
        // to_sql() returns EXPANDED sql (values inlined), so compare the raw
        // built string instead: run one dynamic select, then fetch the cached
        // statement's sql via prepare_cached(kStaticSql) — if the dynamic path
        // produced a different string, cache stats show 2 entries, not 1.
        auto warm = qs.where(storm::f<^^SpikePerson::age>() > (kTableRows - 2)).select();
        assert(warm.has_value());
        [[maybe_unused]] auto before = c->cached_statement_count();
        [[maybe_unused]] auto probe  = c->prepare_cached(kStaticSql);
        assert(probe.has_value() && c->cached_statement_count() == before); // same string → cache hit
        return c;
    }();
    return conn;
}

// threshold such that `age > threshold` returns exactly `rows` rows
auto threshold_for(std::int64_t rows) -> int { return kTableRows - 1 - static_cast<int>(rows); }

void BM_Baseline_QuerySet(benchmark::State& state) {
    (void)shared_conn();
    QS        qs;
    const int threshold = threshold_for(state.range(0));
    for (auto _ : state) {
        auto result = qs.where(storm::f<^^SpikePerson::age>() > threshold).select();
        benchmark::DoNotOptimize(result);
        assert(result.has_value());
    }
}

// Common execute body for the two static variants: reset → bind → step → extract.
template <typename StmtPtr>
auto run_static(StmtPtr* stmt, int threshold) -> plf::hive<SpikePerson> {
    plf::hive<SpikePerson> out;
    (void)stmt->reset();
    (void)stmt->bind_int(1, threshold);
    sqlite3_stmt* raw = stmt->handle();
    while (sqlite3_step(raw) == SQLITE_ROW) {
        SpikePerson p;
        p.id   = sqlite3_column_int(raw, 0);
        p.name = reinterpret_cast<const char*>(sqlite3_column_text(raw, 1)); // NOSONAR spike-only
        p.age  = sqlite3_column_int(raw, 2);
        out.insert(std::move(p));
    }
    return out;
}

void BM_VariantB_StaticSql_PrepareCached(benchmark::State& state) {
    auto      conn      = shared_conn();
    const int threshold = threshold_for(state.range(0));
    for (auto _ : state) {
        auto stmt = conn->prepare_cached(kStaticSql); // hash + compare every iter
        assert(stmt.has_value());
        auto rows = run_static(*stmt, threshold);
        benchmark::DoNotOptimize(rows);
    }
}

void BM_VariantA_StaticSql_Slot(benchmark::State& state) {
    auto conn = shared_conn();
    // Slot simulation: statements owned in an indexed vector, prepared once.
    static std::vector<std::unique_ptr<Conn::Statement>> slots;
    constexpr std::size_t kSlot = 0;
    if (slots.size() <= kSlot) {
        slots.resize(kSlot + 1);
        auto prepared = conn->prepare(kStaticSql);
        assert(prepared.has_value());
        slots[kSlot] = std::make_unique<Conn::Statement>(std::move(*prepared));
    }
    const int threshold = threshold_for(state.range(0));
    for (auto _ : state) {
        auto* stmt = slots[kSlot].get(); // one array index — the whole lookup
        auto  rows = run_static(stmt, threshold);
        benchmark::DoNotOptimize(rows);
    }
}

} // namespace

BENCHMARK(BM_Baseline_QuerySet)->Name("Spike462/baseline")->Arg(1)->Arg(100)->ArgName("Rows");
BENCHMARK(BM_VariantB_StaticSql_PrepareCached)->Name("Spike462/variantB")->Arg(1)->Arg(100)->ArgName("Rows");
BENCHMARK(BM_VariantA_StaticSql_Slot)->Name("Spike462/variantA")->Arg(1)->Arg(100)->ArgName("Rows");
BENCHMARK_MAIN();
```

Adjust to reality while writing: exact `create_table()` spelling and raw-SQLite includes must match what `anchors_raw.cpp` / `query_benchmark.cppm` actually use (read them first, Step 2). If the QuerySet API for table creation differs (e.g. schema helper), mirror the bench setup used there. Keep the three-variant structure and the cache-count fairness probe exactly as above.

- [ ] **Step 4: Add the `storm_spike` target**

Open `benchmarks/CMakeLists.txt`, find the `storm_anchors` target block, duplicate it as `storm_spike` with source `spike_static_query.cpp` (it must link storm + benchmark::benchmark like the others; it does NOT need dashboard/registry sources).

- [ ] **Step 5: Configure + build Release, verify flags**

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release -j1 --target storm_spike   # -j1 first bench build (PCM corruption)
cmake --build --preset ninja-release --target storm_spike        # second pass, module-cache safety
grep -o '\-O3' build/release/build.ninja | head -1               # MUST print -O3 (worktree flag-drop trap)
```
Expected: builds clean; `-O3` present. If `-O3` missing: delete `build/` in the worktree and re-configure before benching.

- [ ] **Step 6: Run interleaved**

```bash
./build/release/benchmarks/storm_spike \
  --benchmark_enable_random_interleaving=true \
  --benchmark_repetitions=12 \
  --benchmark_report_aggregates_only=true \
  --benchmark_min_time=0.2s | tee /tmp/spike462.txt
```
Expected: 6 benchmark families with mean/median/stddev/cv rows. **Trust check: cv < 5% on every family; if not, close other apps / re-run — do not gate on noisy numbers.**

- [ ] **Step 7: Gate evaluation + post numbers**

Compute deltas from medians at `Rows:1`: `gain_A = (baseline - variantA) / baseline`, same for B. Gate passes iff `gain_A >= 2%`. Post the full aggregate table + verdict:

```bash
gh issue comment 462 --body "$(cat <<'EOF'
## Spike results (Phase 0)
<paste aggregate table from /tmp/spike462.txt>

- baseline vs variantA (Rows:1): X.X% — GATE {PASS|FAIL} (threshold 2%)
- baseline vs variantB (Rows:1): X.X% (attribution: SQL-build vs lookup)
- Rows:100 deltas for context: ...
- cv: all families < 5% ✔
EOF
)"
```

- [ ] **Step 8: Commit spike on the spike branch, return to feature branch**

```bash
git add benchmarks/spike_static_query.cpp benchmarks/CMakeLists.txt
git commit -m "spike(462): three-way static-SQL benchmark (throwaway, never merge)"
git checkout feature/462-static-query-path
```
(Spike commit stays on `spike/462-static-query`; branch is deleted in Task 10.)

- [ ] **Step 9: STOP — report gate verdict to the user.** If FAIL: close #462 citing the comment, `git branch -D spike/462-static-query`, remove worktree, done. If PASS: continue to Task 2.

---

### Task 2: Slot registry (`slot_of`)

**Files:**
- Modify: `src/orm/utilities.cppm` (append to `storm::orm::utilities`, export from the module as the existing symbols are)
- Test: `tests/query/test_static_query.cpp` (new file — GLOB picks it up)

**Interfaces:**
- Produces: `storm::orm::utilities::slot_of<Tag>() noexcept -> std::size_t` — process-wide, unique per `Tag` type, stable across calls. Later tasks call it as `utilities::slot_of<Self>()` where `Self` is the full `StaticQuery` instantiation.

- [ ] **Step 1: Write the failing tests**

Create `tests/query/test_static_query.cpp` (header order per S954 pattern used by sibling tests in `tests/query/` — copy the include/import preamble from `tests/query/test_select.cpp` or nearest neighbor, then):

```cpp
namespace {
struct TagA {};
struct TagB {};
} // namespace

TEST(StaticSlotTest, SameTagSameSlot) {
    EXPECT_EQ(storm::orm::utilities::slot_of<TagA>(), storm::orm::utilities::slot_of<TagA>());
}

TEST(StaticSlotTest, DifferentTagsDifferentSlots) {
    EXPECT_NE(storm::orm::utilities::slot_of<TagA>(), storm::orm::utilities::slot_of<TagB>());
}
```

- [ ] **Step 2: Build + run — must FAIL to compile** (`slot_of` undeclared)

```bash
cmake --build --preset ninja-debug 2>&1 | tail -20
```

- [ ] **Step 3: Implement in `utilities.cppm`** (bottom of the namespace, before the closing brace)

```cpp
// #462: process-wide slot ids for the static-query path. One id per Tag type,
// assigned on first use (thread-safe via static-init), never reused.
inline std::atomic<std::size_t> next_static_slot{0};

export template <typename Tag> auto slot_of() noexcept -> std::size_t {
    static const std::size_t slot = next_static_slot.fetch_add(1, std::memory_order_relaxed);
    return slot;
}
```
(If `utilities.cppm` exports via an `export { ... }` block instead of per-symbol `export`, match that style.)

- [ ] **Step 4: Build + run the two tests — PASS**

```bash
cmake --build --preset ninja-debug && ./build/debug/tests/storm_tests --gtest_filter="StaticSlotTest.*"
```

- [ ] **Step 5: Reviewer + commit**

Dispatch `storm-code-reviewer` on the staged diff; address findings; then:

```bash
git add src/orm/utilities.cppm tests/query/test_static_query.cpp
git commit -m "feat(static-query): process-wide slot registry slot_of<Tag>() (#462)"
```
(Pre-commit hook runs format/tidy/tests/coverage — the debug preset must be built first.)

---

### Task 3: `Connection::prepare_slot` — SQLite

**Files:**
- Modify: `src/db/sqlite.cppm` (`class Connection`, next to `prepare_cached` at ~line 357; new member at the bottom with the other members)
- Test: `tests/query/test_static_query.cpp` (append), `tests/errors/` mock-error file following the `test_orm_mock_errors.cpp` pattern for the prepare-failure path

**Interfaces:**
- Consumes: existing `prepare_raw`, `Statement`, `Error`.
- Produces: `auto prepare_slot(std::size_t slot, std::string_view sql) -> std::expected<Statement*, Error>` — identical contract on both backends (Task 4 mirrors it). Statement pointer stable until connection destruction; NOT invalidated by `clear_statement_cache()` (separate storage).

- [ ] **Step 1: Write the failing tests** (append to `tests/query/test_static_query.cpp`)

```cpp
TEST(PrepareSlotTest, SamePointerOnRepeat) {
    auto conn = storm::db::sqlite::Connection::open(":memory:");
    ASSERT_TRUE(conn.has_value());
    auto a = conn->prepare_slot(0, "SELECT 1");
    auto b = conn->prepare_slot(0, "SELECT 1");
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_EQ(*a, *b);
}

TEST(PrepareSlotTest, DistinctSlotsDistinctStatements) {
    auto conn = storm::db::sqlite::Connection::open(":memory:");
    ASSERT_TRUE(conn.has_value());
    auto a = conn->prepare_slot(0, "SELECT 1");
    auto b = conn->prepare_slot(5, "SELECT 2"); // sparse slot — vector grows
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_NE(*a, *b);
}

TEST(PrepareSlotTest, InvalidSqlPropagatesError) {
    auto conn = storm::db::sqlite::Connection::open(":memory:");
    ASSERT_TRUE(conn.has_value());
    auto r = conn->prepare_slot(0, "NOT VALID SQL");
    EXPECT_FALSE(r.has_value());
}

TEST(PrepareSlotTest, ClosedConnectionRefuses) {
    auto conn = storm::db::sqlite::Connection::open(":memory:");
    ASSERT_TRUE(conn.has_value());
    storm::db::sqlite::Connection moved = std::move(*conn); // source now closed
    auto r = conn->prepare_slot(0, "SELECT 1");
    EXPECT_FALSE(r.has_value());
}
```

- [ ] **Step 2: Run — FAIL to compile** (`prepare_slot` missing)
- [ ] **Step 3: Implement** (after `prepare_cached` in `sqlite.cppm`)

```cpp
// #462 static-query path: statements owned by the Connection, indexed by a
// process-wide slot id (utilities::slot_of). No hashing, no mutex — a
// Connection is single-thread by contract (per-thread connections), same as
// every Statement returned by prepare_cached.
[[nodiscard]] auto prepare_slot(std::size_t slot, std::string_view sql)
        -> std::expected<Statement*, Error> {
    if (!is_open()) {
        return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
    }
    if (slot >= slot_stmts_.size()) {
        slot_stmts_.resize(slot + 1);
    }
    if (auto& cached = slot_stmts_[slot]) [[likely]] {
        return cached.get();
    }
    auto prepared = prepare_raw(sql);
    if (!prepared.has_value()) {
        return std::unexpected(prepared.error());
    }
    slot_stmts_[slot] = std::make_unique<Statement>(std::move(*prepared));
    return slot_stmts_[slot].get();
}
```
Member (bottom of class, next to `cache_`): `std::vector<std::unique_ptr<Statement>> slot_stmts_;`

- [ ] **Step 4: Run tests — PASS** (`--gtest_filter="PrepareSlotTest.*"`)
- [ ] **Step 5: Mock error-path test** — read the existing mock pattern in `tests/errors/` and `tests/mock_sqlite/`; if `sqlite3_prepare_v2` failure is already injectable there, add one case exercising `prepare_slot` through it. If the LD_PRELOAD mock lacks a hook for this symbol path, add a configurable stub the way past work did (memory: new real symbol crashes LD_PRELOAD mock). Verify the whole errors suite still passes.
- [ ] **Step 6: Reviewer + commit** — `storm-code-reviewer` on staged diff, then commit `feat(static-query): Connection::prepare_slot for SQLite (#462)`.

---

### Task 4: `Connection::prepare_slot` — PostgreSQL

**Files:**
- Modify: `src/db/postgresql.cppm` (or the PG connection module — locate `prepare_cached` there and mirror Task 3 exactly)
- Test: `tests/query/test_static_query.cpp` (append TYPED_TEST-independent PG cases guarded the way sibling PG tests are — they skip gracefully when `STORM_PG_CONNSTR` is absent)

**Interfaces:**
- Produces: same signature/contract as Task 3 on the PG `Connection`. PG note: `prepare_raw` on PG creates a server-side prepared statement — slot statements must deallocate with the connection exactly like cached ones (check how `prepare_cached` entries are released; reuse the same mechanism; see memory on PG server-side leak testing).

- [ ] **Step 1: Write failing PG tests** — copy the four Task 3 cases, using the PG connection type + the connstr-skip guard used by neighboring PG tests.
- [ ] **Step 2: Run (`ctest --preset ninja-debug`) — PG cases FAIL to compile / assert.**
- [ ] **Step 3: Implement** — mirror Task 3's body 1:1 on the PG Connection (same member name `slot_stmts_`); adapt the not-open error to the PG `Error` shape used by its `prepare_cached`.
- [ ] **Step 4: Run with a live PG (`ctest --preset ninja-debug`) — PASS; also `ninja-debug-sqlite` preset stays green.**
- [ ] **Step 5: PG mock (`tests/mock_libpq/`)** — if the mock intercepts prepare calls, verify no new real symbol crashes it (memory: add configurable stub if so). Run the errors suite.
- [ ] **Step 6: Reviewer + commit** — `feat(static-query): Connection::prepare_slot for PostgreSQL (#462)`.

---

### Task 5: `static_where.cppm` — typed expression layer

**Files:**
- Create: `src/orm/static_where.cppm` (module `storm_orm_static_where`; module-name style copied from `src/orm/where.cppm`'s declaration; imports `where` module for `CompOp`/`comp_op_to_sql` reuse, `utilities` for `ConstexprString`)
- Modify: `src/storm.cppm` (re-export the new partition/module the way `where.cppm` is re-exported; add `storm::sf` alias next to `storm::f`)
- Test: `tests/query/test_static_query.cpp` (append)

**Interfaces:**
- Consumes: `CompOp`, `comp_op_to_sql` (where module), `ConstexprString<N>` (utilities), `Statement::bind_*`.
- Produces (used verbatim by Task 6):
  - `sf<^^M::field>()` → `StaticField<M>`; operators/methods return node types.
  - Every node type `E` exposes:
    - `static constexpr std::size_t param_count`
    - `static consteval auto sql_size() -> std::size_t` (upper bound incl. operators/parens)
    - `template <typename ConnType, std::size_t Cap> static consteval void render(ConstexprString<Cap>& out, std::size_t& next_param)` — appends the fragment; SQLite appends `?`, PG appends `$<next_param>`; increments `next_param` per placeholder
    - `template <typename StmtType, typename ErrorType> auto bind(StmtType* stmt, int& idx) const -> std::expected<void, ErrorType>` — binds runtime values in render order, incrementing `idx`
  - `StaticAnd<L,R>` / `StaticOr<L,R>` via `operator&&` / `operator||`, rendering `(L <op> R)`.
  - `NoWhere` sentinel: `param_count == 0`, `sql_size() == 0`, `render` no-op, `bind` returns success.
  - Concept `StaticExpr<E>` satisfied by all node types (and used by Task 6's `where()` constraint).

- [ ] **Step 1: Write the failing render/bind tests** (these test through public consteval surfaces — no execution yet)

```cpp
// Person comes from test_models.h (id / name / age fields — reuse the model
// the sibling select tests use; adjust member names to the actual model).
using storm::sf;

TEST(StaticWhereRenderTest, ComparisonSqlite) {
    auto e = sf<^^Person::age>() > 30;
    constexpr std::size_t cap = decltype(e)::sql_size() + 1;
    storm::orm::utilities::ConstexprString<cap> out;
    std::size_t p = 1;
    decltype(e)::template render<storm::db::sqlite::Connection>(out, p);
    EXPECT_EQ(std::string(out), "age > ?");
    EXPECT_EQ(p, 2u);
}

TEST(StaticWhereRenderTest, NestedLogicalPg) {
    auto e = (sf<^^Person::age>() > 30 && sf<^^Person::name>() != "x") || sf<^^Person::age>() <= 5;
    constexpr std::size_t cap = decltype(e)::sql_size() + 1;
    storm::orm::utilities::ConstexprString<cap> out;
    std::size_t p = 1;
    decltype(e)::template render<storm::db::postgresql::Connection>(out, p);
    EXPECT_EQ(std::string(out), "((age > $1 AND name != $2) OR age <= $3)");
    EXPECT_EQ(decltype(e)::param_count, 3u);
}

TEST(StaticWhereRenderTest, BetweenLikeNull) {
    auto b = sf<^^Person::age>().between(10, 20);
    auto l = sf<^^Person::name>().like("a%");
    auto n = sf<^^Person::email>().is_null();   // use a nullable member of the test model

    constexpr std::size_t bcap = decltype(b)::sql_size() + 1;
    storm::orm::utilities::ConstexprString<bcap> bout;
    std::size_t bp = 1;
    decltype(b)::template render<storm::db::sqlite::Connection>(bout, bp);
    EXPECT_EQ(std::string(bout), "age BETWEEN ? AND ?");
    EXPECT_EQ(decltype(b)::param_count, 2u);

    constexpr std::size_t lcap = decltype(l)::sql_size() + 1;
    storm::orm::utilities::ConstexprString<lcap> lout;
    std::size_t lp = 1;
    decltype(l)::template render<storm::db::sqlite::Connection>(lout, lp);
    EXPECT_EQ(std::string(lout), "name LIKE ?");

    constexpr std::size_t ncap = decltype(n)::sql_size() + 1;
    storm::orm::utilities::ConstexprString<ncap> nout;
    std::size_t np = 1;
    decltype(n)::template render<storm::db::sqlite::Connection>(nout, np);
    EXPECT_EQ(std::string(nout), "email IS NULL");
    EXPECT_EQ(decltype(n)::param_count, 0u);
}
```
(The `...` in the third test is shorthand IN THIS PLAN ONLY for repeating the identical 5-line render pattern — write all three out in the real file. Adjust expected spelling — `!=` vs `<>`, spacing — to byte-match `comp_op_to_sql` / the dynamic `to_sql` output, which is the authority.)

Also a bind-order test using a real statement:

```cpp
TEST(StaticWhereBindTest, BindsInRenderOrder) {
    auto conn = storm::db::sqlite::Connection::open(":memory:");
    ASSERT_TRUE(conn.has_value());
    ASSERT_TRUE(conn->execute("CREATE TABLE t(a INTEGER, b TEXT)").has_value());
    ASSERT_TRUE(conn->execute("INSERT INTO t VALUES (7, 'x')").has_value());
    auto e = sf<^^Person::age>() == 7 && sf<^^Person::name>() == "x"; // shape only; column names differ — render against t's columns is not the point, bind order is
    auto stmt = conn->prepare("SELECT count(*) FROM t WHERE a = ? AND b = ?");
    ASSERT_TRUE(stmt.has_value());
    int idx = 1;
    auto bound = e.template bind<storm::db::sqlite::Statement, storm::db::sqlite::Error>(&*stmt, idx);
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(idx, 3);
    auto stepped = stmt->step();
    ASSERT_TRUE(stepped.has_value() && *stepped);
    EXPECT_EQ(sqlite3_column_int(stmt->handle(), 0), 1);
}
```

And the compile-time `in()` refusal — a negative-compile assertion via concept:

```cpp
TEST(StaticWhereTest, InIsNotAvailable) {
    // in() must not exist on StaticField — runtime-sized placeholder lists are dynamic-only.
    static_assert(!requires(decltype(sf<^^Person::id>()) f) { f.in(1, 2); });
}
```

- [ ] **Step 2: Run — FAIL to compile** (module doesn't exist).
- [ ] **Step 3: Implement `src/orm/static_where.cppm`**

Core (complete; module preamble/imports copied from `where.cppm`'s style):

```cpp
export module storm_orm_static_where;   // match the naming scheme of sibling modules exactly

import storm_orm_where;      // CompOp, comp_op_to_sql  (use the real module names from where.cppm)
import storm_orm_utilities;  // ConstexprString

namespace storm::orm::static_where {

    using utilities::ConstexprString;
    using where::CompOp;
    using where::comp_op_to_sql;

    // Placeholder append: SQLite "?", PG "$<n>". Consteval — digits written manually.
    // Exported: static_query.cppm (Task 6) uses it for LIMIT/OFFSET placeholders.
    export template <typename ConnType, std::size_t Cap>
    consteval void append_placeholder(ConstexprString<Cap>& out, std::size_t n) {
        if constexpr (requires { ConnType::supports_limit_all; } &&
                      std::is_same_v<ConnType, storm::db::sqlite::Connection>) {
            out.append("?");
        } else {
            out.append("$");
            std::array<char, 20> buf{};
            std::size_t          len = 0;
            do { buf[len++] = static_cast<char>('0' + (n % 10)); n /= 10; } while (n > 0);
            for (std::size_t i = len; i > 0; --i) {
                const char digit[2] = {buf[i - 1], '\0'};
                out.append(std::string_view{digit, 1});
            }
        }
    }
    // NOTE for implementer: pick the backend switch off whatever dialect trait
    // distinguishes the two Connection types today (see `supports_limit_all` /
    // placeholder translation in #418's translate_placeholders) — a dedicated
    // `static constexpr bool positional_params` trait on each Connection is the
    // clean move; add it to both Connections in this task if absent.

    export struct NoWhere {
        static constexpr std::size_t param_count = 0;
        static consteval auto        sql_size() -> std::size_t { return 0; }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& /*out*/, std::size_t& /*next*/) {}
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* /*stmt*/, int& /*idx*/) const -> std::expected<void, ErrorType> { return {}; }
    };

    // Value binding for the closed set of static-path value types.
    template <typename StmtType, typename ErrorType, typename V>
    auto bind_one(StmtType* stmt, int& idx, const V& value) -> std::expected<void, ErrorType> {
        std::expected<void, ErrorType> r;
        if constexpr (std::is_same_v<V, bool>)            r = stmt->bind_int(idx, value ? 1 : 0);
        else if constexpr (std::is_integral_v<V> && sizeof(V) <= 4) r = stmt->bind_int(idx, static_cast<int>(value));
        else if constexpr (std::is_integral_v<V>)         r = stmt->bind_int64(idx, static_cast<std::int64_t>(value));
        else if constexpr (std::is_floating_point_v<V>)   r = stmt->bind_double(idx, static_cast<double>(value));
        else                                              r = stmt->bind_text(idx, std::string_view{value});
        if (!r.has_value()) return std::unexpected(r.error());
        ++idx;
        return {};
    }

    export template <std::meta::info M, CompOp Op, typename V> struct StaticCmp {
        V value_;
        static constexpr std::string_view name        = std::meta::identifier_of(M);
        static constexpr std::size_t      param_count = 1;
        static consteval auto sql_size() -> std::size_t {
            return name.size() + 4 /* " <op> " */ + 21 /* "$NNN…" worst case */;
        }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& out, std::size_t& next) {
            out.append(name);
            out.append(" ");
            out.append(comp_op_to_sql(Op));
            out.append(" ");
            append_placeholder<ConnType>(out, next++);
        }
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* stmt, int& idx) const -> std::expected<void, ErrorType> {
            return bind_one<StmtType, ErrorType>(stmt, idx, value_);
        }
    };

    export template <std::meta::info M, typename V> struct StaticBetween {
        V lo_;
        V hi_;
        static constexpr std::string_view name        = std::meta::identifier_of(M);
        static constexpr std::size_t      param_count = 2;
        static consteval auto sql_size() -> std::size_t { return name.size() + 9 /* " BETWEEN " */ + 21 + 5 /* " AND " */ + 21; }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& out, std::size_t& next) {
            out.append(name);
            out.append(" BETWEEN ");
            append_placeholder<ConnType>(out, next++);
            out.append(" AND ");
            append_placeholder<ConnType>(out, next++);
        }
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* stmt, int& idx) const -> std::expected<void, ErrorType> {
            if (auto r = bind_one<StmtType, ErrorType>(stmt, idx, lo_); !r.has_value()) return r;
            return bind_one<StmtType, ErrorType>(stmt, idx, hi_);
        }
    };

    export template <std::meta::info M> struct StaticLike {
        std::string pattern_;
        static constexpr std::string_view name        = std::meta::identifier_of(M);
        static constexpr std::size_t      param_count = 1;
        static consteval auto sql_size() -> std::size_t { return name.size() + 6 /* " LIKE " */ + 21; }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& out, std::size_t& next) {
            out.append(name);
            out.append(" LIKE ");
            append_placeholder<ConnType>(out, next++);
        }
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* stmt, int& idx) const -> std::expected<void, ErrorType> {
            return bind_one<StmtType, ErrorType>(stmt, idx, pattern_);
        }
    };

    export template <std::meta::info M, bool IsNull> struct StaticIsNull {
        static constexpr std::string_view name        = std::meta::identifier_of(M);
        static constexpr std::size_t      param_count = 0;
        static consteval auto sql_size() -> std::size_t { return name.size() + 12 /* " IS NOT NULL" */; }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& out, std::size_t& /*next*/) {
            out.append(name);
            out.append(IsNull ? " IS NULL" : " IS NOT NULL");
        }
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* /*stmt*/, int& /*idx*/) const -> std::expected<void, ErrorType> { return {}; }
    };

    template <typename E>
    concept StaticExprC = requires { E::param_count; E::sql_size(); };
    export template <typename E> concept StaticExpr = StaticExprC<std::remove_cvref_t<E>>;

    export template <typename L, typename R, bool IsAnd> struct StaticLogical {
        L lhs_;
        R rhs_;
        static constexpr std::size_t param_count = L::param_count + R::param_count;
        static consteval auto sql_size() -> std::size_t {
            return 1 + L::sql_size() + (IsAnd ? 5 : 4) /* " AND " / " OR " */ + R::sql_size() + 1;
        }
        template <typename ConnType, std::size_t Cap>
        static consteval void render(ConstexprString<Cap>& out, std::size_t& next) {
            out.append("(");
            L::template render<ConnType>(out, next);
            out.append(IsAnd ? " AND " : " OR ");
            R::template render<ConnType>(out, next);
            out.append(")");
        }
        template <typename StmtType, typename ErrorType>
        auto bind(StmtType* stmt, int& idx) const -> std::expected<void, ErrorType> {
            if (auto r = lhs_.template bind<StmtType, ErrorType>(stmt, idx); !r.has_value()) return r;
            return rhs_.template bind<StmtType, ErrorType>(stmt, idx);
        }
    };

    export template <StaticExpr L, StaticExpr R> auto operator&&(L lhs, R rhs) {
        return StaticLogical<L, R, true>{std::move(lhs), std::move(rhs)};
    }
    export template <StaticExpr L, StaticExpr R> auto operator||(L lhs, R rhs) {
        return StaticLogical<L, R, false>{std::move(lhs), std::move(rhs)};
    }

    export template <std::meta::info M>
        requires(std::meta::is_nonstatic_data_member(M) && !storm::meta::is_relation_field(M))
    struct StaticField {
        using FieldType = typename[:std::meta::type_of(M):];
        template <typename V> auto operator==(V&& v) const { return make<CompOp::Equal>(std::forward<V>(v)); }
        template <typename V> auto operator!=(V&& v) const { return make<CompOp::NotEqual>(std::forward<V>(v)); }
        template <typename V> auto operator>(V&& v)  const { return make<CompOp::Greater>(std::forward<V>(v)); }
        template <typename V> auto operator>=(V&& v) const { return make<CompOp::GreaterEqual>(std::forward<V>(v)); }
        template <typename V> auto operator<(V&& v)  const { return make<CompOp::Less>(std::forward<V>(v)); }
        template <typename V> auto operator<=(V&& v) const { return make<CompOp::LessEqual>(std::forward<V>(v)); }
        template <typename V> auto between(V lo, V hi) const { return StaticBetween<M, V>{std::move(lo), std::move(hi)}; }
        auto like(std::string_view pattern) const { return StaticLike<M>{std::string(pattern)}; }
        auto is_null()     const requires where::NullableField<FieldType> { return StaticIsNull<M, true>{}; }
        auto is_not_null() const requires where::NullableField<FieldType> { return StaticIsNull<M, false>{}; }
        // NO in(): runtime-sized placeholder lists are inherently dynamic — use QuerySet.
      private:
        template <CompOp Op, typename V> auto make(V&& v) const {
            using Stored = std::remove_cvref_t<V>;
            if constexpr (std::is_convertible_v<Stored, std::string_view> && !std::is_same_v<Stored, std::string>) {
                return StaticCmp<M, Op, std::string>{std::string(std::forward<V>(v))};
            } else {
                return StaticCmp<M, Op, Stored>{std::forward<V>(v)};
            }
        }
    };

    export template <std::meta::info M> auto sf() { return StaticField<M>{}; }

} // namespace storm::orm::static_where
```

Write `StaticBetween` / `StaticLike` / `StaticIsNull` out in full following the `StaticCmp` five-member shape (the comment block above lists their exact SQL and param counts). Then re-export `sf` from `storm.cppm` next to `f` (same re-export style as #442's annotation re-exports).

**Compiler-risk note:** `where.cppm` already instantiates `[:std::meta::type_of(M):]` inside class templates in module purview, so this pattern is proven. If BMI segfaults appear, the known workarounds are: keep every splice inside the template (never at namespace scope), and build twice. Escalate to the `clang-cpp26-compiler-specialist` agent if it persists.

- [ ] **Step 4: Build + run the StaticWhere tests — PASS** (`--gtest_filter="StaticWhere*"`)
- [ ] **Step 5: Reviewer + commit** — `feat(static-query): typed expression layer static_where.cppm (#462)`.

---

### Task 6: `static_query.cppm` — assembly + execution

**Files:**
- Create: `src/orm/static_query.cppm`
- Modify: `src/storm.cppm` (re-export `static_query`)
- Test: `tests/query/test_static_query.cpp` (append)

**Interfaces:**
- Consumes: Task 5 node contract (`param_count` / `sql_size` / `render` / `bind`, `NoWhere`, `StaticExpr`), Task 2 `slot_of`, Tasks 3-4 `prepare_slot`, `BaseStatement` consteval helpers (`table_name_`, `FieldNameGrammar<Base>::build_all_field_names_list()` / `calculate_field_names_size()` as used in `select.cppm:36-59`), `Base::extract_all_columns`, `QuerySet<Model, ConnType>::get_default_connection()`.
- Produces (public API):
  - `storm::static_query<Model, ConnType = default>()` → `StaticQuery<Model, ConnType, NoWhere, NoOrder, false, false>`
  - `.where(StaticExpr auto expr)` (allowed once; second call = compile error via `requires std::same_as<WhereT, NoWhere>`)
  - `.order_by<^^M::field>()` / `.order_by_desc<^^M::field>()` (once; `OrderSpec<Member, Desc>` / `NoOrder`)
  - `.limit(int)` / `.offset(int)` — value runtime, presence in type (`HasLimit`/`HasOffset`; offset requires limit, same rule as the dynamic grammar if it has one — check `append_limit_offset` and mirror)
  - `.select()` → `std::expected<plf::hive<Model>, Error>`
  - `.to_sql()` → `std::string_view` (the consteval string — noexcept, no connection needed)

- [ ] **Step 1: Write the failing tests** — the execution + parity core (append; TYPED_TEST over the same `DatabaseTypes` list the sibling select tests use, with their fixture/seed helpers):

```cpp
TYPED_TEST(StaticQueryTest, SelectWithWhereMatchesDynamic) {
    this->seed_persons();   // reuse the fixture seeding the sibling tests use
    auto stat = storm::static_query<Person, TypeParam>()
                        .where(sf<^^Person::age>() > 30)
                        .select();
    auto dyn  = storm::QuerySet<Person, TypeParam>{}
                        .where(storm::f<^^Person::age>() > 30)
                        .select();
    ASSERT_TRUE(stat.has_value() && dyn.has_value());
    EXPECT_EQ(stat->size(), dyn->size());
}

TYPED_TEST(StaticQueryTest, SqlParityWhereOrderBy) {
    auto sql = storm::static_query<Person, TypeParam>()
                       .where(sf<^^Person::age>() > 30 && sf<^^Person::name>() != "x")
                       .template order_by<^^Person::name>()
                       .to_sql();
    auto dyn = storm::QuerySet<Person, TypeParam>{}
                       .where(storm::f<^^Person::age>() > 30 && storm::f<^^Person::name>() != "x")
                       .template order_by<^^Person::name>()
                       .sql();   // use the dynamic API's raw-SQL surface (#411 to_sql parity work) — NOT expanded_sql
    EXPECT_EQ(std::string(sql), dyn);
}

TYPED_TEST(StaticQueryTest, LimitOffsetAreBoundParams) {
    this->seed_persons_n(50);
    auto r = storm::static_query<Person, TypeParam>()
                     .where(sf<^^Person::age>() >= 0)
                     .template order_by<^^Person::age>()
                     .limit(10)
                     .offset(5)
                     .select();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 10u);
    auto sql = storm::static_query<Person, TypeParam>()
                       .where(sf<^^Person::age>() >= 0)
                       .template order_by<^^Person::age>()
                       .limit(10).offset(5).to_sql();
    EXPECT_TRUE(std::string(sql).contains("LIMIT"));   // placeholders, not literals:
    EXPECT_FALSE(std::string(sql).contains("LIMIT 10"));
}

TYPED_TEST(StaticQueryTest, RepeatedExecutionReusesSlot) {
    this->seed_persons();
    auto q = storm::static_query<Person, TypeParam>().where(sf<^^Person::age>() > 30);
    auto a = q.select();
    auto b = q.select();
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_EQ(a->size(), b->size());
}

TYPED_TEST(StaticQueryTest, StaticAndDynamicInterleaveOnOneConnection) {
    this->seed_persons();
    auto s1 = storm::static_query<Person, TypeParam>().where(sf<^^Person::age>() > 30).select();
    auto d  = storm::QuerySet<Person, TypeParam>{}.where(storm::f<^^Person::age>() < 30).select();
    auto s2 = storm::static_query<Person, TypeParam>().where(sf<^^Person::age>() > 30).select();
    ASSERT_TRUE(s1.has_value() && d.has_value() && s2.has_value());
    EXPECT_EQ(s1->size(), s2->size());
}
```
Plus, in the same step, the remaining checklist cases (write them all — each is 5-10 lines in the same shapes as above): each of the 6 comparison operators returning correct row counts; BETWEEN/LIKE/IS NULL/IS NOT NULL execution; nested `(A && B) || C`; ORDER BY value ordering asc + desc; empty result set; single row; 100+ rows; int/string/double predicates; `to_sql()` exact-string test per backend for one representative shape (hardcoded expected string, both `?` and `$N` forms).

- [ ] **Step 2: Run — FAIL to compile** (`static_query` missing).
- [ ] **Step 3: Implement `src/orm/static_query.cppm`**

```cpp
export module storm_orm_static_query;   // match sibling naming

import storm_orm_static_where;
import storm_orm_utilities;
import storm_orm_statements_base;       // BaseStatement — real module names from the tree
import storm_orm_statements_field_names;
import storm_orm_queryset;              // get_default_connection

namespace storm::orm::static_query_ns {

    using static_where::NoWhere;
    using static_where::StaticExpr;
    using utilities::ConstexprString;

    export struct NoOrder {
        static consteval auto sql_size() -> std::size_t { return 0; }
        template <std::size_t Cap> static consteval void render(ConstexprString<Cap>& /*out*/) {}
    };
    export template <std::meta::info M, bool Desc> struct OrderSpec {
        static constexpr std::string_view name = std::meta::identifier_of(M);
        static consteval auto sql_size() -> std::size_t { return 10 + name.size() + 5; } // " ORDER BY " + name + " DESC"
        template <std::size_t Cap> static consteval void render(ConstexprString<Cap>& out) {
            out.append(" ORDER BY ");
            out.append(name);
            if constexpr (Desc) { out.append(" DESC"); }
        }
    };

    export template <typename Model, typename ConnType, typename WhereT, typename OrderT,
                     bool HasLimit, bool HasOffset>
    class StaticQuery {
        using Base      = BaseStatement<Model, ConnType>;   // exact template name/params from base.cppm
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        WhereT where_{};
        int    limit_{};
        int    offset_{};

        // ---- compile-time SQL assembly -------------------------------------
        static consteval auto build_sql_array() {
            constexpr std::size_t cap = 7 /*SELECT */ + FieldNameGrammar<Base>::calculate_field_names_size()
                                      + 6 /* FROM */ + Base::table_name_.size()
                                      + 7 /* WHERE */ + WhereT::sql_size()
                                      + OrderT::sql_size()
                                      + 30 /* " LIMIT $NNN OFFSET $NNN" */ + 1;
            ConstexprString<cap> out;
            out.append("SELECT ");
            out.append(FieldNameGrammar<Base>::build_all_field_names_list());
            out.append(" FROM ");
            out.append(Base::table_name_);
            std::size_t next_param = 1;
            if constexpr (!std::is_same_v<WhereT, NoWhere>) {
                out.append(" WHERE ");
                WhereT::template render<ConnType>(out, next_param);
            }
            OrderT::render(out);
            if constexpr (HasLimit)  { out.append(" LIMIT ");  static_where::append_placeholder<ConnType>(out, next_param++); }
            if constexpr (HasOffset) { out.append(" OFFSET "); static_where::append_placeholder<ConnType>(out, next_param++); }
            return out;
        }
        static constexpr auto             sql_array = build_sql_array();
        static constexpr std::string_view sql_view{sql_array.data.data(), sql_array.len};

      public:
        StaticQuery() = default;
        StaticQuery(WhereT w, int limit, int offset)
            : where_(std::move(w)), limit_(limit), offset_(offset) {}

        template <StaticExpr E>
            requires std::same_as<WhereT, NoWhere>   // one where() per chain
        auto where(E expr) && {
            return StaticQuery<Model, ConnType, E, OrderT, HasLimit, HasOffset>{std::move(expr), limit_, offset_};
        }
        template <std::meta::info M> auto order_by() &&
            requires std::same_as<OrderT, NoOrder> {
            return StaticQuery<Model, ConnType, WhereT, OrderSpec<M, false>, HasLimit, HasOffset>{std::move(where_), limit_, offset_};
        }
        template <std::meta::info M> auto order_by_desc() &&
            requires std::same_as<OrderT, NoOrder> {
            return StaticQuery<Model, ConnType, WhereT, OrderSpec<M, true>, HasLimit, HasOffset>{std::move(where_), limit_, offset_};
        }
        auto limit(int n) && requires(!HasLimit) {
            auto next = StaticQuery<Model, ConnType, WhereT, OrderT, true, HasOffset>{std::move(where_), n, offset_};
            return next;
        }
        auto offset(int n) && requires(HasLimit && !HasOffset) {   // OFFSET only with LIMIT — mirror dynamic grammar
            auto next = StaticQuery<Model, ConnType, WhereT, OrderT, HasLimit, true>{std::move(where_), limit_, n};
            return next;
        }

        [[nodiscard]] static constexpr auto to_sql() noexcept -> std::string_view { return sql_view; }

        [[nodiscard]] auto select() const -> std::expected<plf::hive<Model>, Error> {
            auto conn = QuerySet<Model, ConnType>::get_default_connection();
            auto stmt_r = conn->prepare_slot(utilities::slot_of<StaticQuery>(), sql_view);
            if (!stmt_r.has_value()) { return std::unexpected(stmt_r.error()); }
            Statement* stmt = *stmt_r;
            if (auto r = stmt->reset(); !r.has_value()) { return std::unexpected(r.error()); }
            int idx = 1;
            if (auto r = where_.template bind<Statement, Error>(stmt, idx); !r.has_value()) {
                return std::unexpected(r.error());
            }
            if constexpr (HasLimit)  { if (auto r = stmt->bind_int(idx++, limit_);  !r.has_value()) return std::unexpected(r.error()); }
            if constexpr (HasOffset) { if (auto r = stmt->bind_int(idx++, offset_); !r.has_value()) return std::unexpected(r.error()); }
            plf::hive<Model> out;
            while (true) {
                auto stepped = stmt->step();
                if (!stepped.has_value()) { return std::unexpected(stepped.error()); }
                if (!*stepped) { break; }
                Model obj;
                Base::extract_all_columns(stmt, obj);
                out.insert(std::move(obj));
            }
            return out;
        }
    };

    export template <typename Model, typename ConnType = storm::db::sqlite::Connection>
    auto static_query() {
        return StaticQuery<Model, ConnType, NoWhere, NoOrder, false, false>{};
    }

} // namespace storm::orm::static_query_ns
```

Adapt to reality while writing (the authority is the tree, not this listing): exact module/import names; `BaseStatement`'s real template parameters; `extract_all_columns` signature (pointer vs reference — `select.cppm:300` shows `Base::extract_all_columns(stmt, obj)` with `Statement*`); whether `reset()` returns `expected` or `void` (mirror how `select.cppm` calls it); the default `ConnType` spelling used by `QuerySet`. The chain methods are `&&`-qualified (each step consumes the previous — matches "each step returns a new type"); add `const&` overloads only if a test genuinely needs re-chaining from an lvalue, not speculatively.

Then export `storm::static_query` from `storm.cppm` next to the `QuerySet` re-export.

- [ ] **Step 4: Build + run the full StaticQuery test set — PASS on `ninja-debug` (SQLite+PG) and `ninja-debug-sqlite`.**
- [ ] **Step 5: Mock error tests** — prepare-failure and bind-failure through `static_query().select()` in the errors suite (same mock as Task 3/4). Run errors suite — PASS.
- [ ] **Step 6: Amend the spec's parity paragraph** — edit `docs/superpowers/specs/2026-07-19-static-query-design.md` "Correctness invariant": byte-parity holds for the SELECT/WHERE/ORDER BY grammar; LIMIT/OFFSET intentionally diverge (dynamic emits literals, static binds parameters — covered by `LimitOffsetAreBoundParams`).
- [ ] **Step 7: Reviewer + commit** — `feat(static-query): StaticQuery consteval SQL assembly + slot execution (#462)`.

---

### Task 7: Coverage + Sonar sweep

**Files:**
- Modify: whatever the coverage report says is unhit (tests appended to `tests/query/test_static_query.cpp` / errors suite)

- [ ] **Step 1: Run coverage** — `cmake --build --preset ninja-debug-coverage --target coverage` (remember `STORM_SKIP_COVERAGE` workaround does NOT apply here; if the run hangs, see memory feedback_phase1_coverage_hang_workaround).
- [ ] **Step 2: Diff against develop's baseline** (memory: diff local coverage against develop) — new modules must be at 100% lines. Consteval-only helpers follow the existing `LCOV_EXCL_LINE — compile-time only` convention where genuinely unreachable at runtime; prefer making them consteval-reachable via tests first (memory: #422 column-name writer had to be consteval for coverage).
- [ ] **Step 3: Add tests until clean; reviewer + commit** — `test(static-query): close coverage gaps (#462)`.

---

### Task 8: Benchmark re-run (rule 6)

- [ ] **Step 1: Build release in the worktree, verify `-O3`** (same commands as Task 1 Step 5, target `storm_bench`).
- [ ] **Step 2: Full suite** — `./build/release/benchmarks/storm_bench --benchmark_repetitions=10 --benchmark_report_aggregates_only=true | tee /tmp/bench462.txt`, compare against a develop-worktree run of the same suite (alternating A/B if any family regresses within 2%).
- [ ] **Step 3: Verdict** — ANY slowdown on existing families → find and fix (the static path must be zero-cost when unused: it adds one unused member vector to Connection — if THAT regresses anything, gate it). Post the comparison summary on #462.

---

### Task 9: Documentation

**Files:**
- Create: `docs/guide/features/STATIC_QUERIES.md` (user approved this new file in the design review)
- Modify: `CLAUDE.md` (QuerySet API section — short static-query block + pointer), `docs/README.md` (index), `.claude/agents/storm-orm-developer.md` + `.claude/agents/storm-code-reviewer.md` + `.claude/agents/storm-performance.md` (mention static path, slot cache, and the "no in() / no reassignment" rules)

- [ ] **Step 1: Write STATIC_QUERIES.md** — sections: motivation (compile-time SQL, slot lookup), full API reference with the Task 6 signatures, what is static vs runtime (values, limit/offset), why no `in()`, why no reassignment (type-per-shape), interaction with the dynamic API, spike/benchmark numbers from #462, thread-safety note (per-thread connections; slot vector unsynchronized by the same contract as Statement use).
- [ ] **Step 2: Update CLAUDE.md + agent files + docs index.**
- [ ] **Step 3: Commit (docs-only — reviewer exempt)** — `docs(static-query): STATIC_QUERIES.md + CLAUDE.md + agent files (#462)`.

---

### Task 10: PR, gates, merge, cleanup

- [ ] **Step 1: Check off completed subtasks on #462** (`gh issue edit 462 --body ...` with `- [x]`) — BEFORE the merge, per workflow.
- [ ] **Step 2: Push + PR + auto-merge**

```bash
git push -u origin feature/462-static-query-path
gh pr create --base develop --title "feat(query): compile-time static SQL path (#462)" \
  --body "Closes #462 ... (summary + spike numbers + bench verdict)

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
gh pr merge --squash --auto
```
- [ ] **Step 3: Wait 30s → `/sonarcloud-status`** — zero issues on new code required; fix-push-recheck loop until clean (poll analysis revision before trusting a green, per memory).
- [ ] **Step 4: `gh pr checks <PR#> --watch`** — ninja-debug, ninja-asan-ubsan, ninja-tsan all green (sanitizers run in CI only).
- [ ] **Step 5: After merge** — `gh issue close 462`; delete spike branch `git branch -D spike/462-static-query`; `git checkout develop && git pull` in the main tree; `git worktree remove .claude/worktrees/feature-462-static-query-path`.
