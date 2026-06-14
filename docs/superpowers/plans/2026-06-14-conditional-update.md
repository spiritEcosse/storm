# Conditional bulk UPDATE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `qs.where(cond).update<^^T::field, ...>(proto).execute()` — update every row matching a WHERE filter in one `UPDATE ... SET ... WHERE` statement, with SET columns chosen at compile time via member NTTPs and values read from a prototype object.

**Architecture:** Mirror the shipped conditional DELETE (`EraseStatement::ConditionalDeleteQuery`). A new templated `UpdateStatement::query_where<Members...>` returns a `ConditionalUpdateQuery` proxy. The SET clause is a compile-time `ConstexprString` keyed on the `<T, Members...>` pack (reusing the FK `_id` and `auto_update` logic already in `UpdateStatement`/`BaseStatement`); the WHERE body is assembled at runtime via `where::to_sql`. Binding does SET params first (reusing `BaseStatement::bind_field_at_index`) then continues the same `param_index` into the WHERE expression. Empty WHERE is refused at runtime, identical to `erase()`.

**Tech Stack:** C++26 (clang-p2996 reflection, `std::meta`), modules, GoogleTest TYPED_TEST (SQLite + PostgreSQL), CMake/Ninja presets.

---

## File Structure

- **Modify** `src/orm/statements/update.cppm` — add the conditional-update path: SET-clause builder keyed on `Members...`, `ConditionalUpdateQuery` proxy, `query_where<Members...>`, `execute_where<Members...>`, `to_sql_where<Members...>`, `ready_conditional_update<Members...>`, `empty_where_error()`.
- **Modify** `src/orm/queryset.cppm` — add the templated `update<Members...>(const T& proto)` overload (next to existing `update(obj)` / `update(span)`).
- **Create** `tests/crud/test_conditional_update.cpp` — TYPED_TEST suite (SQLite + PG), modeled on `tests/crud/test_conditional_erase.cpp`. Auto-discovered by GLOB (no CMake change).
- **Modify** `docs/features/` UPDATE/CRUD docs + `CLAUDE.md` QuerySet API section + relevant `.claude/agents/*.md` — documentation (final task).

**No new modules.** `update.cppm` already imports `storm_orm_where`? — verify in Task 1; the conditional path needs `orm::where::ExpressionVariantPtr` and `orm::where::to_sql`, which `erase.cppm` imports via `import storm_orm_where;`. Add that import to `update.cppm` if missing.

---

## Task 1: Add the conditional-update test file (RED)

**Files:**
- Create: `tests/crud/test_conditional_update.cpp`

This is the full TDD test suite. It will not compile until Tasks 2-3 add the API, so "fails" here means a compile error referencing the missing `update<...>()` overload.

- [ ] **Step 1: Write the failing test file**

