# Multi-m2m Join (`join<m2m_a, m2m_b>()`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eager-load multiple many-to-many relations of one model in a single `join<>()`/`left_join<>()` call (issue #392), each relation as one extra Q2 against the shared Q1 base set.

**Architecture:** `JoinStatementWrapper` carries a `std::vector<M2MRelation>` of per-relation Q2/stitch fn-pointers; Q1 stays wrapper-level (its text only depends on the base model). `M2MJoinStatement` stays single-field; its monolithic complete-SQL fragments are split so the variadic factory can assemble a chained N-junction join with unique aliases (relation *i* → `t{2+2i}`/`t{3+2i}`). `execute_m2m_2query` loops Q2s against one pk→entity map; INNER drops entities empty in ANY inner relation.

**Tech Stack:** C++26 reflection (clang-p2996), C++26 pack indexing, GoogleTest TYPED_TEST (SQLite + PostgreSQL), mock-sqlite error harness.

**Spec:** `docs/superpowers/specs/2026-06-12-multi-m2m-join-design.md`

**Commit strategy note:** the pre-commit hook builds and runs tests, so failing-first tests
cannot be committed alone. Workflow: write tests (uncommitted) → verify they fail to
compile → implement → verify green → commit tests + implementation together.

---

### Task 1: Failing tests — models + `test_many_to_many_multi.cpp`

**Files:**
- Modify: `tests/query/test_m2m_models.h` (append models + seeder)
- Create: `tests/query/test_many_to_many_multi.cpp`

- [ ] **Step 1.1: Add two-relation models to `test_m2m_models.h`**

Append before the closing `namespace storm::test` block (after the `Album` model):

```cpp
// Multi-relation model (#392): two auto-junction m2m fields on one owner.
struct Club {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
};

struct Member {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::meta::many_to_many]] std::vector<Course> courses;
    [[= storm::meta::many_to_many]] std::vector<Club> clubs;
};
```

And inside `namespace storm::test`, after `seed_students`:

```cpp
// Seeds (#392 multi-relation): Ann→courses[Math,Physics] clubs[Chess],
// Ben→courses[Math] clubs[], Cat→courses[] clubs[Chess,Robotics], Dan→[] [].
template <typename ConnType> inline auto seed_members() -> void {
    storm::QuerySet<Member, ConnType> mqs;
    std::vector<Member> const members = {{.name = "Ann", .age = 20},
                                         {.name = "Ben", .age = 22},
                                         {.name = "Cat", .age = 25},
                                         {.name = "Dan", .age = 30}};
    ASSERT_TRUE(mqs.insert(std::span<const Member>(members)).execute().has_value());

    storm::QuerySet<Course, ConnType> cqs;
    std::vector<Course> const courses = {{.title = "Math"}, {.title = "Physics"}};
    ASSERT_TRUE(cqs.insert(std::span<const Course>(courses)).execute().has_value());

    storm::QuerySet<Club, ConnType> kqs;
    std::vector<Club> const clubs = {{.name = "Chess"}, {.name = "Robotics"}};
    ASSERT_TRUE(kqs.insert(std::span<const Club>(clubs)).execute().has_value());

    auto conn = storm::QuerySet<Member, ConnType>::get_default_connection();
    for (const auto* pair : {"(1, 1)", "(1, 2)", "(2, 1)"}) {
        ASSERT_TRUE(conn->execute(std::format("INSERT INTO Member_Course (Member_id, Course_id) VALUES {}", pair))
                            .has_value());
    }
    for (const auto* pair : {"(1, 1)", "(3, 1)", "(3, 2)"}) {
        ASSERT_TRUE(conn->execute(std::format("INSERT INTO Member_Club (Member_id, Club_id) VALUES {}", pair))
                            .has_value());
    }
}
```

- [ ] **Step 1.2: Create `tests/query/test_many_to_many_multi.cpp`**

Follow the header order of `test_many_to_many.cpp` exactly (plf_hive before imports,
model headers after `import storm;`):

