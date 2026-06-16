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

// ── Referential integrity on the auto-junction (#412) ───────────────────────
// Each junction column carries a FOREIGN KEY ... REFERENCES ... ON DELETE
// CASCADE so deleting an owner/related row removes its junction rows instead of
// leaving orphans. The composite PK still rejects duplicate pairs.

// Both sides must carry FOREIGN KEY ... REFERENCES ... ON DELETE CASCADE; shared
// by the SQLite and PostgreSQL cases (the junction FK DDL is dialect-independent).
namespace {
    void expect_junction_cascade_fks(std::string_view sql) {
        EXPECT_TRUE(sql.contains("FOREIGN KEY (Student_id) REFERENCES Student(id) ON DELETE CASCADE")) << sql;
        EXPECT_TRUE(sql.contains("FOREIGN KEY (Course_id) REFERENCES Course(id) ON DELETE CASCADE")) << sql;
    }
} // namespace

TEST(M2MSchemaTest, JunctionEmitsForeignKeysSqlite) {
    expect_junction_cascade_fks(
            storm::orm::schema::SchemaStatement<Student>::junction_table_sql<storm::orm::schema::Dialect::SQLite>()
    );
}

TEST(M2MSchemaTest, JunctionEmitsForeignKeysPostgreSQL) {
    expect_junction_cascade_fks(
            storm::orm::schema::SchemaStatement<Student>::junction_table_sql<storm::orm::schema::Dialect::PostgreSQL>()
    );
}

// A through-model m2m field must NOT produce an auto junction table.
static_assert(!storm::orm::schema::SchemaStatement<Pupil>::has_m2m_junction_);
static_assert(storm::orm::schema::SchemaStatement<Student>::has_m2m_junction_);

TYPED_TEST(M2MBaseTest, JunctionTableIsCreatedAndWritable) {
    auto conn = QuerySet<Student, TypeParam>::get_default_connection();

    // Seed the referenced rows — the junction FKs (#412) require a real Student and
    // Course before a link row may be inserted.
    QuerySet<Student, TypeParam> student_qs;
    QuerySet<Course, TypeParam>  course_qs;
    auto                         student_id = student_qs.insert(Student{.name = "S", .age = 20}).execute();
    ASSERT_TRUE(student_id.has_value()) << student_id.error().message();
    auto course_id = course_qs.insert(Course{.title = "C"}).execute();
    ASSERT_TRUE(course_id.has_value()) << course_id.error().message();

    const std::string pair = std::format(
            "INSERT INTO Student_Course (Student_id, Course_id) VALUES ({}, {})", student_id.value(), course_id.value()
    );

    // The fixture ran create_table_if_not_exists<Student> — the junction must exist.
    auto ins = conn->execute(pair);
    ASSERT_TRUE(ins.has_value()) << ins.error().message();
    // Composite PK rejects duplicate pairs.
    auto dup = conn->execute(pair);
    EXPECT_FALSE(dup.has_value());

    // A junction row with a dangling FK is rejected (referential integrity, #412).
    auto dangling = conn->execute("INSERT INTO Student_Course (Student_id, Course_id) VALUES (99999, 99999)");
    EXPECT_FALSE(dangling.has_value()) << "Junction FK must reject a dangling owner/related id";
}

// ============================================================================
// SQL shape: two-query predicate-pushdown eager load (#391)
//   Q1 = base entities; Q2 = (owner_pk, related.*) filtered by the same base
//   subquery. .sql() joins them with "; ". The execution path runs them as two
//   separate prepared statements inside a transaction (see execute_m2m_2query).
// ============================================================================

