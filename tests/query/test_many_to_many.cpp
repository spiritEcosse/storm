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

// NOLINTEND(misc-const-correctness)