```cpp
#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

using storm::QuerySet;

#include "test_models.h"     // NOSONAR cpp:S954
#include "test_m2m_models.h" // NOSONAR cpp:S954

// ============================================================================
// Compile-time: multi-m2m join API surface (#392)
// ============================================================================

// Probe: is join<Fields...>() well-formed on QuerySet<M>?
template <typename M, std::meta::info... Fields>
concept CanJoin = requires(storm::QuerySet<M> qs) { qs.template join<Fields...>(); };

static_assert(CanJoin<Member, ^^Member::courses, ^^Member::clubs>, "multi m2m join must be accepted");
static_assert(CanJoin<Member, ^^Member::clubs>, "single m2m join still accepted");
// Duplicate m2m fields would silently double-fill one container — rejected.
static_assert(!CanJoin<Member, ^^Member::courses, ^^Member::courses>);
// Mixed FK + m2m stays out of scope (#392).
static_assert(!CanJoin<Message, ^^Message::sender, ^^Member::courses>);

// ============================================================================
// SQL shape: Q1 + one Q2 per relation, "; "-separated (#392)
// ============================================================================

template <typename ConnType> class MultiM2MSqlTest : public StormTestFixture<Member, ConnType, Course, Club> {};

TYPED_TEST_SUITE(MultiM2MSqlTest, DatabaseTypes);

TYPED_TEST(MultiM2MSqlTest, MultiM2MJoinSqlShape) {
    QuerySet<Member, TypeParam> qs;
    auto sql = qs.template join<^^Member::courses, ^^Member::clubs>().select().sql();
    // Q1 — base entities, once.
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Member; ")) << sql;
    // Q2a — courses relation, filtered by the base subquery.
    EXPECT_TRUE(sql.contains("FROM Member_Course t2 INNER JOIN Course t3 ON t2.Course_id = t3.id")) << sql;
    EXPECT_TRUE(sql.contains("WHERE t2.Member_id IN (SELECT id FROM Member)")) << sql;
    // Q2b — clubs relation, filtered by the same base subquery.
    EXPECT_TRUE(sql.contains("FROM Member_Club t2 INNER JOIN Club t3 ON t2.Club_id = t3.id")) << sql;
    // Exactly three statements.
    EXPECT_EQ(std::ranges::count(sql, ';'), 2) << sql;
}

TYPED_TEST(MultiM2MSqlTest, MultiM2MModifiersBoundTheSharedBaseSet) {
    QuerySet<Member, TypeParam> qs;
    auto sql = qs.template join<^^Member::courses, ^^Member::clubs>()
                       .where(storm::orm::where::field<^^Member::age>() > 18)
                       .limit(2)
                       .select()
                       .sql();
    // WHERE/LIMIT appear in Q1 and in BOTH Q2 IN-subqueries.
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Member WHERE age > ? LIMIT 2; ")) << sql;
    constexpr std::string_view bounded = "IN (SELECT id FROM Member WHERE age > ? LIMIT 2)";
    std::size_t hits = 0;
    for (std::size_t pos = sql.find(bounded); pos != std::string::npos; pos = sql.find(bounded, pos + 1)) {
        ++hits;
    }
    EXPECT_EQ(hits, 2U) << sql;
}

// ============================================================================
// Runtime: seeded multi-relation eager loads (#392)
//   Ann→courses[Math,Physics] clubs[Chess]; Ben→courses[Math] clubs[];
//   Cat→courses[] clubs[Chess,Robotics]; Dan→courses[] clubs[].
// ============================================================================

template <typename ConnType> class MultiM2MSeededTest : public StormTestFixture<Member, ConnType, Course, Club> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
        storm::test::seed_members<ConnType>();
    }
};

TYPED_TEST_SUITE(MultiM2MSeededTest, DatabaseTypes);

template <typename Hive> auto find_member(Hive& rows, std::string_view name) -> Member* {
    for (auto& m : rows) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

TYPED_TEST(MultiM2MSeededTest, InnerJoinLoadsBothAndDropsEntitiesEmptyInAnyRelation) {
    QuerySet<Member, TypeParam> qs;
    auto rows = qs.template join<^^Member::courses, ^^Member::clubs>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    // Only Ann is non-empty in BOTH relations.
    ASSERT_EQ(rows->size(), 1U);
    auto* ann = find_member(*rows, "Ann");
    ASSERT_NE(ann, nullptr);
    ASSERT_EQ(ann->courses.size(), 2U);
    EXPECT_EQ(ann->courses[0].title, "Math");
    EXPECT_EQ(ann->courses[1].title, "Physics");
    ASSERT_EQ(ann->clubs.size(), 1U);
    EXPECT_EQ(ann->clubs[0].name, "Chess");
}

TYPED_TEST(MultiM2MSeededTest, LeftJoinKeepsAllAndFillsRelationsIndependently) {
    QuerySet<Member, TypeParam> qs;
    auto rows = qs.template left_join<^^Member::courses, ^^Member::clubs>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 4U);

    auto* ben = find_member(*rows, "Ben"); // courses only
    ASSERT_NE(ben, nullptr);
    ASSERT_EQ(ben->courses.size(), 1U);
    EXPECT_EQ(ben->courses[0].title, "Math");
    EXPECT_TRUE(ben->clubs.empty());

    auto* cat = find_member(*rows, "Cat"); // clubs only
    ASSERT_NE(cat, nullptr);
    EXPECT_TRUE(cat->courses.empty());
    ASSERT_EQ(cat->clubs.size(), 2U);
    EXPECT_EQ(cat->clubs[0].name, "Chess");
    EXPECT_EQ(cat->clubs[1].name, "Robotics");

    auto* dan = find_member(*rows, "Dan"); // both empty
    ASSERT_NE(dan, nullptr);
    EXPECT_TRUE(dan->courses.empty());
    EXPECT_TRUE(dan->clubs.empty());
}

TYPED_TEST(MultiM2MSeededTest, WhereOrderLimitBoundTheSharedBaseSet) {
    QuerySet<Member, TypeParam> qs;
    // age > 18 keeps all; ORDER BY age + LIMIT 2 bounds the BASE set to Ann, Ben.
    auto rows = qs.template left_join<^^Member::courses, ^^Member::clubs>()
                        .where(storm::orm::where::field<^^Member::age>() > 18)
                        .template order_by<^^Member::age>()
                        .limit(2)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 2U);
    auto* ann = find_member(*rows, "Ann");
    ASSERT_NE(ann, nullptr);
    EXPECT_EQ(ann->courses.size(), 2U);
    EXPECT_EQ(ann->clubs.size(), 1U);
    auto* ben = find_member(*rows, "Ben");
    ASSERT_NE(ben, nullptr);
    EXPECT_EQ(ben->courses.size(), 1U);
    EXPECT_TRUE(ben->clubs.empty());
    EXPECT_EQ(find_member(*rows, "Cat"), nullptr);
}

TYPED_TEST(MultiM2MSeededTest, EmptyResultSet) {
    QuerySet<Member, TypeParam> qs;
    auto rows = qs.template join<^^Member::courses, ^^Member::clubs>()
                        .where(storm::orm::where::field<^^Member::age>() > 99)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    EXPECT_TRUE(rows->empty());
}

TYPED_TEST(MultiM2MSeededTest, FirstAndGetLoadBothRelations) {
    QuerySet<Member, TypeParam> qs;
    auto first = qs.template join<^^Member::courses, ^^Member::clubs>().first().execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((*first)->name, "Ann");
    EXPECT_EQ((*first)->courses.size(), 2U);
    EXPECT_EQ((*first)->clubs.size(), 1U);

    auto got = qs.template left_join<^^Member::courses, ^^Member::clubs>()
                       .where(storm::orm::where::field<^^Member::name>() == "Cat")
                       .get()
                       .execute();
    ASSERT_TRUE(got.has_value()) << got.error().message();
    EXPECT_TRUE(got->courses.empty());
    EXPECT_EQ(got->clubs.size(), 2U);
}

TYPED_TEST(MultiM2MSeededTest, RowsGeneratorYieldsBothRelations) {
    QuerySet<Member, TypeParam> qs;
    auto joined = qs.template left_join<^^Member::courses, ^^Member::clubs>();
    std::size_t seen = 0;
    std::size_t total_courses = 0;
    std::size_t total_clubs = 0;
    for (auto&& row : joined.rows()) {
        ASSERT_TRUE(row.has_value()) << row.error().message();
        ++seen;
        total_courses += row->courses.size();
        total_clubs += row->clubs.size();
    }
    EXPECT_EQ(seen, 4U);
    EXPECT_EQ(total_courses, 3U);
    EXPECT_EQ(total_clubs, 3U);
}

// Aggregates over a multi-m2m join run on the chained N-junction join —
// COUNT(*) counts cartesian TUPLES (the consistent extension of the
// single-relation "(base, related) pairs" semantics). Ann: 2 courses × 1 club.
TYPED_TEST(MultiM2MSeededTest, CountOverMultiM2MCountsCartesianTuples) {
    QuerySet<Member, TypeParam> qs;
    auto count = qs.template join<^^Member::courses, ^^Member::clubs>().count().execute();
    ASSERT_TRUE(count.has_value()) << count.error().message();
    EXPECT_EQ(count.value(), 2);
}

// Statement-cache correctness: identical multi query twice, then a different
// (single-relation) query on the same QuerySet.
TYPED_TEST(MultiM2MSeededTest, RepeatedAndSwitchedQueriesUseCacheCorrectly) {
    QuerySet<Member, TypeParam> qs;
    auto multi = qs.template join<^^Member::courses, ^^Member::clubs>();
    auto first_run = multi.select().execute();
    ASSERT_TRUE(first_run.has_value()) << first_run.error().message();
    auto second_run = multi.select().execute();
    ASSERT_TRUE(second_run.has_value()) << second_run.error().message();
    EXPECT_EQ(first_run->size(), second_run->size());

    auto single = qs.template join<^^Member::courses>().select().execute();
    ASSERT_TRUE(single.has_value()) << single.error().message();
    ASSERT_EQ(single->size(), 2U); // Ann, Ben — clubs no longer constrain
    auto* ben = find_member(*single, "Ben");
    ASSERT_NE(ben, nullptr);
    EXPECT_TRUE(ben->clubs.empty()); // single-relation join never fills clubs
}

// NOLINTEND(misc-const-correctness)
```