TYPED_TEST(M2MBaseTest, M2MJoinSqlShape) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template join<^^Student::courses>().select().sql();
    // Q1 — base entities, a plain SELECT (no join, no sorter).
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Student; ")) << sql;
    // Q2 — junction⋈related, related rows filtered by the same base subquery.
    EXPECT_TRUE(sql.contains("SELECT t2.Student_id, t3.id, t3.title")) << sql;
    EXPECT_TRUE(sql.contains("FROM Student_Course t2 INNER JOIN Course t3 ON t2.Course_id = t3.id")) << sql;
    EXPECT_TRUE(sql.contains("WHERE t2.Student_id IN (SELECT id FROM Student)")) << sql;
    // No outer ORDER BY / pk-adjacency contract — the stitch is a hash map.
    EXPECT_FALSE(sql.contains(" t1")) << sql;
}

TYPED_TEST(M2MBaseTest, M2MJoinSqlModifiersGoInsideSubquery) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template join<^^Student::courses>()
                       .where(storm::orm::where::f<^^Student::age>() > 18)
                       .template order_by<^^Student::name, false>()
                       .limit(2)
                       .select()
                       .sql();
    // WHERE / ORDER BY / LIMIT select WHICH base entities load — they appear in
    // Q1 directly AND inside Q2's IN-subquery so both pick the same entities.
    // PG dialect adds NULLS LAST after DESC (matches SQLite NULL ordering).
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        EXPECT_TRUE(
                sql.contains("SELECT id, name, age FROM Student WHERE age > ? ORDER BY name DESC NULLS LAST LIMIT 2; ")
        ) << sql;
        EXPECT_TRUE(sql.contains("IN (SELECT id FROM Student WHERE age > ? ORDER BY name DESC NULLS LAST LIMIT 2)"))
                << sql;
    } else {
        EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Student WHERE age > ? ORDER BY name DESC LIMIT 2; "))
                << sql;
        EXPECT_TRUE(sql.contains("IN (SELECT id FROM Student WHERE age > ? ORDER BY name DESC LIMIT 2)")) << sql;
    }
}

TYPED_TEST(M2MBaseTest, M2MJoinSqlMultiFieldOrderByQualifiesAllFields) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql =
            qs.template join<^^Student::courses>().template order_by<^^Student::age, ^^Student::name>().select().sql();
    // Multi-field ORDER BY appears (unqualified — no t1. surgery) in Q1 and the
    // Q2 IN-subquery; no pk tiebreak (the stitch is a hash map, not adjacency).
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        EXPECT_TRUE(sql.contains("ORDER BY age ASC NULLS FIRST, name ASC NULLS FIRST")) << sql;
    } else {
        EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Student ORDER BY age ASC, name ASC; ")) << sql;
        EXPECT_TRUE(sql.contains("IN (SELECT id FROM Student ORDER BY age ASC, name ASC)")) << sql;
    }
}

