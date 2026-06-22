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
