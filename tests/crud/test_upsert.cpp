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
