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
    constexpr auto    target = UpsertGrammar<Person>::build_conflict_target<^^Person::name>();
    const std::string s(target);
    EXPECT_EQ(s, "(name)");
}

// Grammar emits col=excluded.col for each listed SET column.
TEST(UpsertGrammarTest, ExcludedSetClauseMultipleColumns) {
    constexpr auto    set = UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age, ^^Person::salary>();
    const std::string s(set);
    EXPECT_EQ(s, "age=excluded.age, salary=excluded.salary");
}

// Full DO NOTHING statement for Person conflicting on name.
TEST(UpsertGrammarTest, FullSqlDoNothing) {
    const std::string& sql = UpsertGrammar<Person>::nothing_sql<^^Person::name>();
    EXPECT_TRUE(sql.starts_with("INSERT INTO Person ")) << sql;
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}

// Full DO UPDATE statement, conflict on name, set age.
TEST(UpsertGrammarTest, FullSqlDoUpdate) {
    const std::string sql = UpsertGrammar<Person>::update_sql<^^Person::name>(
            UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age>()
    );
    EXPECT_NE(sql.find("ON CONFLICT (name) DO UPDATE SET age=excluded.age"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}

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

TEST(UpsertGrammarTest, ConstraintsCompile) {
    SUCCEED();
}

template <typename ConnType> class UpsertTest : public StormTestFixture<Person, ConnType> {};
TYPED_TEST_SUITE(UpsertTest, DatabaseTypes);

// Seeds the initial "Zed" row via the DO NOTHING proxy and returns its id.
// Shared by both conflict tests below so each only adds its own conflicting insert.
template <typename ConnType> auto seed_zed(storm::QuerySet<Person, ConnType>& qs) -> std::int64_t {
    Person const first{.name = "Zed", .age = 1, .department = "X"};
    auto         first_id = qs.insert(first).template on_conflict<^^Person::name>().nothing().execute();
    EXPECT_TRUE(first_id.has_value());
    EXPECT_TRUE(first_id.value().has_value());
    return first_id.value().value();
}

// DO NOTHING via the fluent proxy: first insert lands, the conflicting second
// insert is skipped (no row touched), leaving the original row untouched.
TYPED_TEST(UpsertTest, DoNothingSkipsOnConflict) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    seed_zed(qs);

    Person const conflicting{.name = "Zed", .age = 99, .department = "Y"};
    auto         second = qs.insert(conflicting).template on_conflict<^^Person::name>().nothing().execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second.value().has_value()); // skipped — no row touched

    auto rows = qs.where(f<^^Person::name>() == "Zed").select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value().begin()->age, 1); // untouched by the conflicting insert
}

// DO UPDATE via the fluent proxy: the conflicting insert overwrites the listed
// column (age) on the existing row.
TYPED_TEST(UpsertTest, DoUpdateOverwritesListedColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    const std::int64_t                 first_id = seed_zed(qs);

    Person const conflicting{.name = "Zed", .age = 99, .department = "Y"};
    auto         updated_id =
            qs.insert(conflicting).template on_conflict<^^Person::name>().template update<^^Person::age>().execute();
    ASSERT_TRUE(updated_id.has_value());
    EXPECT_EQ(updated_id.value(), first_id);

    auto rows = qs.where(f<^^Person::name>() == "Zed").select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value().begin()->age, 99); // overwritten by the conflicting insert
}

// .sql() golden — DO NOTHING shape via the fluent proxy.
TYPED_TEST(UpsertTest, SqlGoldenNothing) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const                       row{.name = "Q", .age = 1, .department = "X"};
    const std::string                  sql = qs.insert(row).template on_conflict<^^Person::name>().nothing().sql();
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
}