```cpp
#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

// clang-tidy's readability-implicit-bool-conversion mis-fires on the GTest
// streamed-message idiom `EXPECT_*(...) << "literal"`. Band the whole file.
// NOLINTBEGIN(readability-implicit-bool-conversion)

// Conditional bulk UPDATE: qs.where(cond).update<members...>(proto)  (#403)
template <typename ConnType> class ConditionalUpdateTest : public StormTestFixture<Person, ConnType> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        std::vector<Person> seeded = {
                {.id = 1, .name = "Alice", .age = 30, .salary = 50000.0, .is_active = true, .department = "Eng"},
                {.id = 2, .name = "Bob", .age = 25, .salary = 40000.0, .is_active = false, .department = "Eng"},
                {.id = 3, .name = "Charlie", .age = 35, .salary = 60000.0, .is_active = true, .department = "Sales"},
                {.id = 4, .name = "Dave", .age = 40, .salary = 70000.0, .is_active = false, .department = "Sales"},
                {.id = 5, .name = "Eve", .age = 45, .salary = 80000.0, .is_active = true, .department = "Legacy"},
        };
        seeded[0].score = 10;
        seeded[2].score = 30;
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(seeded)));
    }

    static auto countPersons() -> int {
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.count().execute();
        return result.has_value() ? static_cast<int>(result.value()) : -1;
    }

    // Fetch a single person by id (asserts exactly one row).
    static auto getPerson(int person_id) -> Person {
        using storm::orm::where::f;
        const storm::QuerySet<Person, ConnType> qs;
        auto rows = qs.where(f<^^Person::id>() == person_id).select().execute();
        EXPECT_TRUE(rows.has_value());
        EXPECT_EQ(rows.value().size(), 1u);
        return *rows.value().begin();
    }
};

TYPED_TEST_SUITE(ConditionalUpdateTest, DatabaseTypes);

TYPED_TEST(ConditionalUpdateTest, Setup) {
    EXPECT_EQ(this->countPersons(), 5);
}

// --- SET coverage: single column, multiple columns, each type --------------
TYPED_TEST(ConditionalUpdateTest, SingleSetColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() == 30)
                        .template update<^^Person::salary>(Person{.salary = 99000.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 99000.0);
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 40000.0) << "unmatched row unchanged";
}

TYPED_TEST(ConditionalUpdateTest, MultipleSetColumns) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::department>() == "Eng")
                        .template update<^^Person::salary, ^^Person::is_active>(
                                Person{.salary = 12345.0, .is_active = true})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 12345.0);
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 12345.0);
    EXPECT_TRUE(this->getPerson(2).is_active) << "Bob flipped to active";
    EXPECT_DOUBLE_EQ(this->getPerson(3).salary, 60000.0) << "Sales row unchanged";
}

TYPED_TEST(ConditionalUpdateTest, SetIntColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::id>() == 1)
                        .template update<^^Person::age>(Person{.age = 77})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).age, 77);
}

TYPED_TEST(ConditionalUpdateTest, SetStringColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::id>() == 1)
                        .template update<^^Person::department>(Person{.department = "Relocated"})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).department, "Relocated");
}

TYPED_TEST(ConditionalUpdateTest, SetBoolColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::id>() == 1)
                        .template update<^^Person::is_active>(Person{.is_active = false})
                        .execute()
                        .has_value());
    EXPECT_FALSE(this->getPerson(1).is_active);
}

// --- WHERE coverage: 6 operators -------------------------------------------
TYPED_TEST(ConditionalUpdateTest, WhereEqual) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() == 30)
                        .template update<^^Person::age>(Person{.age = 31})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).age, 31);
}

TYPED_TEST(ConditionalUpdateTest, WhereNotEqual) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() != 30)
                        .template update<^^Person::department>(Person{.department = "X"})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).department, "Eng") << "age==30 unchanged";
    EXPECT_EQ(this->getPerson(2).department, "X");
}

TYPED_TEST(ConditionalUpdateTest, WhereGreater) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() > 35)
                        .template update<^^Person::is_active>(Person{.is_active = false})
                        .execute()
                        .has_value());
    EXPECT_FALSE(this->getPerson(4).is_active);
    EXPECT_FALSE(this->getPerson(5).is_active);
    EXPECT_TRUE(this->getPerson(3).is_active) << "age 35 not > 35";
}

TYPED_TEST(ConditionalUpdateTest, WhereGreaterEqual) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() >= 35)
                        .template update<^^Person::age>(Person{.age = 0})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(3).age, 0);
    EXPECT_EQ(this->getPerson(1).age, 30) << "age 30 untouched";
}

TYPED_TEST(ConditionalUpdateTest, WhereLess) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() < 30)
                        .template update<^^Person::age>(Person{.age = 1})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(2).age, 1) << "Bob 25 updated";
    EXPECT_EQ(this->getPerson(1).age, 30) << "age 30 untouched";
}

TYPED_TEST(ConditionalUpdateTest, WhereLessEqual) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() <= 30)
                        .template update<^^Person::age>(Person{.age = 2})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).age, 2);
    EXPECT_EQ(this->getPerson(2).age, 2);
    EXPECT_EQ(this->getPerson(3).age, 35) << "age 35 untouched";
}

// --- WHERE coverage: IN / BETWEEN / LIKE / NULL ----------------------------
TYPED_TEST(ConditionalUpdateTest, WhereIn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>().in(25, 35))
                        .template update<^^Person::is_active>(Person{.is_active = false})
                        .execute()
                        .has_value());
    EXPECT_FALSE(this->getPerson(2).is_active);
    EXPECT_FALSE(this->getPerson(3).is_active);
}

TYPED_TEST(ConditionalUpdateTest, WhereBetween) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>().between(30, 40))
                        .template update<^^Person::salary>(Person{.salary = 1.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 1.0);
    EXPECT_DOUBLE_EQ(this->getPerson(4).salary, 1.0);
    EXPECT_DOUBLE_EQ(this->getPerson(5).salary, 80000.0) << "age 45 untouched";
}

TYPED_TEST(ConditionalUpdateTest, WhereLike) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::department>().like("Eng%"))
                        .template update<^^Person::salary>(Person{.salary = 5.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 5.0);
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 5.0);
}

TYPED_TEST(ConditionalUpdateTest, WhereIsNull) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::score>().is_null())
                        .template update<^^Person::salary>(Person{.salary = 7.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 7.0) << "NULL-score row updated";
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 50000.0) << "non-NULL score untouched";
}

TYPED_TEST(ConditionalUpdateTest, WhereIsNotNull) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::score>().is_not_null())
                        .template update<^^Person::salary>(Person{.salary = 8.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 8.0);
    EXPECT_DOUBLE_EQ(this->getPerson(3).salary, 8.0);
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 40000.0) << "NULL score untouched";
}

// --- WHERE coverage: AND / OR / nested -------------------------------------
TYPED_TEST(ConditionalUpdateTest, WhereAnd) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::department>() == "Sales" && f<^^Person::is_active>() == false)
                        .template update<^^Person::age>(Person{.age = 100})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(4).age, 100) << "Dave (Sales, inactive)";
    EXPECT_EQ(this->getPerson(3).age, 35) << "Charlie (Sales, active) untouched";
}

TYPED_TEST(ConditionalUpdateTest, WhereOr) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() < 26 || f<^^Person::age>() > 44)
                        .template update<^^Person::salary>(Person{.salary = 3.0})
                        .execute()
                        .has_value());
    EXPECT_DOUBLE_EQ(this->getPerson(2).salary, 3.0);
    EXPECT_DOUBLE_EQ(this->getPerson(5).salary, 3.0);
    EXPECT_DOUBLE_EQ(this->getPerson(1).salary, 50000.0);
}

TYPED_TEST(ConditionalUpdateTest, WhereNested) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where((f<^^Person::department>() == "Eng" && f<^^Person::age>() < 28) ||
                         f<^^Person::salary>() > 75000.0)
                        .template update<^^Person::age>(Person{.age = 9})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(2).age, 9) << "Bob (Eng, 25)";
    EXPECT_EQ(this->getPerson(5).age, 9) << "Eve (salary 80k)";
    EXPECT_EQ(this->getPerson(1).age, 30) << "Alice untouched";
}

// --- Chained where() (AND-combined) ----------------------------------------
TYPED_TEST(ConditionalUpdateTest, ChainedWhere) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::department>() == "Eng")
                        .where(f<^^Person::is_active>() == true)
                        .template update<^^Person::age>(Person{.age = 88})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(1).age, 88) << "Alice (Eng, active)";
    EXPECT_EQ(this->getPerson(2).age, 25) << "Bob (Eng, inactive) untouched";
}

// --- Result-set sizes ------------------------------------------------------
TYPED_TEST(ConditionalUpdateTest, EmptyResultNoOp) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    ASSERT_TRUE(qs.where(f<^^Person::age>() > 1000)
                        .template update<^^Person::age>(Person{.age = 1})
                        .execute()
                        .has_value())
            << "Matching nothing is a successful no-op";
    EXPECT_EQ(this->getPerson(1).age, 30) << "no rows changed";
}

TYPED_TEST(ConditionalUpdateTest, LargeResultSet) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    std::vector<Person> batch;
    batch.reserve(150);
    for (int i = 100; i < 250; ++i) {
        batch.emplace_back(Person{.id = i, .name = std::format("Bulk{}", i), .age = 50, .department = "Bulk"});
    }
    ASSERT_TRUE(qs.insert(std::span<const Person>(batch)).execute().has_value());
    ASSERT_TRUE(qs.where(f<^^Person::department>() == "Bulk")
                        .template update<^^Person::age>(Person{.age = 51})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->getPerson(100).age, 51);
    EXPECT_EQ(this->getPerson(249).age, 51);
}

// --- Safety: empty WHERE refuses the full-table write ----------------------
TYPED_TEST(ConditionalUpdateTest, EmptyWhereIsRefused) {
    storm::QuerySet<Person, TypeParam> qs;
    auto result = qs.template update<^^Person::age>(Person{.age = 0}).execute();
    ASSERT_FALSE(result.has_value()) << "update<...>() with no where() must refuse";
    EXPECT_EQ(this->getPerson(1).age, 30) << "Table must be untouched";
}

TYPED_TEST(ConditionalUpdateTest, ToSqlEmptyWhereRefused) {
    storm::QuerySet<Person, TypeParam> qs;
    auto sql = qs.template update<^^Person::age>(Person{.age = 0}).to_sql();
    EXPECT_FALSE(sql.has_value()) << "to_sql() on an unfiltered update<...>() must also refuse";
}

// --- to_sql() shape --------------------------------------------------------
TYPED_TEST(ConditionalUpdateTest, ToSqlShape) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    auto sql = qs.where(f<^^Person::age>() > 30)
                       .template update<^^Person::salary>(Person{.salary = 1.0})
                       .to_sql();
    ASSERT_TRUE(sql.has_value());
    EXPECT_TRUE(sql.value().contains("UPDATE")) << sql.value();
    EXPECT_TRUE(sql.value().contains("SET")) << sql.value();
    EXPECT_TRUE(sql.value().contains("salary")) << sql.value();
    EXPECT_TRUE(sql.value().contains("WHERE")) << sql.value();
}

// --- FK SET column: emits <name>_id ----------------------------------------
TYPED_TEST(ConditionalUpdateTest, FkSetColumnToSql) {
    using storm::orm::where::f;
    storm::QuerySet<Message, TypeParam> mqs;
    auto sql = mqs.where(f<^^Message::id>() == 1)
                       .template update<^^Message::sender>(Message{.sender = Person{.id = 7}})
                       .to_sql();
    ASSERT_TRUE(sql.has_value());
    EXPECT_TRUE(sql.value().contains("sender_id")) << sql.value();
}

// --- auto_update field auto-included ---------------------------------------
TYPED_TEST(ConditionalUpdateTest, AutoUpdateFieldIncludedInSql) {
    using storm::orm::where::f;
    storm::QuerySet<TimestampedRecord, TypeParam> tqs;
    auto sql = tqs.where(f<^^TimestampedRecord::id>() == 1)
                       .template update<^^TimestampedRecord::name>(TimestampedRecord{.name = "renamed"})
                       .to_sql();
    ASSERT_TRUE(sql.has_value());
    EXPECT_TRUE(sql.value().contains("name")) << sql.value();
    EXPECT_TRUE(sql.value().contains("updated_at")) << sql.value()
            << "auto_update field must be auto-appended to SET";
}

// --- model without timestamp: no extra column ------------------------------
TYPED_TEST(ConditionalUpdateTest, NoTimestampNoExtraColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    auto sql = qs.where(f<^^Person::id>() == 1)
                       .template update<^^Person::age>(Person{.age = 5})
                       .to_sql();
    ASSERT_TRUE(sql.has_value());
    // SET has exactly one assignment ("age=...") — no auto column appended.
    EXPECT_EQ(std::ranges::count(sql.value(), '='), 1) << sql.value();
}

// NOLINTEND(readability-implicit-bool-conversion)
```