TYPED_TEST(M2MBaseTest, M2MLeftJoinSqlShape) {
    QuerySet<Student, TypeParam> qs;
    auto                         sql = qs.template left_join<^^Student::courses>().select().sql();
    // Q2 is always an INNER junction⋈related join — LEFT vs INNER is a post-stitch
    // filter (LEFT keeps zero-relation entities), never an SQL difference.
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Student; ")) << sql;
    EXPECT_TRUE(sql.contains("FROM Student_Course t2 INNER JOIN Course t3")) << sql;
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

// Referential integrity (#412): deleting an owner cascades to its junction rows.
// Alice (id 1) owns two Student_Course links (Math, Physics); deleting her must
// leave no orphaned junction rows for Student_id = 1 (ON DELETE CASCADE).
TYPED_TEST(M2MSeededTest, DeletingOwnerCascadesJunctionRows) {
    auto conn = QuerySet<Student, TypeParam>::get_default_connection();

    auto before = conn->prepare("SELECT COUNT(*) FROM Student_Course WHERE Student_id = 1");
    ASSERT_TRUE(before.has_value());
    auto before_stmt = std::move(before.value());
    ASSERT_EQ(before_stmt.step_raw(), decltype(before_stmt)::ROW_AVAILABLE);
    ASSERT_EQ(before_stmt.extract_int64(0), 2) << "Alice should start with two junction rows";

    QuerySet<Student, TypeParam> sqs;
    auto                         del = sqs.where(storm::orm::where::f<^^Student::id>() == 1).erase().execute();
    ASSERT_TRUE(del.has_value()) << "Deleting Alice failed: " << del.error().message();

    auto after = conn->prepare("SELECT COUNT(*) FROM Student_Course WHERE Student_id = 1");
    ASSERT_TRUE(after.has_value());
    auto after_stmt = std::move(after.value());
    ASSERT_EQ(after_stmt.step_raw(), decltype(after_stmt)::ROW_AVAILABLE);
    EXPECT_EQ(after_stmt.extract_int64(0), 0) << "Junction rows must cascade away when the owner is deleted";
}

// #414 / #9 — the m2m two-query prefetch opens its own TransactionGuard for
// snapshot consistency. Inside an outer storm::begin() scope it must not fail on
// a nested BEGIN; the prefetch goes passive and the outer guard owns the commit.
TYPED_TEST(M2MSeededTest, EagerLoadInsideOuterTransactionSucceeds) {
    QuerySet<Student, TypeParam> qs;
    auto                         txn = storm::begin(QuerySet<Student, TypeParam>::get_default_connection());
    ASSERT_TRUE(txn.has_value()) << "storm::begin should start a transaction";

    auto rows = qs.template join<^^Student::courses>().select().execute();
    ASSERT_TRUE(rows.has_value()) << "m2m prefetch inside outer txn must not fail on nested BEGIN: "
                                  << (rows ? "" : rows.error().message());
    ASSERT_EQ(rows->size(), 2U);

    ASSERT_TRUE(txn->commit().has_value()) << "outer commit should succeed";
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
                        .where(storm::orm::where::f<^^Student::age>() > 99)
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

// ============================================================================
// Container coverage (#203 Phase 1 edge cases): plf::hive and shared_ptr elements
// ============================================================================

template <typename ConnType> class M2MContainerTest : public StormTestFixture<Playlist, ConnType, Track, Album> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        QuerySet<Playlist, ConnType> pqs;
        ASSERT_TRUE(pqs.insert(Playlist{.name = "Road trip"}).execute().has_value());
        QuerySet<Album, ConnType> aqs;
        ASSERT_TRUE(aqs.insert(Album{.name = "Greatest hits"}).execute().has_value());
        QuerySet<Track, ConnType> tqs;
        std::vector<Track> const  tracks = {{.title = "Intro"}, {.title = "Outro"}};
        ASSERT_TRUE(tqs.insert(std::span<const Track>(tracks)).execute().has_value());
        for (const auto* sql :
             {"INSERT INTO Playlist_Track (Playlist_id, Track_id) VALUES (1, 1)",
              "INSERT INTO Playlist_Track (Playlist_id, Track_id) VALUES (1, 2)",
              "INSERT INTO Album_Track (Album_id, Track_id) VALUES (1, 2)"}) {
            ASSERT_TRUE(conn->execute(sql).has_value());
        }
    }
};

TYPED_TEST_SUITE(M2MContainerTest, DatabaseTypes);

TYPED_TEST(M2MContainerTest, HiveContainerEagerLoad) {
    QuerySet<Playlist, TypeParam> qs;
    auto                          rows = qs.template join<^^Playlist::tracks>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    ASSERT_EQ(rows->begin()->tracks.size(), 2U); // appended via hive insert()
    auto track_it = rows->begin()->tracks.begin();
    EXPECT_EQ(track_it->title, "Intro");
}

TYPED_TEST(M2MContainerTest, SharedPtrElementsEagerLoad) {
    QuerySet<Album, TypeParam> qs;
    auto                       rows = qs.template join<^^Album::tracks>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    ASSERT_EQ(rows->begin()->tracks.size(), 1U);
    ASSERT_NE(rows->begin()->tracks[0], nullptr); // wrapped via make_shared
    EXPECT_EQ(rows->begin()->tracks[0]->title, "Outro");
}

// NOLINTEND(misc-const-correctness)