- [ ] **Step 1.3: Verify the tests fail to compile (fail-first proof)**

```bash
cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -30
```

Expected: compile error — `join<^^Member::courses, ^^Member::clubs>()` violates the
current `requires` clause (`sizeof...(FKFields) == 1` for m2m), and the
`static_assert(CanJoin<Member, ^^Member::courses, ^^Member::clubs>)` fires.
Do NOT commit yet (pre-commit hook would fail).

### Task 2: `join.cppm` — per-relation descriptors + fragment split + variadic factory

**Files:**
- Modify: `src/orm/statements/join.cppm`

- [ ] **Step 2.1: Namespace-scope clause-SQL fn type + `M2MRelation` + wrapper vector**

Replace the `JoinStatementWrapper` struct (join.cppm:29-67) with:

```cpp
    // Builds clause-parameterized SQL (Q1 base subquery / per-relation Q2) from
    // the QuerySet's WHERE / ORDER BY / LIMIT / OFFSET state.
    using M2MClauseSqlFn = auto (*)(
            const orm::where::ExpressionVariantPtr&,
            const std::optional<OrderByWrapper>&,
            const std::optional<int>&,
            const std::optional<int>&
    ) -> std::string;

    // One eager-loaded m2m relation (#392): its Q2 builder plus the stitch
    // fn-pointers. extract_q2_owner_pk_fn keys the stitch into the shared Q1
    // pk→entity map; append_related_q2_fn fills the entity's container;
    // container_empty_fn + is_left drive the per-relation INNER drop.
    struct M2MRelation {
        M2MClauseSqlFn build_q2_sql_fn;
        auto (*extract_q2_owner_pk_fn)(ErasedStatementPtr) -> std::int64_t;
        auto (*append_related_q2_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void;
        auto (*container_empty_fn)(ErasedObjectPtr) -> bool;
        // LEFT keeps zero-relation entities; INNER drops them after the stitch.
        bool is_left;
    };

    struct JoinStatementWrapper {
        auto (*get_complete_sql_fn)() -> const std::string&;
        // Per-row extractor for FK joins. nullptr for m2m wrappers (the two-query
        // m2m path extracts base rows via Base::extract_all_columns, never this).
        auto (*extract_row_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void = nullptr;

        // Many-to-many two-query predicate-pushdown extension (#391, #392).
        // build_q1_sql_fn produces the base-entity SELECT — its text depends only
        // on the base model + clauses, so ONE Q1 serves every relation. Each
        // eager-loaded m2m relation contributes one M2MRelation descriptor; the
        // stitch loop runs each Q2 in turn against the shared pk→entity map.
        // Empty for plain FK joins.
        M2MClauseSqlFn           build_q1_sql_fn = nullptr;
        std::vector<M2MRelation> m2m_relations;

        [[nodiscard]] auto is_m2m() const -> bool {
            return !m2m_relations.empty();
        }

        // Get complete pre-computed SELECT...JOIN SQL
        [[nodiscard]] auto get_complete_sql() const -> const std::string& {
            return get_complete_sql_fn();
        }

        auto extract_row(ErasedStatementPtr stmt, ErasedObjectPtr obj) const -> void {
            extract_row_fn(stmt, obj);
        }
    };
```

