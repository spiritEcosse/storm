# Upsert (`ON CONFLICT`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add single-row `INSERT ... ON CONFLICT (target) DO UPDATE / DO NOTHING` to the `QuerySet` insert proxy.

**Architecture:** A new `consteval` grammar module (`UpsertGrammar<T>`) builds the conflict clause from reflection and appends it to the existing compile-time INSERT...RETURNING SQL. New proxy structs on `InsertStatement` (`ConflictTarget` → `UpdateUpsertQuery` / `NothingUpsertQuery`) carry the conflict target + SET columns as NTTPs and dispatch to two new execute paths. The upsert chain drops `ReturnId`: `.update<>()` returns `expected<int64_t>`, `.nothing()` returns `expected<optional<int64_t>>`.

**Tech Stack:** C++26 (clang-p2996 reflection), Storm ORM modules, GoogleTest TYPED_TEST over SQLite + PostgreSQL.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-06-22-upsert-design.md` — every task implements part of it.
- **Branch:** work in worktree `.claude/worktrees/feature-205-upsert` on `feature/205-upsert-on-conflict`. NEVER edit files in the main `/storm_develop/` tree (edits there silently vanish from this build).
- **TDD (rule #9):** write tests → run (MUST fail) → implement → run (pass). Never implement before a failing test.
- **Build presets:** Debug `cmake --preset ninja-debug && cmake --build --preset ninja-debug`; tests `ctest --preset ninja-debug` or filtered `./build/debug/tests/storm_tests --gtest_filter="UpsertTest.*"`.
- **No CMake changes:** `src/` and `tests/` use GLOB auto-discovery for `.cppm`/`.cpp`. Re-run `cmake --preset ninja-debug` after adding a new file so the glob refreshes.
- **Compile-time errors:** use `requires` constraints, never `throw` in `consteval` (rule #11).
- **`import std;`** + textual `#include <meta>` BEFORE the module imports in reflection TUs (the `.cppm` pattern in update_grammar.cppm).
- **Column-name writer (#422):** always emit columns via `storm::meta::append_column_name(buf, member)` so FK fields get the `_id` suffix consistently.
- **600-line hook limit:** `insert.cppm` is already 604 lines. If adding proxies trips the file-size hook, move the new proxy structs into a sibling `namespace detail` block (pattern from #438) or accept the `.lint-skip` only as a last resort.
- **SonarCloud "Storm Strict":** zero new issues / duplication. Use `std::format` not concat (S6185), `||`/`&&` not `or`/`and` (S3659), `public:` in fixtures (S3656), `emplace_back` (S6003).

---

## File Structure

- **Create** `src/orm/statements/upsert_grammar.cppm` — `UpsertGrammar<T>`: `consteval` conflict-target list, `excluded.col` SET-clause builder, and per-`<Target...,SetCols...>` upsert SQL string. Mirrors `update_grammar.cppm`.
- **Modify** `src/orm/statements/insert.cppm` — `on_conflict<Target...>()` on `SingleQuery`/`VoidQuery`; new `ConflictTarget`, `UpdateUpsertQuery`, `NothingUpsertQuery` proxies; `execute_upsert_update` / `execute_upsert_nothing` paths. Import `storm_orm_statements_upsert_grammar` + `storm_orm_indexes`.
- **Create** `tests/crud/test_upsert.cpp` — TYPED_TEST suite over `DatabaseTypes`.
- **Create** `docs/guide/features/UPSERT.md` — user docs.
- **Modify** `docs/README.md`, `CLAUDE.md` — link + API row.

---

## Task 1: `UpsertGrammar<T>` — conflict-target + SET clause builders

**Files:**
- Create: `src/orm/statements/upsert_grammar.cppm`
- Test: `tests/crud/test_upsert.cpp` (compile-time SQL-string assertions only in this task)

**Interfaces:**
- Consumes: `BaseStatement<T>` (`Base::all_members_`, `Base::primary_key_`, `Base::pk_name_`, `Base::table_name_`), `storm::meta::append_column_name`, `storm::meta::is_auto_update`, `InsertStatement<T,ConnType>::insert_returning_sql_array` (via re-derivation — see step 3).
- Produces:
  - `UpsertGrammar<T>::build_conflict_target<Target...>()` → `ConstexprString` of `(col1, col2, ...)` (FK-aware).
  - `UpsertGrammar<T>::build_excluded_set_clause<SetCols...>()` → `ConstexprString` of `col=excluded.col, ...` (+ unlisted auto_update appended as `col=?`).
  - `UpsertGrammar<T>::is_settable_member<Member>()` → `bool`.
  - `UpsertGrammar<T>::is_unlisted_auto_update<SetCols...>(member)` → `bool`.

- [ ] **Step 1: Write the failing test** — append to (new) `tests/crud/test_upsert.cpp`:

```cpp
#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include <sqlite3.h>

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

namespace {
using storm::orm::statements::UpsertGrammar;
}

// Grammar emits a single-column conflict target via the FK-aware writer.
TEST(UpsertGrammarTest, ConflictTargetSingleColumn) {
    constexpr auto target = UpsertGrammar<Person>::build_conflict_target<^^Person::name>();
    const std::string s(target);
    EXPECT_EQ(s, "(name)");
}

// Grammar emits col=excluded.col for each listed SET column.
TEST(UpsertGrammarTest, ExcludedSetClauseMultipleColumns) {
    constexpr auto set = UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age, ^^Person::salary>();
    const std::string s(set);
    EXPECT_EQ(s, "age=excluded.age, salary=excluded.salary");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -20`
Expected: FAIL — `UpsertGrammar` not declared / module `storm_orm_statements_upsert_grammar` not found.

- [ ] **Step 3: Write `src/orm/statements/upsert_grammar.cppm`**

```cpp
module;

// Compile-time UPSERT (ON CONFLICT) SQL grammar (#205): the consteval helpers
// that spell the conflict-target list and the `excluded.col` DO UPDATE SET
// clause, split out of InsertStatement to keep that class cohesive. Stateless
// and connection-free — every member derives purely from reflection over T.

#include <meta>

export module storm_orm_statements_upsert_grammar;

import std;

import storm_orm_statements_base;
import storm_orm_utilities;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    template <typename T> struct UpsertGrammar {
        using Base = BaseStatement<T>;

        // (col1, col2, ...) — conflict-target column list, FK "_id"-aware (#422).
        template <std::meta::info... Target> static consteval auto build_conflict_target() {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            result.append("(");
            bool first = true;
            (
                    [&] {
                        if (!first) {
                            result.append(", ");
                        }
                        meta::append_column_name(result, Target);
                        first = false;
                    }(),
                    ...
            );
            result.append(")");
            return result;
        }

        // Each SET target must be a non-static data member of T and not the PK.
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            return std::meta::is_nonstatic_data_member(Member) && Member != Base::primary_key_;
        }

        // True when `member` carries auto_update (#209) and is NOT in the explicit pack.
        template <std::meta::info... SetCols>
        static consteval auto is_unlisted_auto_update(std::meta::info member) -> bool {
            return meta::is_auto_update(member) && ((member != SetCols) && ...);
        }

        // "col=excluded.col, ..." for the explicit SetCols pack, then any
        // auto_update field of T not already listed, appended as "col=?" (bound
        // now() at execution). The column ORDER of the auto_update tail is the
        // canonical bind order used by the execute path.
        template <std::meta::info... SetCols> static consteval auto build_excluded_set_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool first = true;
            (
                    [&] {
                        if (!first) {
                            result.append(", ");
                        }
                        meta::append_column_name(result, SetCols);
                        result.append("=excluded.");
                        meta::append_column_name(result, SetCols);
                        first = false;
                    }(),
                    ...
            );
            for (const auto& member : Base::all_members_) {
                if (is_unlisted_auto_update<SetCols...>(member)) {
                    if (!first) {
                        result.append(", ");
                    }
                    result.append(std::meta::identifier_of(member));
                    result.append("=?");
                    first = false;
                }
            }
            return result;
        }
    };

} // namespace storm::orm::statements
```

- [ ] **Step 4: Export the new module from `storm`** — verify `src/storm.cppm` re-exports statement modules. Run:

```bash
grep -n "statements_update_grammar\|export import" src/storm.cppm | head
```

If `update_grammar` is exported there, add the same line for `upsert_grammar`. If statement modules are pulled transitively (no explicit line), no change needed. Mirror whatever `update_grammar` does.

- [ ] **Step 5: Re-glob and build**

Run: `cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -20`
Expected: build succeeds (the new `.cppm` is globbed in).

- [ ] **Step 6: Run the grammar tests**

Run: `ctest --preset ninja-debug -R UpsertGrammarTest --output-on-failure`
Expected: PASS (2 tests).

- [ ] **Step 7: Commit**

```bash
git add src/orm/statements/upsert_grammar.cppm src/storm.cppm tests/crud/test_upsert.cpp
git commit -m "feat(upsert): UpsertGrammar conflict-target + excluded SET clause (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Upsert SQL string builder (full statement)

**Files:**
- Modify: `src/orm/statements/upsert_grammar.cppm`
- Test: `tests/crud/test_upsert.cpp`

**Interfaces:**
- Consumes: Task 1 grammar; `InsertStatement<T,ConnType>` private SQL fragments. Because `UpsertGrammar` is connection-free but the INSERT prefix depends only on `T` (not `ConnType`), re-derive the prefix from `BaseStatement<T>` + `FieldNameGrammar<Base>` rather than reaching into `InsertStatement`.
- Produces:
  - `UpsertGrammar<T>::build_upsert_sql<DoUpdate, Target...>()` returning the full SQL `ConstexprString` (when `DoUpdate==false`, SetCols are ignored — DO NOTHING). For DO UPDATE, see Task overload below.
  - Two static strings via a helper template `upsert_sql_string<bool DoUpdate, target-pack, setcol-pack>` — but C++ can't take two NTTP packs in one template. **Resolution:** split into two entry points:
    - `build_update_upsert_sql<Target..., /*sep*/ SetCols...>()` is impossible (two packs). Instead the SET clause is passed as a precomputed `ConstexprString` value argument. See step 3.

- [ ] **Step 1: Write the failing test** — append to `tests/crud/test_upsert.cpp`:

```cpp
// Full DO NOTHING statement for Person conflicting on name.
TEST(UpsertGrammarTest, FullSqlDoNothing) {
    const std::string sql = UpsertGrammar<Person>::nothing_sql<^^Person::name>();
    EXPECT_TRUE(sql.starts_with("INSERT INTO person ")) << sql;
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}

// Full DO UPDATE statement, conflict on name, set age.
TEST(UpsertGrammarTest, FullSqlDoUpdate) {
    const std::string sql = UpsertGrammar<Person>::update_sql<^^Person::name>(
            UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age>());
    EXPECT_NE(sql.find("ON CONFLICT (name) DO UPDATE SET age=excluded.age"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20`
Expected: FAIL — `nothing_sql` / `update_sql` not declared.

- [ ] **Step 3: Add the SQL assemblers to `upsert_grammar.cppm`** (inside `struct UpsertGrammar<T>`, after `build_excluded_set_clause`):

```cpp
        import storm_orm_statements_field_names; // (top of file, with other imports)

        // INSERT INTO <table> (<non-pk cols>) VALUES (<placeholders>) ON CONFLICT
        // The INSERT prefix + RETURNING suffix are identical to InsertStatement's
        // RETURNING variant; re-derived here from reflection so this module stays
        // connection-free.
        static consteval auto build_insert_prefix() {
            ConstexprString<utilities::buffer_size::SQL_LARGE> result;
            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(FieldNameGrammar<Base>::build_non_pk_field_names_list());
            result.append(") VALUES (");
            result.append(InsertPlaceholders<T>::value); // see step 4
            result.append(")");
            return result;
        }

        // DO NOTHING full statement (compile-time constant per Target...).
        template <std::meta::info... Target> static auto nothing_sql() -> const std::string& {
            static const std::string s = [] {
                std::string out(build_insert_prefix());
                out += " ON CONFLICT ";
                out += std::string(build_conflict_target<Target...>());
                out += " DO NOTHING RETURNING ";
                out += std::string(Base::pk_name_);
                return out;
            }();
            return s;
        }

        // DO UPDATE full statement. SET clause passed in as a value (two NTTP
        // packs can't share one template parameter list).
        template <std::meta::info... Target, std::size_t N>
        static auto update_sql(const ConstexprString<N>& set_clause) -> std::string {
            std::string out(build_insert_prefix());
            out += " ON CONFLICT ";
            out += std::string(build_conflict_target<Target...>());
            out += " DO UPDATE SET ";
            out += std::string(set_clause);
            out += " RETURNING ";
            out += std::string(Base::pk_name_);
            return out;
        }
```

NOTE: `build_non_pk_field_names_list()` and `InsertPlaceholders` placeholder text live in `InsertStatement`. If they are not reachable as standalone helpers, lift the placeholder builder (`build_placeholders()` from insert.cppm lines 42-60) into `FieldNameGrammar` or a small free helper so both modules share it (DRY). Verify with:

```bash
grep -n "build_non_pk_field_names_list\|build_placeholders" src/orm/statements/field_names.cppm src/orm/statements/insert.cppm
```

If `build_placeholders` is private to `InsertStatement`, add a `consteval` `build_placeholders()` to `FieldNameGrammar<Base>` (mirroring insert.cppm:42-57, skipping `Base::primary_key_`) and have InsertStatement call it — small, test-covered refactor that removes the duplication.

- [ ] **Step 4: Build, run, verify pass**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R UpsertGrammarTest --output-on-failure`
Expected: 4 grammar tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/upsert_grammar.cppm src/orm/statements/field_names.cppm src/orm/statements/insert.cppm tests/crud/test_upsert.cpp
git commit -m "feat(upsert): full ON CONFLICT SQL assemblers (DO UPDATE / DO NOTHING) (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Compile-time validation constraints

**Files:**
- Modify: `src/orm/statements/upsert_grammar.cppm`
- Test: `tests/crud/test_upsert.cpp` (positive compile + `static_assert` checks)

**Interfaces:**
- Consumes: `storm::indexes_t<T>` (`UniqueIndex<...>::fields`), `storm::meta::is_unique`, `Task 1` `is_settable_member`.
- Produces:
  - concept `ConflictTargetUnique<T, Target...>`.
  - concept `UpsertSettable<T, SetCols...>`.

- [ ] **Step 1: Write the failing test** — append to `tests/crud/test_upsert.cpp`:

```cpp
// Positive: a single unique field IS a valid conflict target.
static_assert(storm::orm::statements::ConflictTargetUnique<Person, ^^Person::name>);
// Positive: the UniqueIndex<name, department> column set IS valid.
static_assert(storm::orm::statements::ConflictTargetUnique<Person, ^^Person::name, ^^Person::department>);
// Negative: a non-unique column is NOT a valid conflict target.
static_assert(!storm::orm::statements::ConflictTargetUnique<Person, ^^Person::age>);
// Negative: the PK is NOT settable.
static_assert(!storm::orm::statements::UpsertSettable<Person, ^^Person::id>);
// Positive: a normal column IS settable.
static_assert(storm::orm::statements::UpsertSettable<Person, ^^Person::age>);

TEST(UpsertGrammarTest, ConstraintsCompile) { SUCCEED(); }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -25`
Expected: FAIL — `ConflictTargetUnique` / `UpsertSettable` not declared.

- [ ] **Step 3: Add the concepts to `upsert_grammar.cppm`** (after `import` lines; needs `import storm_orm_indexes;` and `import storm_orm_field_attr;`):

```cpp
    // True if the Target... column set exactly matches some UniqueIndex<...> in Indexes<T>.
    template <typename T, std::meta::info... Target>
    consteval auto target_matches_unique_index() -> bool {
        constexpr std::array targets{Target...};
        bool matched = false;
        [&]<typename... Idx>(std::tuple<Idx...>*) {
            (
                    [&] {
                        if constexpr (Idx::unique) {
                            if (Idx::fields.size() == targets.size()) {
                                bool all = true;
                                for (std::size_t i = 0; i < targets.size(); ++i) {
                                    if (Idx::fields[i] != targets[i]) {
                                        all = false;
                                    }
                                }
                                if (all) {
                                    matched = true;
                                }
                            }
                        }
                    }(),
                    ...);
        }(static_cast<storm::indexes_t<T>*>(nullptr));
        return matched;
    }

    // A valid conflict target: every Target is a data member of T, AND either a
    // single FieldAttr::unique field, or the PK, or a matching UniqueIndex<...>.
    template <typename T, std::meta::info... Target>
    concept ConflictTargetUnique = ((std::meta::is_nonstatic_data_member(Target)) && ...) && (
            (sizeof...(Target) == 1 &&
             ((storm::meta::is_unique(Target) ||
               Target == BaseStatement<T>::primary_key_) && ...)) ||
            target_matches_unique_index<T, Target...>());

    // A valid SET target: a non-static data member of T that is not the PK.
    template <typename T, std::meta::info... SetCols>
    concept UpsertSettable = (UpsertGrammar<T>::template is_settable_member<SetCols>() && ...);
```

NOTE: `BaseStatement<T>::primary_key_` must be reachable. If it is `protected`, use `UpsertGrammar<T>::Base::primary_key_` via a public `static consteval auto pk() { return Base::primary_key_; }` helper, or compare against `storm::meta::find_primary_key(^^T)` if that free helper exists. Verify:

```bash
grep -n "primary_key_\|find_primary_key" src/orm/statements/base.cppm | head
```

- [ ] **Step 4: Build, run, verify pass**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R UpsertGrammarTest --output-on-failure`
Expected: all grammar tests PASS (the `static_assert`s compile).

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/upsert_grammar.cppm tests/crud/test_upsert.cpp
git commit -m "feat(upsert): ConflictTargetUnique + UpsertSettable constraints (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Execute paths on `InsertStatement`

**Files:**
- Modify: `src/orm/statements/insert.cppm`
- Test: `tests/crud/test_upsert.cpp`

**Interfaces:**
- Consumes: `UpsertGrammar<T>` (Task 1-2), `Base::bind_non_pk_fields_impl`, `Base::batch_now()`, `conn_->prepare_cached`, `Statement::step_raw/extract_int64/reset/ROW_AVAILABLE/NO_MORE_ROWS`.
- Produces (public on `InsertStatement<T,ConnType>`):
  - `execute_upsert_nothing<Target...>(const T&) -> std::expected<std::optional<std::int64_t>, Error>`
  - `execute_upsert_update<Target..., N>(const T&, const ConstexprString<N>& set_clause, /*auto_update bind plan*/) -> std::expected<std::int64_t, Error>`

  Bind contract: both bind the object's non-PK fields (same order/binder as plain INSERT). DO UPDATE additionally binds `now()` for each **unlisted auto_update** column, in `all_members_` declaration order, AFTER the VALUES params (because `excluded.col` SET targets need no params; only the trailing `col=?` auto_update tail does).

- [ ] **Step 1: Write the failing test** — append to `tests/crud/test_upsert.cpp`:

```cpp
template <typename ConnType> class UpsertTest : public StormTestFixture<Person, ConnType> {};
TYPED_TEST_SUITE(UpsertTest, DatabaseTypes);

// DO NOTHING: first insert returns an id; conflicting second returns nullopt.
TYPED_TEST(UpsertTest, DoNothingSkipsOnConflict) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Ann", .age = 30, .department = "Eng"};

    auto first = qs.insert(a).template on_conflict<^^Person::name>().nothing().execute();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first.value().has_value()) << "new row → id present";
    EXPECT_GT(first.value().value(), 0);

    Person const dup{.name = "Ann", .age = 99, .department = "Eng"};
    auto second = qs.insert(dup).template on_conflict<^^Person::name>().nothing().execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second.value().has_value()) << "conflict → nullopt (skipped)";

    // Existing row untouched (age still 30).
    auto row = qs.where(storm::orm::where::f<^^Person::name>() == std::string("Ann")).select().execute();
    ASSERT_TRUE(row.has_value());
    ASSERT_EQ(row.value().size(), 1);
    EXPECT_EQ(row.value().begin()->age, 30) << "DO NOTHING must not overwrite";
}

// DO UPDATE: conflicting insert overwrites the listed column and returns the id.
TYPED_TEST(UpsertTest, DoUpdateOverwritesListedColumn) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Bob", .age = 30, .department = "Eng"};
    auto first = qs.insert(a).template on_conflict<^^Person::name>()
                         .template update<^^Person::age>().execute();
    ASSERT_TRUE(first.has_value());
    const std::int64_t id = first.value();

    Person const upd{.name = "Bob", .age = 45, .department = "Eng"};
    auto second = qs.insert(upd).template on_conflict<^^Person::name>()
                          .template update<^^Person::age>().execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), id) << "DO UPDATE returns the conflicting row's id";

    auto row = qs.where(storm::orm::where::f<^^Person::name>() == std::string("Bob")).select().execute();
    ASSERT_TRUE(row.has_value());
    ASSERT_EQ(row.value().size(), 1);
    EXPECT_EQ(row.value().begin()->age, 45) << "DO UPDATE overwrites age";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -25`
Expected: FAIL — `on_conflict` not a member of the insert proxy.

- [ ] **Step 3: Add execute paths to `InsertStatement`** (public section, near `execute_single_optimized`, insert.cppm ~line 394). Import at top: `import storm_orm_statements_upsert_grammar;` and `import storm_orm_indexes;`.

```cpp
        // Upsert DO NOTHING — RETURNING yields the new id, or no row when skipped.
        template <std::meta::info... Target>
        [[nodiscard]] auto execute_upsert_nothing(const T& obj)
                -> std::expected<std::optional<std::int64_t>, Error> {
            const std::string& sql = UpsertGrammar<T>::template nothing_sql<Target...>();
            auto prepared = prepare_and_bind(sql, obj);
            if (!prepared) {
                return std::unexpected(prepared.error());
            }
            Statement* stmt = *prepared;
            const int rc = stmt->step_raw();
            std::optional<std::int64_t> out;
            if (rc == Statement::ROW_AVAILABLE) {
                out = stmt->extract_int64(0);
            }
            stmt->reset();
            if (rc == Statement::ROW_AVAILABLE || rc == Statement::NO_MORE_ROWS) {
                return out; // value() set when a row came back, nullopt when skipped
            }
            return std::unexpected(Error{rc, stmt->get_error_message()});
        }

        // Upsert DO UPDATE — always touches a row, so RETURNING always yields the id.
        // set_clause is the precomputed "col=excluded.col, ..." (+ auto_update tail).
        template <std::meta::info... Target, std::meta::info... SetCols>
        [[nodiscard]] auto execute_upsert_update(const T& obj)
                -> std::expected<std::int64_t, Error> {
            const std::string sql = UpsertGrammar<T>::template update_sql<Target...>(
                    UpsertGrammar<T>::template build_excluded_set_clause<SetCols...>());
            auto stmt_res = conn_->prepare_cached(sql);
            if (!stmt_res) {
                return std::unexpected(stmt_res.error());
            }
            Statement* stmt = *stmt_res;
            // (1) bind VALUES params (non-PK fields, same as plain INSERT).
            if (auto b = bind_all_fields(*stmt, obj); !b) {
                return std::unexpected(b.error());
            }
            // (2) bind the trailing auto_update now() params (excluded.col targets bind nothing).
            if (auto b = bind_upsert_auto_updates<SetCols...>(*stmt, obj); !b) {
                return std::unexpected(b.error());
            }
            const int rc = stmt->step_raw();
            if (rc == Statement::ROW_AVAILABLE) {
                std::int64_t id = stmt->extract_int64(0);
                stmt->reset();
                return id;
            }
            stmt->reset();
            return std::unexpected(Error{rc, stmt->get_error_message()});
        }

      private:
        // Bind now() for each unlisted auto_update column, in declaration order,
        // starting at the param index right after the VALUES placeholders.
        template <std::meta::info... SetCols>
        [[nodiscard]] auto bind_upsert_auto_updates(Statement& stmt, const T& /*obj*/) noexcept
                -> std::expected<void, Error> {
            int param_index = static_cast<int>(placeholders_count()) + 1; // 1-based, after VALUES
            const auto now = Base::batch_now();
            std::expected<void, Error> result{};
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (
                        [&] {
                            if (!result) {
                                return;
                            }
                            constexpr auto member = Base::all_members_[Is];
                            if constexpr (UpsertGrammar<T>::template is_unlisted_auto_update<SetCols...>(member)) {
                                if (auto r = stmt.bind_timepoint(param_index++, now); !r) {
                                    result = std::unexpected(r.error());
                                }
                            }
                        }(),
                        ...);
            }(typename Base::field_indices_t{});
            return result;
        }