- [ ] **Step 2: Verify it fails to compile (RED)**

Run: `cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -30`
Expected: FAIL — compile error in `test_conditional_update.cpp` about no matching `update<...>()` member / `template update` not found. This proves the tests exercise unimplemented API.

- [ ] **Step 3: Commit the failing test**

```bash
git add tests/crud/test_conditional_update.cpp
git commit -m "test(orm): conditional bulk UPDATE — failing TYPED_TEST suite (#403)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

(The pre-commit hook runs `ctest`; a non-building tree will block the commit. If so, commit with the test file but expect the hook to fail at the test stage — in that case, proceed to Task 2/3 first and commit the test together with the implementation in Task 3's commit. Prefer: implement Tasks 2-3, then run the suite, then a single combined commit.)

---

## Task 2: SET-clause builder + conditional path in `UpdateStatement`

**Files:**
- Modify: `src/orm/statements/update.cppm`

- [ ] **Step 1: Ensure the where import is present**

At the top of `update.cppm`, confirm these imports exist; add the missing one:

```cpp
import storm_orm_where;   // ADD if not already present — needed for ExpressionVariantPtr + to_sql
```

(`erase.cppm` already imports it; `update.cppm` currently does not.)

- [ ] **Step 2: Add a member→index helper and SET-clause builder**

Inside `class UpdateStatement`, in the private section (after `field_assignments_`), add:

```cpp
        // Index of a member info within Base::all_members_ (compile-time).
        static consteval auto index_of_member(std::meta::info member) -> std::size_t {
            for (std::size_t i = 0; i < Base::all_members_.size(); ++i) {
                if (Base::all_members_[i] == member) {
                    return i;
                }
            }
            std::unreachable(); // guarded by SettableMember concept at the call site
        }

        // Append "<name>=?" (or "<name>_id=?" for FK fields) for one member.
        template <typename Buf> static consteval auto append_one_assignment(Buf& buf, std::meta::info member) -> void {
            buf.append(std::meta::identifier_of(member));
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                buf.append("_id=?");
            } else {
                buf.append("=?");
            }
        }

        // Is `member` an auto_update field NOT already present in the explicit pack?
        template <std::meta::info... Members>
        static consteval auto is_unlisted_auto_update(std::meta::info member) -> bool {
            if (!Base::is_auto_update_field(member)) {
                return false;
            }
            return ((member != Members) && ...); // true only if not in the pack
        }

        // Build the SET clause for the explicit Members... pack, then append any
        // auto_update field of T that the caller did not list. The column ORDER
        // here is the canonical bind order used by bind_conditional_set().
        template <std::meta::info... Members> static consteval auto build_conditional_set_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;
            // 1) explicit members, in the given order
            (
                    [&] {
                        if (!first) {
                            result.append(", ");
                        }
                        append_one_assignment(result, Members);
                        first = false;
                    }(),
                    ...
            );
            // 2) auto_update fields not already listed (stamped now() at bind time)
            for (const auto& member : Base::all_members_) {
                if (is_unlisted_auto_update<Members...>(member)) {
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
```

Note: `std::meta::annotation_of_type` is used the same way `build_field_assignments` already does in this file — safe here because `Members` are re-derived constants, not BMI-crossing infos.

- [ ] **Step 3: Add a SettableMember concept (compile-time guard)**

Above the class (in the same `export namespace`), or as a `static consteval` predicate, add a guard so a non-member or PK in the SET list is a clear compile error. Add to the private section:

```cpp
        // Each SET target must be a non-static data member of T and not the PK.
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            if (!std::meta::is_nonstatic_data_member(Member)) {
                return false;
            }
            return Member != Base::primary_key_;
        }
```

- [ ] **Step 4: Add `empty_where_error()` and the conditional SQL builder**

In the public section (mirroring `EraseStatement`):

```cpp
        // Refusal when update<...>() is called with no WHERE filter.
        static auto empty_where_error() -> std::unexpected<Error> {
            return std::unexpected(
                    Error{-1, "update() requires a WHERE clause; use update_all() to update all rows"});
        }

        // Runtime SQL: "UPDATE <table> SET <set_clause> WHERE <expr>".
        // SET clause is compile-time; WHERE body is dynamic.
        template <std::meta::info... Members>
        static auto build_conditional_update_sql(const orm::where::ExpressionVariant& expr) -> std::string {
            static const std::string set_clause = std::string(build_conditional_set_clause<Members...>());
            std::string sql;
            sql.reserve(Base::table_name_.size() + set_clause.size() + 32);
            sql += "UPDATE ";
            sql += Base::table_name_;
            sql += " SET ";
            sql += set_clause;
            sql += " WHERE ";
            sql += orm::where::to_sql(expr);
            return sql;
        }
```

- [ ] **Step 5: Build & verify it compiles**

Run: `cmake --build --preset ninja-debug --target storm 2>&1 | tail -20`
Expected: builds (no test wiring yet; just confirm the module compiles). If `index_of_member`/`is_settable_member` are unused so far, that is fine — they are used in Task 3.

- [ ] **Step 6: Commit (combined with Task 3 — do not commit a half-wired statement)**

Skip an isolated commit here; Task 3 wires binding + the proxy and is committed together. (The pre-commit hook runs tests; an unreachable code path with no caller still compiles, but committing mid-feature risks a failing hook. Commit at end of Task 3.)

---

## Task 3: Binding + `ConditionalUpdateQuery` proxy + `query_where`

**Files:**
- Modify: `src/orm/statements/update.cppm`

- [ ] **Step 1: Add SET binding that matches the SET-clause order**

In the protected/private section, add a binder that binds the explicit members (by their `all_members_` index, reusing `bind_field_at_index` so FK `_id` and value-type dispatch match single-row UPDATE), then the unlisted auto_update fields (stamped `now()`), advancing `param_index`:

```cpp
        // Bind the SET values in the SAME order build_conditional_set_clause emits them:
        // (1) explicit Members... from `proto`, (2) unlisted auto_update fields stamped now().
        // param_index advances; WHERE binding continues from where this leaves off.
        template <std::meta::info... Members>
        [[nodiscard]] auto bind_conditional_set(Statement* stmt, const T& proto, int& param_index) noexcept
                -> std::expected<void, Error> {
            const auto now = Base::batch_now(); // one clock read; compiles away if no timestamp field

            // (1) explicit members, in pack order. IsUpdate=true so an explicitly-listed
            //     auto_update member is stamped now() (matches single-row UPDATE), and a
            //     listed auto_create member binds the proto's stored value.
            std::expected<void, Error> result{};
            ((result = Base::template bind_field_at_index<ConnType, index_of_member(Members), false, true>(
                      stmt, proto, param_index, now),
              result.has_value()) &&
             ...);
            if (!result) {
                return result;
            }

            // (2) unlisted auto_update fields — stamp now() in declaration order.
            for (std::size_t i = 0; i < Base::all_members_.size(); ++i) {
                // Plain runtime loop: dispatch each index via a compile-time fold.
                auto step = bind_unlisted_auto_update_at<Members...>(stmt, i, param_index, now);
                if (!step) {
                    return step;
                }
            }
            return {};
        }
```

Add the per-index helper that only binds when the member at runtime index `i` is an unlisted auto_update field. Because `bind_one`/`bind_value_by_type` need a value and the auto_update value is `now()`, bind `now()` directly:

```cpp
        // For each compile-time index, if all_members_[Index] is an unlisted auto_update
        // field and matches runtime `i`, bind now() at param_index. Folded over field_indices_t.
        template <std::meta::info... Members, std::size_t... Is>
        [[nodiscard]] auto bind_unlisted_auto_update_fold(
                Statement* stmt, std::size_t i, int& param_index, std::chrono::system_clock::time_point now,
                std::index_sequence<Is...> /*unused*/) noexcept -> std::expected<void, Error> {
            std::expected<void, Error> result{};
            (
                    [&] {
                        if (result.has_value() && i == Is &&
                            is_unlisted_auto_update<Members...>(Base::all_members_[Is])) {
                            result = Base::template bind_one<ConnType>(stmt, param_index, now);
                        }
                    }(),
                    ...);
            return result;
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto bind_unlisted_auto_update_at(
                Statement* stmt, std::size_t i, int& param_index,
                std::chrono::system_clock::time_point now) noexcept -> std::expected<void, Error> {
            return bind_unlisted_auto_update_fold<Members...>(stmt, i, param_index, now,
                                                              typename Base::field_indices_t{});
        }
```

(`bind_one` and `bind_field_at_index` are protected members of `BaseStatement`; `UpdateStatement` already calls `Base::template bind_field_at_index<...>` in `inline_bind_all_fields`, so access is established.)

- [ ] **Step 2: Add `ready_conditional_update`, `execute_where`, `to_sql_where`**

```cpp
        // Empty-WHERE check + prepare + SET-then-WHERE bind. Shared by execute/to_sql.
        template <std::meta::info... Members>
        [[nodiscard]] auto ready_conditional_update(const T& proto,
                                                    const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<Statement*, Error> {
            if (!where_expr) [[unlikely]] {
                return empty_where_error();
            }
            auto prepare_result = conn_->prepare_cached(build_conditional_update_sql<Members...>(*where_expr));
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt = *prepare_result;
            stmt->reset();
            int param_index = 1;
            if (auto set_result = bind_conditional_set<Members...>(stmt, proto, param_index); !set_result) {
                return std::unexpected(set_result.error());
            }
            // WHERE params continue from param_index (mirrors bind_having_params continuation).
            if (auto where_result =
                        Base::template bind_having_params<Statement, Error>(stmt, where_expr, param_index);
                !where_result) {
                return std::unexpected(where_result.error());
            }
            return stmt;
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto execute_where(const T& proto, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            auto stmt_result = ready_conditional_update<Members...>(proto, where_expr);
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            if (auto exec_result = (*stmt_result)->execute(); !exec_result) {
                return std::unexpected(exec_result.error());
            }
            return {};
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto to_sql_where(const T& proto, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<std::string, Error> {
            auto stmt_result = ready_conditional_update<Members...>(proto, where_expr);
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            return (*stmt_result)->expanded_sql();
        }
```

Note: `bind_having_params` already takes `(stmt, expr, int& param_index)` and continues from the given index — exactly the continuation the issue calls for. Reuse it rather than adding a new entry point.

- [ ] **Step 3: Add the `ConditionalUpdateQuery` proxy + `query_where`**

In the public section, after the existing `query(...)` overloads:

```cpp
        // Conditional bulk UPDATE (#403): UPDATE <table> SET <set> WHERE <where_expr>.
        // SET columns are the Members... pack; values come from `proto`. A null where_expr
        // is refused at execute()/to_sql() time so a dropped where() cannot write the whole
        // table (update_all() — not yet implemented — would be the explicit full-table path).
        template <std::meta::info... Members> struct ConditionalUpdateQuery {
            UpdateStatement                  stmt;
            const T&                         proto;
            orm::where::ExpressionVariantPtr where_expr;
            [[nodiscard]] auto               execute() -> std::expected<void, Error> {
                return stmt.template execute_where<Members...>(proto, where_expr);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.template to_sql_where<Members...>(proto, where_expr);
            }
        };

        template <std::meta::info... Members>
            requires(sizeof...(Members) > 0 && (is_settable_member<Members>() && ...))
        auto query_where(const T& proto [[clang::lifetimebound]],
                         orm::where::ExpressionVariantPtr where_expr) -> ConditionalUpdateQuery<Members...> {
            return {std::move(*this), proto, std::move(where_expr)};
        }
```

- [ ] **Step 4: Build the library**

Run: `cmake --build --preset ninja-debug --target storm 2>&1 | tail -20`
Expected: builds clean.

- [ ] **Step 5: (defer test run to Task 4 after queryset wiring)**

---

## Task 4: Wire `QuerySet::update<Members...>` + run the suite (GREEN)

**Files:**
- Modify: `src/orm/queryset.cppm:276-283` (next to existing `update` overloads)

- [ ] **Step 1: Add the templated overload**

After the existing bulk `update(std::span<const T>...)` overload, add:

```cpp
        // Conditional bulk UPDATE (#403) — updates rows matching the current where() filter.
        // SET columns are the Members... NTTPs; values come from `proto`.
        // qs.where(cond).update<^^T::a, ^^T::b>(T{...}).execute() -> std::expected<void, Error>.
        // With NO where() set, .execute()/.to_sql() return std::unexpected and refuse to
        // write the whole table.
        template <std::meta::info... Members>
            requires(sizeof...(Members) > 0)
        [[nodiscard]] auto update(const T& proto [[clang::lifetimebound]]) {
            return orm::statements::UpdateStatement<T, ConnType>(conn_)
                    .template query_where<Members...>(proto, where_expr_);
        }
```

- [ ] **Step 2: Build everything (lib + tests)**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -30`
Expected: builds clean, including `test_conditional_update.cpp`.

- [ ] **Step 3: Run the new suite (GREEN)**

Run: `./build/debug/tests/storm_tests --gtest_filter="ConditionalUpdateTest*/*" 2>&1 | tail -40`
Expected: all tests PASS on both `Sqlite` and `Postgres` type params (PG skips gracefully if not running).

- [ ] **Step 4: Run the full suite (no regressions)**

Run: `ctest --preset ninja-debug 2>&1 | tail -20`
Expected: all tests pass.

- [ ] **Step 5: Commit implementation + tests together**

```bash
git add src/orm/statements/update.cppm src/orm/queryset.cppm tests/crud/test_conditional_update.cpp
git commit -m "feat(orm): conditional bulk UPDATE — qs.where(cond).update<members...>(proto) (#403)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Sanitizers + coverage + benchmark

**Files:** none (verification only)

- [ ] **Step 1: ASAN+UBSAN**

Run: `cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan && ctest --preset ninja-asan-ubsan 2>&1 | tail -20`
Expected: no new ASAN/UBSAN findings. (Per memory: sanitizers run every session; this is mandatory after code changes.)

- [ ] **Step 2: TSAN**

Run: `cmake --preset ninja-tsan && cmake --build --preset ninja-tsan && ctest --preset ninja-tsan 2>&1 | tail -20`
Expected: no data races.

- [ ] **Step 3: Coverage 100% on new code**

Run: `cmake --build --preset ninja-debug-coverage --target coverage 2>&1 | tail -30`
Expected: `update.cppm` and `queryset.cppm` new lines covered. If a template-instantiation branch is uncovered (per the unhit-template-instantiation memory), add a targeted test (e.g. an FK SET column `execute()` path, or an `auto_update` `execute()` path that asserts the timestamp advanced) rather than excluding.

- [ ] **Step 4: Benchmark (no regression on existing UPDATE paths)**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release && ./build/release/benchmarks/storm_bench --benchmark_filter='Storm/UPDATE/.*' 2>&1 | tail -20`
Expected: existing single/bulk UPDATE benchmarks unchanged (the conditional path is new code on a cold path; it must not touch the hot single/bulk binder). If any slowdown on the existing UPDATE paths, investigate — the new template members must not perturb `inline_bind_all_fields`.

- [ ] **Step 5: clang-tidy clean on the diff**

Run: `bash scripts/run_clang_tidy.sh --diff 2>&1 | tee /tmp/tidy_403.txt | tail -30`
Expected: no findings. Watch for S6177 (`using enum`), S6009 (`string_view` params), S3656 (GTest protected → public). Fix inline if any.

---

## Task 6: Documentation

**Files:**
- Modify: `CLAUDE.md` (QuerySet API section — add the conditional UPDATE example next to the conditional DELETE / `erase()` notes)
- Modify: `docs/features/` UPDATE or CRUD doc (find the file documenting `update()` / conditional DELETE; add a conditional-UPDATE section mirroring the DELETE one)
- Modify: `.claude/agents/*.md` if any agent file documents the `update()` API or the conditional DELETE pattern (grep for `erase()` / `ConditionalDelete` / `qs.where(cond)`)

- [ ] **Step 1: Find the docs that mention conditional DELETE**

Run: `grep -rln "qs.where(cond).erase\|ConditionalDelete\|conditional bulk DELETE\|erase_all" docs/ CLAUDE.md .claude/`
Expected: a list of files to update with the parallel UPDATE content.

- [ ] **Step 2: Add the conditional UPDATE docs**

In `CLAUDE.md` QuerySet API section, after the `erase()` notes, add:

```markdown
// Conditional bulk UPDATE (#403) — updates rows matching the current where().
// SET columns are compile-time member NTTPs; values come from a prototype.
qs.where(f<^^Person::salary>() < 50000)
  .update<^^Person::salary, ^^Person::is_active>(Person{.salary=60000, .is_active=true})
  .execute();   // -> std::expected<void, Error>
// Empty where() is refused (no full-table write). auto_update fields auto-stamped now().
```

Mirror the conditional-DELETE prose in the feature doc, covering: the member-NTTP SET DSL, FK `_id` columns, auto_update auto-inclusion, empty-WHERE refusal, void return, out-of-scope (`update_all`, row count, column-relative SET).

- [ ] **Step 3: Commit docs**

```bash
git add CLAUDE.md docs/ .claude/
git commit -m "docs(orm): document conditional bulk UPDATE (#403)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: PR + SonarCloud + merge

- [ ] **Step 1: Check off the issue's Definition of done**

Run `gh issue view 403`; if it has checkbox subtasks, edit the body with `- [x]` for each delivered item (`gh issue edit 403 --body "..."`).

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin feature/403-conditional-update
gh pr create --base develop --title "feat(orm): conditional bulk UPDATE — qs.where(cond).update<members...>(proto) (#403)" \
  --body "Closes #403

Follow-up to #198 (conditional DELETE, PR #402). Adds qs.where(cond).update<^^T::field,...>(proto):
- Compile-time member-NTTP SET DSL; values read from a prototype object
- FK columns emit <name>_id; auto_update timestamps auto-stamped now()
- Empty WHERE refused at runtime (mirrors erase()); returns std::expected<void, Error>

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

- [ ] **Step 3: SonarCloud gate**

Wait 30s, then run `/sonarcloud-status`. If zero new issues + zero duplications, proceed; else fix all, push, re-check until clean.

- [ ] **Step 4: CI + merge**

`gh pr checks <PR#> --watch`. After SonarCloud gate AND all CI jobs pass: `gh pr merge --squash`.

- [ ] **Step 5: Close issue + return to develop**

```bash
gh issue close 403
git checkout develop && git pull
```