(The old `ClauseSqlFn` nested alias, the five m2m fn-pointer members, and
`m2m_is_left` are gone.)

- [ ] **Step 2.2: Split `M2MJoinStatement` complete-SQL fragments**

Inside `M2MJoinStatement`, REPLACE `calculate_select_prefix_size`,
`build_select_prefix`, `calculate_join_suffix_size`, `build_join_suffix`,
`select_prefix_arr_`, `join_suffix_arr_`, and the public `get_complete_sql()`
(join.cppm:521-607) with fragment builders. `append_related_columns` stays (used
by `build_q2_prefix`). New code:

```cpp
        // ---- Complete-SQL fragments (#392) ------------------------------------
        // The modifier-free join SQL (aggregates / DISTINCT / set-ops) is
        // assembled per-wrapper in make_m2m_join_wrapper from these fragments so
        // several relations can chain with unique aliases: relation i uses
        // junction alias 2+2i, related alias 3+2i (relation 0 keeps t2/t3).

        // base_cols_: "SELECT t1.<c1>, t1.<c2>[_id], …" — base columns only.
        static consteval auto calculate_base_cols_size() -> std::size_t {
            std::size_t total = 7; // "SELECT "
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                total += 2 + 3 + std::meta::identifier_of(Base::all_members_[i]).size() + 3;
            }
            return total + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_base_cols() {
            ConstexprString<calculate_base_cols_size()> result;
            result.append("SELECT ");
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if (!first) {
                    result.append(", ");
                }
                result.append("t1.");
                result.append(std::meta::identifier_of(Base::all_members_[i]));
                if (Base::is_fk_field(Base::all_members_[i])) {
                    result.append("_id");
                }
                first = false;
            }
            return result;
        }

        // base_from_: " FROM (SELECT <cols> FROM <Base>) t1"
        static consteval auto calculate_base_from_size() -> std::size_t {
            return 14 + Base::field_names_array_.len + 6 + Base::table_name_.size() + 4 +
                   utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_base_from() {
            ConstexprString<calculate_base_from_size()> result;
            result.append(" FROM (SELECT ");
            result.append(Base::field_names_array_);
            result.append(" FROM ");
            result.append(Base::table_name_);
            result.append(") t1");
            return result;
        }

        static constexpr auto base_cols_arr_ = build_base_cols();
        static constexpr auto base_from_arr_ = build_base_from();

        // Constexpr name snapshots for the runtime fragment appenders below
        // (the consteval name fns cannot be called at runtime).
        static constexpr std::string_view junction_name_v_ = junction_table_name();
        static constexpr std::string_view owner_col_v_     = owner_col_name();
        static constexpr std::string_view related_col_v_   = related_col_name();
        static constexpr std::string_view join_kw_v_       = join_keyword();

        struct RelatedCol {
            std::string_view name;
            bool             is_fk;
        };
        static constexpr auto related_cols_ = []() consteval {
            std::array<RelatedCol, RelatedBase::field_count_> cols{};
            for (std::size_t i = 0; i < RelatedBase::field_count_; ++i) {
                cols[i] = {std::meta::identifier_of(RelatedBase::all_members_[i]),
                           Base::is_fk_field(RelatedBase::all_members_[i])};
            }
            return cols;
        }();

      public:
        static constexpr auto base_cols_view() -> std::string_view {
            return base_cols_arr_.view();
        }
        static constexpr auto base_from_view() -> std::string_view {
            return base_from_arr_.view();
        }

        // Append ", t<A>.<col>[_id]" for every related member. Runs once per
        // wrapper type (complete SQL is a function-local static in the factory).
        static auto append_complete_cols(std::string& sql, std::size_t related_alias) -> void {
            for (const auto& col : related_cols_) {
                sql += std::format(", t{}.{}{}", related_alias, col.name, col.is_fk ? "_id" : "");
            }
        }

        // Append "<KW> <junction> t<J> ON t1.<pk> = t<J>.<owner>_id
        //         <KW> <Related> t<R> ON t<J>.<related>_id = t<R>.<rpk>"
        // with J = junction_alias, R = junction_alias + 1.
        static auto append_complete_join(std::string& sql, std::size_t junction_alias) -> void {
            sql += std::format(
                    "{0}{1} t{2} ON t1.{3} = t{2}.{4}_id{0}{5} t{6} ON t{2}.{7}_id = t{6}.{8}",
                    join_kw_v_,
                    junction_name_v_,
                    junction_alias,
                    Base::pk_name_,
                    owner_col_v_,
                    RelatedBase::table_name_,
                    junction_alias + 1,
                    related_col_v_,
                    RelatedBase::pk_name_
            );
        }

      private:
```

