#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

using storm::QuerySet;
using storm::orm::where::field;

#include "test_models.h"            // NOSONAR cpp:S954
#include "test_reverse_fk_models.h" // NOSONAR cpp:S954

namespace stmt = storm::orm::statements;

// ============================================================================
// Compile-time: reverse_fk container fields are excluded from persisted members
// ============================================================================

static_assert(stmt::BaseStatement<RfPerson>::field_count_ == 3, "tasks (reverse_fk) is not a persisted column");
static_assert(stmt::BaseStatement<RfTask>::field_count_ == 3); // id, title, assignee
static_assert(stmt::BaseStatement<RfReporter>::field_count_ == 2);
static_assert(stmt::BaseStatement<RfBug>::field_count_ == 4); // id, summary, author, reviewer
static_assert(stmt::BaseStatement<RfBoard>::field_count_ == 2, "notes (reverse_fk) is not a persisted column");
static_assert(stmt::BaseStatement<RfPerson>::has_reverse_fk_field_);
static_assert(!stmt::BaseStatement<RfTask>::has_reverse_fk_field_);

// ============================================================================
// Compile-time: reverse-FK concepts
// ============================================================================

// Annotated container destination (select path)
static_assert(stmt::ReverseFKFieldOf<RfPerson, ^^RfPerson::tasks>);
static_assert(stmt::ReverseFKFieldOf<RfBoard, ^^RfBoard::notes>);
static_assert(!stmt::ReverseFKFieldOf<RfPerson, ^^RfPerson::name>);

// Cross-model FK selector (aggregate/filter path)
static_assert(stmt::ReverseFKSelector<RfPerson, ^^RfTask::assignee>);
static_assert(stmt::ReverseFKSelector<RfReporter, ^^RfBug::author>);
static_assert(stmt::ReverseFKSelector<RfReporter, ^^RfBug::reviewer>);
static_assert(!stmt::ReverseFKSelector<RfPerson, ^^RfTask::title>);   // not an FK
static_assert(!stmt::ReverseFKSelector<RfPerson, ^^RfPerson::tasks>); // same model, not cross-model FK

// JoinableFields accepts both reverse-FK forms
static_assert(stmt::JoinableFields<RfPerson, ^^RfPerson::tasks>);
static_assert(stmt::JoinableFields<RfPerson, ^^RfTask::assignee>);

// ============================================================================
// Schema: reverse_fk creates NO junction table and is not a column
// ============================================================================

static_assert(!storm::orm::schema::SchemaStatement<RfPerson>::has_m2m_junction_);

TEST(ReverseFKSchemaTest, BaseTableHasNoReverseContainerColumn) {
    const auto& sql = storm::create_table_sql<RfPerson>();
    EXPECT_FALSE(sql.contains("tasks")) << sql;
    EXPECT_TRUE(sql.contains("name")) << sql;
    EXPECT_TRUE(sql.contains("age")) << sql;
}

TEST(ReverseFKSchemaTest, OwnerTableHasFkColumn) {
    const auto& sql = storm::create_table_sql<RfTask>();
    EXPECT_TRUE(sql.contains("assignee_id")) << sql;
}

// ============================================================================
// Runtime fixtures
// ============================================================================

template <typename ConnType> class ReverseFKTest : public StormTestFixture<RfPerson, ConnType, RfTask> {
  public:
    void on_after_setup(const std::shared_ptr<ConnType>& conn) override {
        (void)conn;
        storm::test::seed_rf_people<ConnType>();
    }

    // Shared assertion: the first two rows are Alice (2 tasks) then Bob (1 task).
    // Used by both INNER and LEFT join tests (they agree on the present rows; LEFT
    // additionally keeps Carol, asserted separately).
    static void expect_alice_then_bob(plf::hive<RfPerson>& rows) {
        auto it = rows.begin();
        EXPECT_EQ(it->name, "Alice");
        EXPECT_EQ(it->tasks.size(), 2U);
        ++it;
        EXPECT_EQ(it->name, "Bob");
        EXPECT_EQ(it->tasks.size(), 1U);
    }
};

TYPED_TEST_SUITE(ReverseFKTest, DatabaseTypes);

// Plain CRUD ignores the reverse_fk container.
TYPED_TEST(ReverseFKTest, PlainSelectDoesNotPopulateReverseContainer) {
    auto rows = QuerySet<RfPerson, TypeParam>().template order_by<^^RfPerson::id>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 3U);
    for (const auto& p : *rows) {
        EXPECT_TRUE(p.tasks.empty()) << "plain select must not eager-load reverse_fk";
    }
}

// LEFT join: every person, container filled (empty when none).
TYPED_TEST(ReverseFKTest, LeftJoinPopulatesAndKeepsEmpty) {
    auto rows = QuerySet<RfPerson, TypeParam>()
                        .template left_join<^^RfPerson::tasks>()
                        .template order_by<^^RfPerson::id>()
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 3U);
    TestFixture::expect_alice_then_bob(*rows);
    auto carol = rows->begin();
    ++carol;
    ++carol; // plf::hive: std::next is ambiguous (plf has its own), advance by hand
    EXPECT_EQ(carol->name, "Carol");
    EXPECT_TRUE(carol->tasks.empty()); // LEFT keeps zero-task person
}