```

NOTE: confirm the exact timepoint-bind call used by conditional UPDATE (`bind_timepoint` vs other). Verify and copy the real call:

```bash
grep -n "bind.*now\|bind_timepoint\|now)" src/orm/statements/update.cppm | head
```

Also add a `static consteval std::size_t placeholders_count()` to `InsertStatement` returning `field_count_ - 1` (non-PK count) if no equivalent exists.

- [ ] **Step 4: Build (proxies not wired yet — expect proxy errors only).** This task's execute methods compile standalone; the proxy `.on_conflict()` arrives in Task 5. To unit-test the execute paths now, temporarily call them directly:

```cpp
// Temporary direct-call test (delete after Task 5 wires the proxy):
TYPED_TEST(UpsertTest, DirectExecuteNothing) {
    auto conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
    storm::orm::statements::InsertStatement<Person, TypeParam> stmt{conn};
    Person const a{.name = "Zed", .age = 1, .department = "X"};
    auto r = stmt.template execute_upsert_nothing<^^Person::name>(a);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().has_value());
}
```

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R "UpsertTest.DirectExecuteNothing" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/insert.cppm tests/crud/test_upsert.cpp
git commit -m "feat(upsert): execute_upsert_nothing/update paths on InsertStatement (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Proxy chain — `on_conflict()` → `update()` / `nothing()`

**Files:**
- Modify: `src/orm/statements/insert.cppm`
- Test: `tests/crud/test_upsert.cpp`

**Interfaces:**
- Consumes: Task 4 execute paths; concepts from Task 3.
- Produces (public): `SingleQuery::on_conflict<Target...>()` and `VoidQuery::on_conflict<Target...>()` → `ConflictTarget`; `ConflictTarget::update<SetCols...>()` → terminal `expected<int64_t>`; `ConflictTarget::nothing()` → terminal `expected<optional<int64_t>>`. Each terminal proxy has `.execute()`, `.to_sql()`, `.sql()`.

- [ ] **Step 1: Write the failing test** — the two TYPED_TEST cases from Task 4 Step 1 (`DoNothingSkipsOnConflict`, `DoUpdateOverwritesListedColumn`) are the failing tests for this task. They already use the proxy chain. Also add `.sql()` goldens:

```cpp
TYPED_TEST(UpsertTest, SqlGoldenNothing) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Q", .age = 1, .department = "X"};
    const std::string sql = qs.insert(a).template on_conflict<^^Person::name>().nothing().sql();
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -25`
Expected: FAIL — `on_conflict` not a member of `SingleQuery`.

- [ ] **Step 3: Add proxies to `InsertStatement`.** Define the terminal proxies and `ConflictTarget`, then add `on_conflict` to `SingleQuery`/`VoidQuery`. If `insert.cppm` trips the 600-line hook, wrap these in `namespace detail { ... }` (pattern #438) and alias.

```cpp
        // DO UPDATE terminal proxy.
        template <std::meta::info... Target> struct ConflictTargetProxy; // fwd

        template <std::meta::info... Target, std::meta::info... SetCols>
        struct UpdateUpsertQuery {
            InsertStatement stmt;
            const T&        obj;
            [[nodiscard]] auto execute() -> std::expected<std::int64_t, Error> {
                return stmt.template execute_upsert_update<Target..., SetCols...>(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                const std::string sql = UpsertGrammar<T>::template update_sql<Target...>(
                        UpsertGrammar<T>::template build_excluded_set_clause<SetCols...>());
                return stmt.to_sql_with(sql, obj); // thin wrapper over to_sql_impl + bind_single (+auto_update)
            }
            [[nodiscard]] auto sql() -> std::string {
                return UpsertGrammar<T>::template update_sql<Target...>(
                        UpsertGrammar<T>::template build_excluded_set_clause<SetCols...>());
            }
        };

        // DO NOTHING terminal proxy.
        template <std::meta::info... Target>
        struct NothingUpsertQuery {
            InsertStatement stmt;
            const T&        obj;
            [[nodiscard]] auto execute() -> std::expected<std::optional<std::int64_t>, Error> {
                return stmt.template execute_upsert_nothing<Target...>(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql_with(UpsertGrammar<T>::template nothing_sql<Target...>(), obj);
            }
            [[nodiscard]] auto sql() -> std::string {
                return UpsertGrammar<T>::template nothing_sql<Target...>();
            }
        };

        // Intermediate: carries the conflict target, offers update<>/nothing().
        template <std::meta::info... Target>
        struct ConflictTarget {
            InsertStatement stmt;
            const T&        obj;
            template <std::meta::info... SetCols>
                requires UpsertSettable<T, SetCols...>
            [[nodiscard]] auto update() -> UpdateUpsertQuery<Target..., SetCols...> {
                return {std::move(stmt), obj};
            }
            [[nodiscard]] auto nothing() -> NothingUpsertQuery<Target...> {
                return {std::move(stmt), obj};
            }
        };
```

Add to `SingleQuery` AND `VoidQuery` (both hold `InsertStatement stmt; const T& obj;`):

```cpp
            template <std::meta::info... Target>
                requires ConflictTargetUnique<T, Target...>
            [[nodiscard]] auto on_conflict() -> ConflictTarget<Target...> {
                return {std::move(stmt), obj};
            }
```

Add the `to_sql_with` helper to `InsertStatement` (private), generalizing `to_sql_impl` to bind non-PK + auto_update tail:

```cpp
        template <std::meta::info... SetCols>
        [[nodiscard]] auto to_sql_with(const std::string& sql, const T& obj)
                -> std::expected<std::string, Error> {
            return to_sql_impl(sql, [&](Statement& s) -> std::expected<void, Error> {
                if (auto b = bind_single(s, obj); !b) {
                    return std::unexpected(b.error());
                }
                return bind_upsert_auto_updates<SetCols...>(s, obj);
            });
        }
```

(For `NothingUpsertQuery::to_sql`, SetCols is empty — auto_update tail is empty too, so the bind is just non-PK fields.)

- [ ] **Step 4: Build, run all upsert tests, verify pass**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R "UpsertTest|UpsertGrammarTest" --output-on-failure`
Expected: ALL upsert tests PASS. Delete the temporary `DirectExecuteNothing` test from Task 4.

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/insert.cppm tests/crud/test_upsert.cpp
git commit -m "feat(upsert): on_conflict/update/nothing proxy chain (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Coverage tests — FK, composite target, auto_update, errors

**Files:**
- Modify: `tests/crud/test_upsert.cpp`

**Interfaces:** Consumes the full public API from Tasks 1-5. Per the testing checklist in the spec.

- [ ] **Step 1: Write the additional tests:**

```cpp
// Composite conflict target via UniqueIndex<name, department>.
TYPED_TEST(UpsertTest, CompositeConflictTarget) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Cara", .age = 20, .department = "Eng"};
    auto first = qs.insert(a).template on_conflict<^^Person::name, ^^Person::department>()
                         .template update<^^Person::age>().execute();
    ASSERT_TRUE(first.has_value());
    Person const upd{.name = "Cara", .age = 21, .department = "Eng"};
    auto second = qs.insert(upd).template on_conflict<^^Person::name, ^^Person::department>()
                          .template update<^^Person::age>().execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), first.value());
}

// DO UPDATE with multiple SET columns.
TYPED_TEST(UpsertTest, DoUpdateMultipleColumns) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Dan", .age = 10, .salary = 100.0, .department = "Eng"};
    (void)qs.insert(a).template on_conflict<^^Person::name>()
            .template update<^^Person::age, ^^Person::salary>().execute();
    Person const upd{.name = "Dan", .age = 11, .salary = 200.0, .department = "Eng"};
    auto r = qs.insert(upd).template on_conflict<^^Person::name>()
                     .template update<^^Person::age, ^^Person::salary>().execute();
    ASSERT_TRUE(r.has_value());
    auto row = qs.where(storm::orm::where::f<^^Person::name>() == std::string("Dan")).select().execute();
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row.value().begin()->age, 11);
    EXPECT_DOUBLE_EQ(row.value().begin()->salary, 200.0);
}

// to_sql() inlines the bound params (debug helper).
TYPED_TEST(UpsertTest, ToSqlInlinesParams) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const a{.name = "Eve", .age = 5, .department = "X"};
    auto r = qs.insert(a).template on_conflict<^^Person::name>().nothing().to_sql();
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << r.value();
}
```

If a model with an `auto_update` time_point + `unique` field exists in `shared/models.h`, add a test asserting the timestamp is refreshed after a DO UPDATE upsert. Verify:

```bash
grep -n "auto_update\|time_point" shared/models.h
```

If none has BOTH, note it: an auto_update-on-upsert test needs such a model; either reuse an existing timestamp model that also has a unique field, or skip with a `// TODO(follow-up)` ONLY if no suitable model exists (document in the PR).

- [ ] **Step 2: Run, verify pass**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R UpsertTest --output-on-failure`
Expected: ALL PASS.

- [ ] **Step 3: Error-path test via mock backend** — follow `tests/errors/` pattern. Add a prepare-failure case (mock returns error from `prepare_cached`) asserting `execute()` returns `std::unexpected`. Locate the pattern:

```bash
grep -rln "mock\|MockConn\|prepare.*error" tests/errors/ | head
```

Mirror an existing insert error test, swapping in the `.on_conflict<>().nothing().execute()` call.

- [ ] **Step 4: Commit**

```bash
git add tests/crud/test_upsert.cpp
git commit -m "test(upsert): composite target, multi-col, auto_update, to_sql, errors (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: Documentation

**Files:**
- Create: `docs/guide/features/UPSERT.md`
- Modify: `docs/README.md`, `CLAUDE.md`

- [ ] **Step 1: Write `docs/guide/features/UPSERT.md`** — both patterns, the return-type table from the spec, and the get-or-create caveat (DO NOTHING returns `nullopt` on conflict, NOT the existing id; use DO UPDATE if you need the pk every time). Include the two SQL examples. Match the structure of a sibling like `docs/guide/features/JOIN_OPERATIONS.md`.

- [ ] **Step 2: Link from `docs/README.md`** — add UPSERT.md under the features list (mirror how REFERENTIAL_INTEGRITY.md is linked).

- [ ] **Step 3: Add a QuerySet API row to `CLAUDE.md`** — under the QuerySet API section, document:

```cpp
// Upsert (#205) — single-row INSERT ... ON CONFLICT (target) DO UPDATE / DO NOTHING.
// Conflict target must be a unique field / UniqueIndex (compile-time checked).
qs.insert(p).on_conflict<^^Person::name>().update<^^Person::age>().execute();   // expected<int64_t>
qs.insert(p).on_conflict<^^Person::name>().nothing().execute();                 // expected<optional<int64_t>> (nullopt = skipped)
```

- [ ] **Step 4: Commit**

```bash
git add docs/guide/features/UPSERT.md docs/README.md CLAUDE.md
git commit -m "docs(upsert): UPSERT.md + README/CLAUDE API references (#205)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 8: Sanitizers, benchmarks, full suite, PR

**Files:** none (verification + PR).

- [ ] **Step 1: Full test suite (SQLite + PG)**

Run: `ctest --preset ninja-debug --output-on-failure 2>&1 | tail -30`
Expected: all pass (PG skips gracefully if not running; if running, the upsert TYPED_TESTs must pass on PG too).

- [ ] **Step 2: Sanitizers (rule #7)**

```bash
cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan && ctest --preset ninja-asan-ubsan -R "Upsert" --output-on-failure
cmake --preset ninja-tsan && cmake --build --preset ninja-tsan && ctest --preset ninja-tsan -R "Upsert" --output-on-failure
```

Expected: no new ASAN/UBSAN/TSAN violations.

- [ ] **Step 3: Benchmark guard (rule #6)** — upsert adds NO code to existing INSERT hot paths (new methods are separate). Confirm the plain-insert benchmark is unchanged:

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/INSERT/.*' --benchmark_repetitions=5
```

Expected: INSERT numbers within noise of develop (revert any regression — there should be none, since `execute_single_optimized` is untouched).

- [ ] **Step 4: Update the issue's Definition of Done** — `gh issue view 205`, check off the delivered acceptance criteria with `gh issue edit 205 --body "..."` (`- [x]`). MySQL criterion → mark as "clear error / out of scope (compile-time backend syntax is SQLite/PG only)".

- [ ] **Step 5: Push + PR + auto-merge**

```bash
git push -u origin feature/205-upsert-on-conflict
gh pr create --base develop --title "feat(upsert): ON CONFLICT DO UPDATE / DO NOTHING (#205)" \
  --body "Closes #205

Single-row upsert via insert proxy chain. See docs/superpowers/specs/2026-06-22-upsert-design.md.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
sleep 30
```

Then `/sonarcloud-status`; if zero new issues, `gh pr checks <PR#> --watch`; once SonarCloud gate + all CI jobs green, `gh pr merge <PR#> --squash --auto`.

- [ ] **Step 6: After merge** — `gh issue close 205`, `git -C <main repo> checkout develop && git pull`, `git worktree remove .claude/worktrees/feature-205-upsert`.