Also update the class doc comment (join.cppm:405-431): replace the
`get_complete_sql()` paragraph with a note that the modifier-free N-junction
join SQL is assembled in `make_m2m_join_wrapper` from `base_cols_` /
per-relation cols / `base_from_` / per-relation join fragments, and the
single-relation text is unchanged.

- [ ] **Step 2.3: Variadic factory**

Replace `make_m2m_join_wrapper` (join.cppm:800-829) with:

```cpp
    // One M2MRelation descriptor (#392) — Q2 builder + stitch fns for one field.
    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info M2MField>
        requires M2MFieldOf<T, M2MField>
    [[nodiscard]] auto make_m2m_relation() -> M2MRelation {
        using JS = M2MJoinStatement<T, ConnType, Type, M2MField>;
        return M2MRelation{
                .build_q2_sql_fn = +[](const orm::where::ExpressionVariantPtr& where_expr,
                                       const std::optional<OrderByWrapper>&    order_by,
                                       const std::optional<int>&               limit,
                                       const std::optional<int>&               offset) -> std::string {
                    return JS::build_q2_sql(where_expr, order_by, limit, offset);
                },
                .extract_q2_owner_pk_fn = +[](ErasedStatementPtr stmt) -> std::int64_t {
                    return JS::extract_q2_owner_pk(static_cast<typename ConnType::Statement*>(stmt));
                },
                .append_related_q2_fn = +[](ErasedStatementPtr stmt, ErasedObjectPtr obj) -> void {
                    JS::append_related_q2(static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj));
                },
                .container_empty_fn =
                        +[](ErasedObjectPtr obj) -> bool { return JS::container_empty(*static_cast<T*>(obj)); },
                .is_left = (Type == JoinType::Left)
        };
    }

    // Modifier-free chained join SQL over every relation — relation i uses
    // junction alias 2+2i / related alias 3+2i, so the single-relation text is
    // byte-identical to the pre-#392 monolithic builder. Consumed by
    // aggregates / DISTINCT / set-ops; a COUNT over it counts cartesian TUPLES
    // (the N-relation extension of the documented "(base, related) pairs").
    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info... M2MFields>
    [[nodiscard]] auto build_m2m_complete_sql() -> std::string {
        std::string sql{M2MJoinStatement<T, ConnType, Type, M2MFields...[0]>::base_cols_view()};
        [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
            (M2MJoinStatement<T, ConnType, Type, M2MFields...[Is]>::append_complete_cols(sql, 3 + (2 * Is)), ...);
        }(std::make_index_sequence<sizeof...(M2MFields)>{});
        sql += M2MJoinStatement<T, ConnType, Type, M2MFields...[0]>::base_from_view();
        [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
            (M2MJoinStatement<T, ConnType, Type, M2MFields...[Is]>::append_complete_join(sql, 2 + (2 * Is)), ...);
        }(std::make_index_sequence<sizeof...(M2MFields)>{});
        return sql;
    }

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info... M2MFields>
        requires(sizeof...(M2MFields) >= 1 && (M2MFieldOf<T, M2MFields> && ...))
    [[nodiscard]] auto make_m2m_join_wrapper() -> JoinStatementWrapper {
        using First = M2MJoinStatement<T, ConnType, Type, M2MFields...[0]>;
        JoinStatementWrapper wrapper{
                .get_complete_sql_fn = +[]() -> const std::string& {
                    static const std::string str = build_m2m_complete_sql<T, ConnType, Type, M2MFields...>();
                    return str;
                },
                // Q1 text depends only on the base model + clauses — any relation
                // yields the same string; take it from the first.
                .build_q1_sql_fn = +[](const orm::where::ExpressionVariantPtr& where_expr,
                                       const std::optional<OrderByWrapper>&    order_by,
                                       const std::optional<int>&               limit,
                                       const std::optional<int>&               offset) -> std::string {
                    return First::build_base_subquery(where_expr, order_by, limit, offset);
                }
        };
        wrapper.m2m_relations.reserve(sizeof...(M2MFields));
        (wrapper.m2m_relations.push_back(make_m2m_relation<T, ConnType, Type, M2MFields>()), ...);
        return wrapper;
    }
```

