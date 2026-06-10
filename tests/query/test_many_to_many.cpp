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
// Compile-time: m2m fields are excluded from persisted members (#203)
// ============================================================================

static_assert(
        storm::orm::statements::BaseStatement<Student>::field_count_ == 3,
        "courses (m2m) must not count as a persisted column"
);
static_assert(storm::orm::statements::BaseStatement<Course>::field_count_ == 2);

// ============================================================================
// Compile-time: M2MFieldOf concept
// ============================================================================

static_assert(storm::orm::statements::M2MFieldOf<Student, ^^Student::courses>);
static_assert(!storm::orm::statements::M2MFieldOf<Student, ^^Student::name>);
static_assert(!storm::orm::statements::FKFieldOf<Student, ^^Student::courses>);
static_assert(storm::orm::statements::M2MFieldOf<Pupil, ^^Pupil::courses>);

// ============================================================================
// Compile-time: related-type extraction from containers via std::meta
// ============================================================================

static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<Course>>, Course>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<plf::hive<Track>>, Track>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<std::shared_ptr<Track>>>, Track>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::deque<Course>>, Course>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<std::unique_ptr<Course>>>, Course>);

// ============================================================================
// Runtime: CRUD on a model WITH an m2m field ignores the container member
// ============================================================================

template <typename ConnType> class M2MBaseTest : public StormTestFixture<Student, ConnType, Course> {};

TYPED_TEST_SUITE(M2MBaseTest, DatabaseTypes);

TYPED_TEST(M2MBaseTest, PlainCrudIgnoresM2MField) {
    QuerySet<Student, TypeParam> qs;
    Student const                s{.name = "Alice", .age = 20, .courses = {{.id = 99, .title = "ghost"}}};
    auto                         ins = qs.insert(s).execute();
    ASSERT_TRUE(ins.has_value()) << ins.error().message();

    auto rows = qs.select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Alice");
    EXPECT_EQ(rows->begin()->age, 20);
    EXPECT_TRUE(rows->begin()->courses.empty()); // plain select never populates m2m
}

// ============================================================================
// Schema: auto-generated junction table DDL (#203 Phase 1)
// ============================================================================

TEST(M2MSchemaTest, JunctionTableSqlSQLite) {
    const auto& sql =
            storm::orm::schema::SchemaStatement<Student>::junction_table_sql<storm::orm::schema::Dialect::SQLite>();
    EXPECT_TRUE(sql.contains("CREATE TABLE Student_Course")) << sql;
    EXPECT_TRUE(sql.contains("Student_id INTEGER NOT NULL")) << sql;
    EXPECT_TRUE(sql.contains("Course_id INTEGER NOT NULL")) << sql;
    EXPECT_TRUE(sql.contains("PRIMARY KEY (Student_id, Course_id)")) << sql;
}

TEST(M2MSchemaTest, JunctionTableSqlPostgreSQL) {
    const auto& sql =
            storm::orm::schema::SchemaStatement<Student>::junction_table_sql<storm::orm::schema::Dialect::PostgreSQL>();
    EXPECT_TRUE(sql.contains("CREATE TABLE Student_Course")) << sql;
    EXPECT_TRUE(sql.contains("Student_id BIGINT NOT NULL")) << sql;
    EXPECT_TRUE(sql.contains("Course_id BIGINT NOT NULL")) << sql;
}

// A through-model m2m field must NOT produce an auto junction table.
static_assert(!storm::orm::schema::SchemaStatement<Pupil>::has_m2m_junction_);
static_assert(storm::orm::schema::SchemaStatement<Student>::has_m2m_junction_);

TYPED_TEST(M2MBaseTest, JunctionTableIsCreatedAndWritable) {
    auto conn = QuerySet<Student, TypeParam>::get_default_connection();
    // The fixture ran create_table_if_not_exists<Student> — the junction must exist.
    auto ins = conn->execute("INSERT INTO Student_Course (Student_id, Course_id) VALUES (1, 1)");
    ASSERT_TRUE(ins.has_value()) << ins.error().message();
    // Composite PK rejects duplicate pairs.
    auto dup = conn->execute("INSERT INTO Student_Course (Student_id, Course_id) VALUES (1, 1)");
    EXPECT_FALSE(dup.has_value());
}

// ============================================================================
// SQL shape: 3-table join over a base-table subquery (#203 Phase 1)
// ============================================================================

TYPED_TEST(M2MBaseTest, M2MJoinSqlShape) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template join<^^Student::courses>().select().sql();
    EXPECT_TRUE(sql.contains("SELECT t1.id, t1.name, t1.age, t3.id, t3.title")) << sql;
    EXPECT_TRUE(sql.contains("FROM (SELECT id, name, age FROM Student) t1")) << sql;
    EXPECT_TRUE(sql.contains("INNER JOIN Student_Course t2 ON t1.id = t2.Student_id")) << sql;
    EXPECT_TRUE(sql.contains("INNER JOIN Course t3 ON t2.Course_id = t3.id")) << sql;
    EXPECT_TRUE(sql.ends_with(" ORDER BY t1.id")) << sql;
}

