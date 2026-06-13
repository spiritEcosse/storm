#include <gtest/gtest.h>
#include <meta>
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
// Schema: one auto junction table per m2m field (#392)
// ============================================================================

TEST(MultiM2MSchemaTest, OneJunctionTablePerM2MField) {
    const auto& sqls =
            storm::orm::schema::SchemaStatement<Member>::junction_table_sqls<storm::orm::schema::Dialect::SQLite>();
    ASSERT_EQ(sqls.size(), 2U);
    EXPECT_TRUE(sqls[0].contains("CREATE TABLE Member_Course")) << sqls[0];
    EXPECT_TRUE(sqls[0].contains("PRIMARY KEY (Member_id, Course_id)")) << sqls[0];
    EXPECT_TRUE(sqls[1].contains("CREATE TABLE Member_Club")) << sqls[1];
    EXPECT_TRUE(sqls[1].contains("PRIMARY KEY (Member_id, Club_id)")) << sqls[1];
}

// ============================================================================
// SQL shape: Q1 + one Q2 per relation, "; "-separated (#392)
// ============================================================================

template <typename ConnType> class MultiM2MSqlTest : public StormTestFixture<Member, ConnType, Course, Club> {};

TYPED_TEST_SUITE(MultiM2MSqlTest, DatabaseTypes);

TYPED_TEST(MultiM2MSqlTest, MultiM2MJoinSqlShape) {
    QuerySet<Member, TypeParam> qs;
    auto                        sql = qs.template join<^^Member::courses, ^^Member::clubs>().select().sql();
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
    auto                        sql = qs.template join<^^Member::courses, ^^Member::clubs>()
                       .where(storm::orm::where::f<^^Member::age>() > 18)
                       .limit(2)
                       .select()
                       .sql();
    // WHERE/LIMIT appear in Q1 and in BOTH Q2 IN-subqueries.
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Member WHERE age > ? LIMIT 2; ")) << sql;
    constexpr std::string_view bounded = "IN (SELECT id FROM Member WHERE age > ? LIMIT 2)";
    std::size_t                hits    = 0;
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

// Finds the member with the given name; nullptr if absent.
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
    auto                        rows = qs.template join<^^Member::courses, ^^Member::clubs>().select().execute();
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
    auto                        rows = qs.template left_join<^^Member::courses, ^^Member::clubs>().select().execute();
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
                        .where(storm::orm::where::f<^^Member::age>() > 18)
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
    auto                        rows = qs.template join<^^Member::courses, ^^Member::clubs>()
                        .where(storm::orm::where::f<^^Member::age>() > 99)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    EXPECT_TRUE(rows->empty());
}

TYPED_TEST(MultiM2MSeededTest, FirstAndGetLoadBothRelations) {
    QuerySet<Member, TypeParam> qs;
    auto                        first = qs.template join<^^Member::courses, ^^Member::clubs>().first().execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((*first)->name, "Ann");
    EXPECT_EQ((*first)->courses.size(), 2U);
    EXPECT_EQ((*first)->clubs.size(), 1U);

    auto got = qs.template left_join<^^Member::courses, ^^Member::clubs>()
                       .where(storm::orm::where::f<^^Member::name>() == "Cat")
                       .get()
                       .execute();
    ASSERT_TRUE(got.has_value()) << got.error().message();
    EXPECT_TRUE(got->courses.empty());
    EXPECT_EQ(got->clubs.size(), 2U);
}

TYPED_TEST(MultiM2MSeededTest, RowsGeneratorYieldsBothRelations) {
    QuerySet<Member, TypeParam> qs;
    auto                        joined        = qs.template left_join<^^Member::courses, ^^Member::clubs>();
    std::size_t                 seen          = 0;
    std::size_t                 total_courses = 0;
    std::size_t                 total_clubs   = 0;
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
    auto                        count = qs.template join<^^Member::courses, ^^Member::clubs>().count().execute();
    ASSERT_TRUE(count.has_value()) << count.error().message();
    EXPECT_EQ(count.value(), 2);
}

// The aggregate complete SQL emits related FK columns as "<field>_id" — Lesson
// carries an FK to Topic, so the chained join selects t3.topic_id (#392).
template <typename ConnType> class M2MRelatedFkTest : public StormTestFixture<Tutor, ConnType, Lesson, Topic> {};

TYPED_TEST_SUITE(M2MRelatedFkTest, DatabaseTypes);

TYPED_TEST(M2MRelatedFkTest, M2MWithFkRelatedModelLoadsAndCounts) {
    auto                       conn = QuerySet<Tutor, TypeParam>::get_default_connection();
    QuerySet<Topic, TypeParam> tqs;
    ASSERT_TRUE(tqs.insert(Topic{.name = "Algebra"}).execute().has_value());
    QuerySet<Lesson, TypeParam> lqs;
    ASSERT_TRUE(lqs.insert(Lesson{.title = "Intro", .topic = {.id = 1}}).execute().has_value());
    QuerySet<Tutor, TypeParam> qs;
    ASSERT_TRUE(qs.insert(Tutor{.name = "Eva"}).execute().has_value());
    ASSERT_TRUE(conn->execute("INSERT INTO Tutor_Lesson (Tutor_id, Lesson_id) VALUES (1, 1)").has_value());

    // Eager load: the related FK extracts as a pk-only Topic object.
    auto rows = qs.template join<^^Tutor::lessons>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    ASSERT_EQ(rows->begin()->lessons.size(), 1U);
    EXPECT_EQ(rows->begin()->lessons[0].title, "Intro");
    EXPECT_EQ(rows->begin()->lessons[0].topic.id, 1);

    // Aggregate complete SQL emits the related FK column as topic_id.
    auto count = qs.template join<^^Tutor::lessons>().count().execute();
    ASSERT_TRUE(count.has_value()) << count.error().message();
    EXPECT_EQ(count.value(), 1);
}

// Statement-cache correctness: identical multi query twice, then a different
// (single-relation) query on the same QuerySet.
TYPED_TEST(MultiM2MSeededTest, RepeatedAndSwitchedQueriesUseCacheCorrectly) {
    QuerySet<Member, TypeParam> qs;
    auto                        multi     = qs.template join<^^Member::courses, ^^Member::clubs>();
    auto                        first_run = multi.select().execute();
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