**Note:** a function-local alias template is not valid C++, which is why the
folds spell `M2MJoinStatement<T, ConnType, Type, M2MFields...[Is]>` out in full.

### Task 3: `select.cppm` — loop the relations

**Files:**
- Modify: `src/orm/statements/select.cppm`

- [ ] **Step 3.1: `build_sql` m2m arm (select.cppm:90-98)**

```cpp
            // M2M joins (#391, #392): the eager load is 1 + N queries — Q1 selects
            // the base entities once, each relation's Q2 selects (owner_pk,
            // related.*) filtered by the same base subquery. build_sql is only a
            // debugging/introspection surface for m2m (.sql()); execution runs them
            // separately in execute_m2m_2query. Join them with "; " so .sql()
            // shows the full plan.
            if (join_wrapper && join_wrapper->is_m2m()) {
                std::string sql = join_wrapper->build_q1_sql_fn(where_expr, order_by_wrapper, limit, offset);
                for (const auto& rel : join_wrapper->m2m_relations) {
                    sql += "; ";
                    sql += rel.build_q2_sql_fn(where_expr, order_by_wrapper, limit, offset);
                }
                return sql;
            }
```

- [ ] **Step 3.2: `execute_m2m_2query` (select.cppm:638-673) — Q2 loop + unconditional drop pass**

Replace the Q2/drop section:

```cpp
            // Q2 per relation (#392) — related rows, stitched into their owner's
            // container through the shared map.
            for (const auto& rel : wrapper.m2m_relations) {
                if (auto stitched = run_q2_stitch(rel, c, by_pk); !stitched) {
                    return std::unexpected(stitched.error());
                }
            }

            // INNER semantics: drop entities empty in ANY inner relation.
            drop_empty_relations(results, wrapper);
```

- [ ] **Step 3.3: `run_q2_stitch` takes the relation (select.cppm:709)**

Signature and body use `const M2MRelation& rel`:

```cpp
        // Q2: prepare one relation's junction⋈related query, bind the SAME WHERE
        // (its IN-subquery), step rows, append each related object to its owner.
        [[nodiscard]] auto run_q2_stitch(
                const M2MRelation& rel, const QueryClauses& c, std::unordered_map<std::int64_t, T*>& by_pk
        ) noexcept -> std::expected<void, Error> {
            auto prep = prepare_clause_sql(rel.build_q2_sql_fn, c);
            if (!prep) {
                return std::unexpected(prep.error());
            }
            Statement* stmt        = *prep;
            int        step_result = 0;
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                const std::int64_t owner = rel.extract_q2_owner_pk_fn(stmt);
                if (auto it = by_pk.find(owner); it != by_pk.end()) {
                    rel.append_related_q2_fn(stmt, it->second);
                }
            }
            if (step_result != Statement::NO_MORE_ROWS) {
                Error err{step_result, stmt->get_error_message()};
                stmt->reset();
                return std::unexpected(std::move(err));
            }
            stmt->reset();
            return {};
        }
```

`prepare_clause_sql`'s parameter type changes from
`typename JoinStatementWrapper::ClauseSqlFn` to `M2MClauseSqlFn`.

- [ ] **Step 3.4: `drop_empty_relations` (select.cppm:734-742) — any INNER relation empty**

```cpp
        // INNER-join semantics (#392): remove entities whose container stayed
        // empty in ANY inner relation; LEFT relations never drop. When every
        // relation is LEFT the predicate is constant-false and nothing drops.
        static auto drop_empty_relations(plf::hive<T>& results, const JoinStatementWrapper& wrapper) noexcept -> void {
            for (auto it = results.begin(); it != results.end();) {
                T&         obj  = *it;
                const bool drop = std::ranges::any_of(wrapper.m2m_relations, [&obj](const M2MRelation& rel) {
                    return !rel.is_left && rel.container_empty_fn(&obj);
                });
                it = drop ? results.erase(it) : std::next(it);
            }
        }
```

- [ ] **Step 3.5: `rows_generator` m2m arm (select.cppm:351-359) — wrapper is no longer POD**

The wrapper now owns a vector; move it instead of copying:

```cpp
                    return rows_m2m_materialized(
                            std::move(conn),
                            std::move(*join_wrapper), // owns the relation vector — move into the coroutine frame
                            std::move(where_expr),
                            limit,
                            offset,
                            std::move(order_by_wrapper)
                    );
```