TYPED_TEST(M2MBaseTest, M2MJoinSqlModifiersGoInsideSubquery) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template join<^^Student::courses>()
                       .where(storm::orm::where::field<^^Student::age>() > 18)
                       .template order_by<^^Student::name, false>()
                       .limit(2)
                       .select()
                       .sql();
    // WHERE / ORDER BY / LIMIT select WHICH base entities load — inside the subquery.
    // Outer ORDER BY repeats the user's ordering (t1-qualified) + pk tiebreak.
    // PG dialect adds NULLS LAST after DESC (matches SQLite NULL ordering).
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        EXPECT_TRUE(sql.contains("FROM Student WHERE age > ? ORDER BY name DESC NULLS LAST LIMIT 2) t1")) << sql;
        EXPECT_TRUE(sql.ends_with(" ORDER BY t1.name DESC NULLS LAST, t1.id")) << sql;
    } else {
        EXPECT_TRUE(sql.contains("FROM Student WHERE age > ? ORDER BY name DESC LIMIT 2) t1")) << sql;
        EXPECT_TRUE(sql.ends_with(" ORDER BY t1.name DESC, t1.id")) << sql;
    }
}

TYPED_TEST(M2MBaseTest, M2MJoinSqlMultiFieldOrderByQualifiesAllFields) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql =
            qs.template join<^^Student::courses>().template order_by<^^Student::age, ^^Student::name>().select().sql();
    // every user order field gets the t1. prefix, then the pk tiebreak
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        EXPECT_TRUE(sql.ends_with(" ORDER BY t1.age ASC NULLS FIRST, t1.name ASC NULLS FIRST, t1.id")) << sql;
    } else {
        EXPECT_TRUE(sql.ends_with(" ORDER BY t1.age ASC, t1.name ASC, t1.id")) << sql;
    }
}

TYPED_TEST(M2MBaseTest, M2MLeftJoinSqlShape) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template left_join<^^Student::courses>().select().sql();
    EXPECT_TRUE(sql.contains("LEFT JOIN Student_Course t2")) << sql;
    EXPECT_TRUE(sql.contains("LEFT JOIN Course t3")) << sql;
}

// ============================================================================
// Runtime: eager loading aggregates related objects (#203 Phase 1)
// ============================================================================

// Fixture with seeded data: Alice→[Math, Physics], Bob→[Math], Carol→[].
template <typename ConnType> class M2MSeededTest : public StormTestFixture<Student, ConnType, Course> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
        storm::test::seed_students<ConnType>();
    }
};

TYPED_TEST_SUITE(M2MSeededTest, DatabaseTypes);

// Finds the student with the given name; fails the test if absent.
template <typename Hive> auto find_student(Hive& rows, std::string_view name) -> Student* {
    for (auto& s : rows) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

TYPED_TEST(M2MSeededTest, EagerLoadAggregatesCourses) {
    QuerySet<Student, TypeParam> qs;
    auto                         rows = qs.template join<^^Student::courses>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    // INNER join drops Carol (no courses); 3 joined rows aggregate into 2 students
    ASSERT_EQ(rows->size(), 2U);

    auto* alice = find_student(*rows, "Alice");
    ASSERT_NE(alice, nullptr);
    ASSERT_EQ(alice->courses.size(), 2U);
    EXPECT_EQ(alice->courses[0].title, "Math");
    EXPECT_EQ(alice->courses[1].title, "Physics");
    EXPECT_EQ(alice->age, 20);

    auto* bob = find_student(*rows, "Bob");
    ASSERT_NE(bob, nullptr);
    ASSERT_EQ(bob->courses.size(), 1U);
    EXPECT_EQ(bob->courses[0].title, "Math");
}

TYPED_TEST(M2MSeededTest, LeftJoinKeepsStudentsWithoutCourses) {
    QuerySet<Student, TypeParam> qs;
    auto                         rows = qs.template left_join<^^Student::courses>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 3U);

    auto* carol = find_student(*rows, "Carol");
    ASSERT_NE(carol, nullptr);
    EXPECT_TRUE(carol->courses.empty());
    auto* alice = find_student(*rows, "Alice");
    ASSERT_NE(alice, nullptr);
    EXPECT_EQ(alice->courses.size(), 2U);
}

TYPED_TEST(M2MSeededTest, EmptyResultSet) {
    QuerySet<Student, TypeParam> qs;
    auto                         rows = qs.template join<^^Student::courses>()
                        .where(storm::orm::where::field<^^Student::age>() > 99)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    EXPECT_TRUE(rows->empty());
}

// Aggregates over an m2m join count (student, course) PAIRS — documented behavior.
TYPED_TEST(M2MSeededTest, CountOverM2MJoinCountsPairs) {
    QuerySet<Student, TypeParam> qs;
    auto                         count = qs.template join<^^Student::courses>().count().execute();
    ASSERT_TRUE(count.has_value()) << count.error().message();
    EXPECT_EQ(count.value(), 3);
}

// NOLINTEND(misc-const-correctness)