// INNER join: drops persons with no tasks.
TYPED_TEST(ReverseFKTest, InnerJoinDropsZeroRelationEntities) {
    auto rows = QuerySet<RfPerson, TypeParam>()
                        .template join<^^RfPerson::tasks>()
                        .template order_by<^^RfPerson::id>()
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 2U); // Carol dropped
    TestFixture::expect_alice_then_bob(*rows);
}

// Owner rows carry their columns (title) and the FK pk.
TYPED_TEST(ReverseFKTest, OwnerColumnsExtracted) {
    auto rows = QuerySet<RfPerson, TypeParam>()
                        .where(field<^^RfPerson::id>() == 1)
                        .template left_join<^^RfPerson::tasks>()
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    const auto& alice = *rows->begin();
    ASSERT_EQ(alice.tasks.size(), 2U);
    std::vector<std::string> titles;
    for (const auto& t : alice.tasks) {
        titles.emplace_back(t.title);
        EXPECT_EQ(t.assignee.id, 1); // FK pk threaded back
    }
    std::ranges::sort(titles);
    EXPECT_EQ(titles, (std::vector<std::string>{"T1", "T2"}));
}

// WHERE bounds the BASE entities (the loaded persons), not the tasks.
TYPED_TEST(ReverseFKTest, WhereAppliesToBaseEntities) {
    auto rows = QuerySet<RfPerson, TypeParam>()
                        .where(field<^^RfPerson::age>() < 25)
                        .template left_join<^^RfPerson::tasks>()
                        .template order_by<^^RfPerson::id>()
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 2U); // Alice + Bob (age < 25)
    EXPECT_EQ(rows->begin()->tasks.size(), 2U);
}

// LIMIT bounds base entities.
TYPED_TEST(ReverseFKTest, LimitBoundsBaseEntities) {
    auto rows = QuerySet<RfPerson, TypeParam>()
                        .template left_join<^^RfPerson::tasks>()
                        .template order_by<^^RfPerson::id>()
                        .limit(1)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Alice");
    EXPECT_EQ(rows->begin()->tasks.size(), 2U);
}

// Aggregate over the cross-model FK selector: tasks-per-person including zeros.
TYPED_TEST(ReverseFKTest, LeftJoinAggregateCountIncludesZeroGroups) {
    auto total = QuerySet<RfPerson, TypeParam>().template left_join<^^RfTask::assignee>().count().execute();
    ASSERT_TRUE(total.has_value()) << total.error().message();
    // 3 task rows + 1 NULL row for Carol (LEFT) = 4.
    EXPECT_EQ(*total, 4);
}

// ============================================================================
// Disambiguation fixture (RfReporter / RfBug)
// ============================================================================

template <typename ConnType> class ReverseFKDisambigTest : public StormTestFixture<RfReporter, ConnType, RfBug> {
  public:
    void on_after_setup(const std::shared_ptr<ConnType>& conn) override {
        storm::QuerySet<RfReporter, ConnType> rqs;
        std::vector<RfReporter> const         reporters = {{.name = "Rita"}, {.name = "Sam"}};
        ASSERT_TRUE(rqs.insert(std::span<const RfReporter>(reporters)).execute().has_value());

        storm::QuerySet<RfBug, ConnType> bqs;
        // Bug1 authored by Rita(1), reviewed by Sam(2); Bug2 authored by Sam(2), reviewed by Rita(1).
        std::vector<RfBug> const bugs =
                {{.summary = "B1", .author = {.id = 1}, .reviewer = {.id = 2}},
                 {.summary = "B2", .author = {.id = 2}, .reviewer = {.id = 1}}};
        ASSERT_TRUE(bqs.insert(std::span<const RfBug>(bugs)).execute().has_value());
        (void)conn;
    }
};

TYPED_TEST_SUITE(ReverseFKDisambigTest, DatabaseTypes);

// Cross-model FK selector disambiguates between the owner's two FKs back at the
// base: counting over RfBug::author vs RfBug::reviewer keys on different columns
// (here both have 2 rows, but the selector picks distinct FK columns in the SQL).
TYPED_TEST(ReverseFKDisambigTest, SelectorDisambiguatesFkInAggregate) {
    auto by_author = QuerySet<RfReporter, TypeParam>().template join<^^RfBug::author>().count().execute();
    ASSERT_TRUE(by_author.has_value()) << by_author.error().message();
    EXPECT_EQ(*by_author, 2); // two bugs, each with an author

    auto by_reviewer = QuerySet<RfReporter, TypeParam>().template join<^^RfBug::reviewer>().count().execute();
    ASSERT_TRUE(by_reviewer.has_value()) << by_reviewer.error().message();
    EXPECT_EQ(*by_reviewer, 2); // two bugs, each with a reviewer
}

// The two selectors generate different ON columns (author_id vs reviewer_id).
TEST(ReverseFKDisambigSqlTest, DistinctFkColumnsInJoinSql) {
    using AuthorJS = stmt::
            ReverseFKJoinStatement<RfReporter, storm::db::sqlite::Connection, stmt::JoinType::Left, ^^RfBug::author>;
    using ReviewerJS = stmt::
            ReverseFKJoinStatement<RfReporter, storm::db::sqlite::Connection, stmt::JoinType::Left, ^^RfBug::reviewer>;
    EXPECT_TRUE(AuthorJS::get_complete_sql().contains("t2.author_id = t1.id")) << AuthorJS::get_complete_sql();
    EXPECT_TRUE(ReviewerJS::get_complete_sql().contains("t2.reviewer_id = t1.id")) << ReviewerJS::get_complete_sql();
}

// NOLINTEND(misc-const-correctness)