Also update the comment in `execute_m2m_2query`'s doc block to mention "one Q2
per relation (#392)".

### Task 4: `queryset.cppm` — accept the pack

**Files:**
- Modify: `src/orm/queryset.cppm:235-282`
- Modify: `src/orm/statements/join.cppm` (the `m2m_fields_distinct` helper lives with the other m2m machinery)

- [ ] **Step 4.1: Add `m2m_fields_distinct` to `join.cppm`** (exported, next to `M2MRelation`)

```cpp
    // Rejects join<^^T::courses, ^^T::courses>() — a duplicated m2m field would
    // run its Q2 twice and silently double-fill the same container (#392).
    template <std::meta::info... Fields> consteval auto m2m_fields_distinct() -> bool {
        const std::array<std::string_view, sizeof...(Fields)> names{std::meta::identifier_of(Fields)...};
        for (std::size_t i = 0; i < names.size(); ++i) {
            for (std::size_t j = i + 1; j < names.size(); ++j) {
                if (names[i] == names[j]) {
                    return false;
                }
            }
        }
        return true;
    }
```

- [ ] **Step 4.2: `join()` (queryset.cppm:235-257)**

```cpp
        // INNER JOIN support for FK fields or many-to-many fields (#203, #392)
        // Immutable: returns a new QuerySet with the join attached (Django-style).
        // Usage:
        //   Single FK: message_qs.join<^^Message::sender>().select()
        //   Multi FK:  message_qs.join<^^Message::sender, ^^Message::receiver>().select()
        //   M2M:       student_qs.join<^^Student::courses>().select()
        //   Multi M2M: member_qs.join<^^Member::courses, ^^Member::clubs>().select()
        // Mixed FK + m2m in one call is rejected (out of scope, #392).
        template <std::meta::info... FKFields>
            requires(
                    sizeof...(FKFields) >= 1 &&
                    ((orm::statements::FKFieldOf<T, FKFields> && ...) ||
                     ((orm::statements::M2MFieldOf<T, FKFields> && ...) &&
                      orm::statements::m2m_fields_distinct<FKFields...>()))
            )
        [[nodiscard]] auto join() const -> QuerySet {
            auto result = clone_state();
            if constexpr ((orm::statements::M2MFieldOf<T, FKFields> && ...)) {
                result.join_stmt_ = orm::statements::
                        make_m2m_join_wrapper<T, ConnType, orm::statements::JoinType::Inner, FKFields...>();
            } else {
                result.join_stmt_ = orm::statements::
                        make_join_wrapper<T, ConnType, orm::statements::JoinType::Inner, FKFields...>();
            }
            return result;
        }
```

- [ ] **Step 4.3: `left_join()` (queryset.cppm:259-282)** — same requires change, same
pack pass-through with `JoinType::Left`, and add the `Multi M2M:` usage line:
`member_qs.left_join<^^Member::courses, ^^Member::clubs>().select()` (LEFT keeps
base entities empty in any/all relations). RIGHT JOIN untouched (later removed in #397).

### Task 5: Green build + full suites

- [ ] **Step 5.1: Build debug + run new tests**

```bash
cmake --build --preset ninja-debug 2>&1 | tail -5
./build/debug/tests/storm_tests --gtest_filter='MultiM2M*' --gtest_brief=1
```

Expected: all `MultiM2MSqlTest.*` / `MultiM2MSeededTest.*` PASS on both backends.

- [ ] **Step 5.2: Full suite (SQLite + PG)**

```bash
ctest --preset ninja-debug 2>&1 | tail -5
```

Expected: 100% tests passed — in particular every pre-existing `M2M*` test
(single-relation SQL text must be byte-identical).

### Task 6: Mock error paths for the second Q2

**Files:**
- Modify: `tests/mock_sqlite/test_orm_mock_errors.cpp` (after `M2MSelectFailsOnWhereBindError`, line ~3364)

- [ ] **Step 6.1: Add multi-relation mock model + tests (fail-first not applicable —
API already implemented; these extend error coverage)**

```cpp
    // ============================================================================
    // Multi-relation m2m (#392) — second-Q2 error paths
    // ============================================================================

    struct MockClub {
        [[= storm::meta::FieldAttr::primary]] std::int64_t id{};
        std::string                                        name;
    };

    struct MockMember {
        [[= storm::meta::FieldAttr::primary]] std::int64_t      id{};
        std::string                                             name;
        [[= storm::meta::many_to_many]] std::vector<MockCourse> courses;
        [[= storm::meta::many_to_many]] std::vector<MockClub>   clubs;
    };

    // Prepare order: calls 1-3 = connection/BEGIN bring-up, 4 = Q1, 5 = Q2a
    // (courses), 6 = Q2b (clubs). Failing call 6 exercises the per-relation Q2
    // loop's error branch for a relation after the first.
    TEST_F(ORMMockErrorTest, MultiM2MSelectFailsOnSecondQ2PrepareError) {
        MockSqlite3Config::prepare_fails_on_call(6, SQLITE_IOERR);

        QuerySet<MockMember> qs;
        auto rows = qs.join<^^MockMember::courses, ^^MockMember::clubs>().select().execute();

        ASSERT_FALSE(rows.has_value());
        EXPECT_EQ(rows.error().code(), SQLITE_IOERR);
    }

    // Step order: 1 = BEGIN, 2 = Q1 NO_MORE (empty), 3 = Q2a NO_MORE,
    // 4 = Q2b fails → the error surfaces from the second run_q2_stitch and the
    // transaction never commits.
    TEST_F(ORMMockErrorTest, MultiM2MSelectFailsOnSecondQ2StepError) {
        MockSqlite3Config::step_fails_on_call(4, SQLITE_CORRUPT);

        QuerySet<MockMember> qs;
        auto rows = qs.join<^^MockMember::courses, ^^MockMember::clubs>().select().execute();

        ASSERT_FALSE(rows.has_value());
        EXPECT_EQ(rows.error().code(), SQLITE_CORRUPT);
    }

    // With two relations the COMMIT is the 5th step (BEGIN, Q1, Q2a, Q2b, COMMIT).
    TEST_F(ORMMockErrorTest, MultiM2MSelectFailsOnCommitError) {
        MockSqlite3Config::step_fails_on_call(5, SQLITE_FULL);

        QuerySet<MockMember> qs;
        auto rows = qs.join<^^MockMember::courses, ^^MockMember::clubs>().select().execute();

        ASSERT_FALSE(rows.has_value());
        EXPECT_EQ(rows.error().code(), SQLITE_FULL);
    }
```

- [ ] **Step 6.2: Build + run mock tests**

```bash
cmake --build --preset ninja-debug 2>&1 | tail -3
./build/debug/tests/storm_mock_sqlite_tests --gtest_filter='*MultiM2M*' --gtest_brief=1
./build/debug/tests/storm_mock_sqlite_tests --gtest_filter='*M2M*' --gtest_brief=1
```

Expected: new tests PASS; all pre-existing M2M mock tests still PASS (their
prepare/step call counts are unchanged — single-relation queries still issue
exactly one Q2).

### Task 7: Coverage

- [ ] **Step 7.1:**

```bash
cmake --build --preset ninja-debug-coverage --target coverage 2>&1 | tail -15
```

Expected: 100% line coverage on `src/`. If a new branch is uncovered (e.g. the
`drop ? erase : next` arms), add the missing test rather than an exclusion.

### Task 8: Sanitizers

- [ ] **Step 8.1: ASAN+UBSAN** (memory: ASAN runs are 4-5× slower — use a long timeout)

```bash
cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan 2>&1 | tail -3
ctest --preset ninja-asan-ubsan 2>&1 | tail -5
```

Expected: 100% passed, no sanitizer reports.

- [ ] **Step 8.2: TSAN**

```bash
cmake --preset ninja-tsan && cmake --build --preset ninja-tsan 2>&1 | tail -3
ctest --preset ninja-tsan 2>&1 | tail -5
```

Expected: 100% passed.

### Task 9: Release build + benchmarks

The wrapper gained a `std::vector` member — verify the FK-join and plain SELECT
hot paths did not regress (empty vector: no allocation, but the wrapper copy in
`clone_state` is no longer trivial).

- [ ] **Step 9.1:**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release 2>&1 | tail -3
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/(SELECT|JOIN)/.*' --benchmark_repetitions=5 2>&1 | tail -40
```

Compare medians against develop (check out develop in a scratch run if needed).
Deltas under ~2% with cv ≪ 5% are noise; if a suspicious delta appears, re-run
ALTERNATING develop/feature (dev,feat × 5, median-of-medians) before concluding.
Revert the offending change if a real slowdown is confirmed.

### Task 10: Docs

**Files:**
- Modify: `docs/features/JOIN_OPERATIONS.md` (multi-m2m section under the #391 strategy)
- Modify: `CLAUDE.md` (m2m bullet + QuerySet API example)
- Check: `grep -rn "many_to_many\|m2m" .claude/agents/*.md` — update any agent file describing the single-m2m constraint

- [ ] **Step 10.1:** JOIN_OPERATIONS.md: document `join<m2m_a, m2m_b>()` — shared Q1,
one Q2 per relation, additive cost, INNER drops entities empty in ANY inner
relation, LEFT fills relations independently (Django semantics), aggregates
count cartesian tuples, duplicates rejected at compile time, mixed FK+m2m
rejected. CLAUDE.md: extend the m2m QuerySet example with
`member_qs.join<^^Member::courses, ^^Member::clubs>().select()`.

### Task 11: Commit, PR, gates, merge

- [ ] **Step 11.1:** `git status --short` → stage the implementation, tests, and docs
files explicitly (never `git add -A`), commit:
`feat(orm): multiple m2m joins in one query (#392)`.
- [ ] **Step 11.2:** Push, `gh pr create --base develop` with `Closes #392`.
- [ ] **Step 11.3:** Wait 30s → `/sonarcloud-status`; fix everything until clean.
- [ ] **Step 11.4:** `gh pr checks <PR#> --watch` until ninja-debug, ninja-asan-ubsan, ninja-tsan pass.
- [ ] **Step 11.5:** Check off issue #392 subtasks if any, `gh pr merge --squash`, `gh issue close 392`, `git checkout develop && git pull`.
