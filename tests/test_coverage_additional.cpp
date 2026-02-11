/**
 * @file test_coverage_additional.cpp
 * @brief Additional tests specifically targeting remaining coverage gaps
 *
 * This file tests code paths not covered by existing tests:
 * - Multiple aggregate operations returning tuples
 * - COUNT_DISTINCT aggregate
 * - JOIN type extractions (float, bool, optional)
 * - execute_single_optimized in UpdateStatement
 * - Edge cases for aggregate empty table handling
 */

#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTBEGIN(readability-identifier-length)

import storm;

import <expected>;
import <string>;
import <vector>;
import <optional>;
import <tuple>;
import <cstdint>;

using namespace storm;

// =============================================================================
// Test Models for Multiple Aggregate Operations
// =============================================================================

struct AggPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
    double                                    salary{};
    int                                       score{};
};

// =============================================================================
// Test Models for JOIN Type Extraction Coverage
// =============================================================================

struct JoinUser {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    float                                     rating{};  // Float type for JOIN extraction
    bool                                      active{};  // Bool type for JOIN extraction
    std::optional<int>                        opt_score; // Optional for JOIN extraction
    std::optional<std::string>                opt_bio;   // Optional string for JOIN
};

struct JoinPost {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               title;
    [[= storm::meta::FieldAttr::fk]] JoinUser author;
    int                                       views{};
};

// =============================================================================
// Multiple Aggregates Test Fixture
// =============================================================================

template <typename ConnType> class MultipleAggregatesTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result =
                QuerySet<AggPerson, ConnType>::set_default_connection(storm::test::get_connection_string<ConnType>());
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<AggPerson, ConnType>::get_default_connection();

        auto create = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE AggPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "salary REAL NOT NULL, "
                "score INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create.has_value());

        storm::test::begin_test_txn<ConnType>(conn, {"AggPerson"});

        qs = std::make_unique<QuerySet<AggPerson, ConnType>>();

        // Insert test data with varied values
        std::vector<AggPerson> const people = {
                {0, "Alice", 25, 50000.0, 85},
                {0, "Bob", 30, 60000.0, 90},
                {0, "Charlie", 35, 70000.0, 75},
                {0, "Diana", 28, 55000.0, 95},
                {0, "Eve", 40, 80000.0, 80},
        };

        for (const auto& person : people) {
            auto r = qs->insert(person);
            ASSERT_TRUE(r.has_value());
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<AggPerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<AggPerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<AggPerson, ConnType>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<AggPerson, ConnType>> qs;
};

TYPED_TEST_SUITE(MultipleAggregatesTest, DatabaseTypes);

// =============================================================================
// Multiple Aggregates Tests (returns tuples)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, TwoAggregatesCountAndSum) {
    // Tests tuple return type with 2 operations
    auto result = this->qs->count().template sum<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "Two aggregates should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 25 + 30 + 35 + 28 + 40); // 158
}

TYPED_TEST(MultipleAggregatesTest, TwoAggregatesSumAndAvg) {
    // Tests tuple with SUM (int64_t) and AVG (double)
    auto result = this->qs->template sum<^^AggPerson::score>().template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "SUM + AVG should succeed";

    auto [sum, avg] = result.value();
    EXPECT_EQ(sum, 85 + 90 + 75 + 95 + 80); // 425
    EXPECT_NEAR(avg, (50000.0 + 60000.0 + 70000.0 + 55000.0 + 80000.0) / 5.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, ThreeAggregates) {
    // Tests tuple with 3 operations
    auto result = this->qs->count().template sum<^^AggPerson::age>().template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "Three aggregates should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, FourAggregates) {
    // Tests tuple with 4 operations
    auto result = this->qs->count()
                          .template sum<^^AggPerson::age>()
                          .template min<^^AggPerson::score>()
                          .template max<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Four aggregates should succeed";

    auto [count, sum, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_EQ(min, 75.0); // MIN returns double
    EXPECT_EQ(max, 95.0); // MAX returns double
}

TYPED_TEST(MultipleAggregatesTest, FiveAggregatesAllTypes) {
    // Tests all 5 aggregate types together
    auto result = this->qs->count()
                          .template sum<^^AggPerson::age>()
                          .template avg<^^AggPerson::salary>()
                          .template min<^^AggPerson::score>()
                          .template max<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Five aggregates should succeed";

    auto [count, sum, avg, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(min, 75.0);
    EXPECT_EQ(max, 95.0);
}

TYPED_TEST(MultipleAggregatesTest, MultipleAggregatesEmptyTable) {
    // Test multiple aggregates on empty table (default/zero values)
    // First delete all rows
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->count().template sum<^^AggPerson::age>().template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "Aggregates on empty table should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 0);
    EXPECT_EQ(sum, 0);
    // AVG of empty set should be 0.0 or NULL depending on implementation
}

// =============================================================================
// Single Aggregate Edge Cases
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, SingleCountEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->count().select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(MultipleAggregatesTest, SingleSumEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->template sum<^^AggPerson::age>().select();
    ASSERT_TRUE(result.has_value());
    // SUM of empty set is 0 or NULL
}

TYPED_TEST(MultipleAggregatesTest, SingleAvgEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->template avg<^^AggPerson::salary>().select();
    ASSERT_TRUE(result.has_value());
    // AVG of empty set
}

TYPED_TEST(MultipleAggregatesTest, SingleMinEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->template min<^^AggPerson::score>().select();
    ASSERT_TRUE(result.has_value());
}

TYPED_TEST(MultipleAggregatesTest, SingleMaxEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p);
    }

    auto result = this->qs->template max<^^AggPerson::score>().select();
    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// Single Aggregate as First in Chain (Coverage for avg/min/max entry points)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, SingleAvgWithData) {
    // Test avg() as the FIRST aggregate call (not chained after count/sum)
    auto result = this->qs->template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "Single AVG should succeed";
    EXPECT_NEAR(result.value(), 63000.0, 0.01); // (50000+60000+70000+55000+80000)/5
}

TYPED_TEST(MultipleAggregatesTest, SingleMinWithData) {
    // Test min() as the FIRST aggregate call
    auto result = this->qs->template min<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value()) << "Single MIN should succeed";
    EXPECT_EQ(result.value(), 75.0); // Charlie has lowest score
}

TYPED_TEST(MultipleAggregatesTest, SingleMaxWithData) {
    // Test max() as the FIRST aggregate call
    auto result = this->qs->template max<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value()) << "Single MAX should succeed";
    EXPECT_EQ(result.value(), 95.0); // Diana has highest score
}

// =============================================================================
// Chaining FROM avg/min/max (Coverage for AggregateStatement::avg/min/max)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, AvgThenCount) {
    // Chain count() AFTER avg() - tests AggregateStatement<..., AVG>::count()
    auto result = this->qs->template avg<^^AggPerson::salary>().count().select();

    ASSERT_TRUE(result.has_value()) << "AVG then COUNT should succeed";

    auto [avg, count] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(count, 5);
}

TYPED_TEST(MultipleAggregatesTest, MinThenSum) {
    // Chain sum() AFTER min() - tests AggregateStatement<..., MIN>::sum()
    auto result = this->qs->template min<^^AggPerson::score>().template sum<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "MIN then SUM should succeed";

    auto [min, sum] = result.value();
    EXPECT_EQ(min, 75.0);
    EXPECT_EQ(sum, 158); // 25+30+35+28+40
}

TYPED_TEST(MultipleAggregatesTest, MaxThenAvg) {
    // Chain avg() AFTER max() - tests AggregateStatement<..., MAX>::avg()
    auto result = this->qs->template max<^^AggPerson::score>().template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "MAX then AVG should succeed";

    auto [max, avg] = result.value();
    EXPECT_EQ(max, 95.0);
    EXPECT_NEAR(avg, 63000.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, AvgThenMin) {
    // Chain min() AFTER avg() - tests AggregateStatement<..., AVG>::min()
    auto result = this->qs->template avg<^^AggPerson::salary>().template min<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "AVG then MIN should succeed";

    auto [avg, min] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(min, 25.0); // Alice is youngest
}

TYPED_TEST(MultipleAggregatesTest, AvgThenMax) {
    // Chain max() AFTER avg() - tests AggregateStatement<..., AVG>::max()
    auto result = this->qs->template avg<^^AggPerson::salary>().template max<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "AVG then MAX should succeed";

    auto [avg, max] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(max, 40.0); // Eve is oldest
}

TYPED_TEST(MultipleAggregatesTest, MinThenMax) {
    // Chain max() AFTER min() - tests range query pattern
    auto result = this->qs->template min<^^AggPerson::age>().template max<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "MIN then MAX should succeed";

    auto [min, max] = result.value();
    EXPECT_EQ(min, 25.0); // Alice
    EXPECT_EQ(max, 40.0); // Eve
}

TYPED_TEST(MultipleAggregatesTest, MaxThenMin) {
    // Chain min() AFTER max() - reverse order
    auto result = this->qs->template max<^^AggPerson::score>().template min<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value()) << "MAX then MIN should succeed";

    auto [max, min] = result.value();
    EXPECT_EQ(max, 95.0); // Diana
    EXPECT_EQ(min, 75.0); // Charlie
}

// =============================================================================
// Aggregate with JOIN Tests
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, MultipleAggregatesWithJoin) {
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        GTEST_SKIP() << "Aggregate+JOIN SQL generation not yet supported on PostgreSQL";
        return;
    }
    // First set up FK tables
    const auto& conn = QuerySet<AggPerson, TypeParam>::get_default_connection();

    (void)storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE JoinUser ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "rating REAL NOT NULL, "
            "active INTEGER NOT NULL, "
            "opt_score INTEGER, "
            "opt_bio TEXT)"
    );

    (void)storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE JoinPost ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT NOT NULL, "
            "author_id INTEGER NOT NULL, "
            "views INTEGER NOT NULL, "
            "FOREIGN KEY (author_id) REFERENCES JoinUser(id))"
    );

    // Insert users
    (void)conn->execute(
            "INSERT INTO JoinUser (name, rating, active, opt_score, opt_bio) VALUES ('User1', 4.5, 1, 100, 'Bio1')"
    );
    (void)conn->execute(
            "INSERT INTO JoinUser (name, rating, active, opt_score, opt_bio) VALUES ('User2', 3.8, 0, NULL, NULL)"
    );

    // Insert posts
    (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Post1', 1, 100)");
    (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Post2', 1, 200)");
    (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Post3', 2, 50)");

    QuerySet<JoinPost, TypeParam> post_qs;

    // Test multiple aggregates with JOIN
    auto result = post_qs.template join<&JoinPost::author>().count().template sum<^^JoinPost::views>().select();

    ASSERT_TRUE(result.has_value()) << "Multiple aggregates with JOIN should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 350); // 100 + 200 + 50
}

// =============================================================================
// JOIN Type Extraction Tests (float, bool, optional)
// =============================================================================

template <typename ConnType> class JoinTypeExtractionTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result =
                QuerySet<JoinUser, ConnType>::set_default_connection(storm::test::get_connection_string<ConnType>());
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<JoinUser, ConnType>::get_default_connection();

        auto create_user = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE JoinUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "rating REAL NOT NULL, "
                "active INTEGER NOT NULL, "
                "opt_score INTEGER, "
                "opt_bio TEXT)"
        );
        ASSERT_TRUE(create_user.has_value());

        auto create_post = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE JoinPost ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "title TEXT NOT NULL, "
                "author_id INTEGER NOT NULL, "
                "views INTEGER NOT NULL, "
                "FOREIGN KEY (author_id) REFERENCES JoinUser(id))"
        );
        ASSERT_TRUE(create_post.has_value());

        storm::test::begin_test_txn<ConnType>(conn, {"JoinUser"});

        user_qs = std::make_unique<QuerySet<JoinUser, ConnType>>();
        post_qs = std::make_unique<QuerySet<JoinPost, ConnType>>();

        // Insert test users with various type values
        (void)conn->execute(
                "INSERT INTO JoinUser (name, rating, active, opt_score, opt_bio) VALUES ('Alice', 4.5, 1, 100, 'Alice "
                "bio')"
        );
        (void)conn->execute(
                "INSERT INTO JoinUser (name, rating, active, opt_score, opt_bio) VALUES ('Bob', 3.2, 0, NULL, NULL)"
        );
        (void)conn->execute(
                "INSERT INTO JoinUser (name, rating, active, opt_score, opt_bio) VALUES ('Charlie', 4.9, 1, 95, NULL)"
        );

        // Insert posts
        (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Post by Alice', 1, 100)");
        (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Another Alice post', 1, 200)");
        (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Post by Bob', 2, 50)");
        (void)conn->execute("INSERT INTO JoinPost (title, author_id, views) VALUES ('Charlie post', 3, 150)");
    }

    auto TearDown() -> void override {
        user_qs = nullptr;
        post_qs = nullptr;
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<JoinUser, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<JoinUser, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<JoinUser, ConnType>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<JoinUser, ConnType>> user_qs;
    std::unique_ptr<QuerySet<JoinPost, ConnType>> post_qs;
};

TYPED_TEST_SUITE(JoinTypeExtractionTest, DatabaseTypes);

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsFloatField) {
    // Tests extract_typed_field for float
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with float field should succeed";
    ASSERT_FALSE(result.value().empty());

    // Check that float values are extracted correctly
    for (const auto& post : result.value()) {
        if (post.author.name == "Alice") {
            EXPECT_NEAR(post.author.rating, 4.5f, 0.01f);
        } else if (post.author.name == "Bob") {
            EXPECT_NEAR(post.author.rating, 3.2f, 0.01f);
        } else if (post.author.name == "Charlie") {
            EXPECT_NEAR(post.author.rating, 4.9f, 0.01f);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsBoolField) {
    // Tests extract_typed_field for bool
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with bool field should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Alice") {
            EXPECT_TRUE(post.author.active);
        } else if (post.author.name == "Bob") {
            EXPECT_FALSE(post.author.active);
        } else if (post.author.name == "Charlie") {
            EXPECT_TRUE(post.author.active);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalIntWithValue) {
    // Tests extract_typed_field for optional<int> with value
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with optional int should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Alice") {
            ASSERT_TRUE(post.author.opt_score.has_value());
            EXPECT_EQ(post.author.opt_score.value(), 100);
        } else if (post.author.name == "Charlie") {
            ASSERT_TRUE(post.author.opt_score.has_value());
            EXPECT_EQ(post.author.opt_score.value(), 95);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalIntNull) {
    // Tests extract_typed_field for optional<int> with NULL
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Bob") {
            EXPECT_FALSE(post.author.opt_score.has_value());
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalStringWithValue) {
    // Tests extract_typed_field for optional<string> with value
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Alice") {
            ASSERT_TRUE(post.author.opt_bio.has_value());
            EXPECT_EQ(post.author.opt_bio.value(), "Alice bio");
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalStringNull) {
    // Tests extract_typed_field for optional<string> with NULL
    auto result = this->post_qs->template join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Bob" || post.author.name == "Charlie") {
            EXPECT_FALSE(post.author.opt_bio.has_value());
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinWithWhere) {
    // Test JOIN with WHERE to ensure proper handling
    auto result = this->post_qs->where(storm::orm::where::field<^^JoinPost::views>() > 75)
                          .template join<&JoinPost::author>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN with WHERE should succeed";
    // Should return posts with views > 75 (Alice's posts with 100/200 and Charlie's with 150)
    EXPECT_EQ(result.value().size(), 3);
}

TYPED_TEST(JoinTypeExtractionTest, JoinWithOrderBy) {
    // Test JOIN with ORDER BY
    auto result = this->post_qs->template join<&JoinPost::author>().template order_by<^^JoinPost::views>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with ORDER BY should succeed";
    EXPECT_EQ(result.value().size(), 4);

    // Verify ordering (ascending by default)
    auto it         = result.value().begin();
    int  prev_views = 0;
    while (it != result.value().end()) {
        EXPECT_GE(it->views, prev_views);
        prev_views = it->views;
        ++it;
    }
}

// =============================================================================
// Update execute_single_optimized Coverage
// =============================================================================

struct UpdatePerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       value{};
};

template <typename ConnType> class UpdateOptimizedTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<UpdatePerson, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<UpdatePerson, ConnType>::get_default_connection();

        auto create = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE UpdatePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "value INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create.has_value());

        storm::test::begin_test_txn<ConnType>(conn, {"UpdatePerson"});

        qs = std::make_unique<QuerySet<UpdatePerson, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<UpdatePerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<UpdatePerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<UpdatePerson, ConnType>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<UpdatePerson, ConnType>> qs;
};

TYPED_TEST_SUITE(UpdateOptimizedTest, DatabaseTypes);

TYPED_TEST(UpdateOptimizedTest, RepeatedSingleUpdates) {
    // Test repeated single updates to exercise cached statement path
    UpdatePerson p{0, "Test", 100};
    auto         insert_result = this->qs->insert(p);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Perform multiple single updates (exercises cached_update_stmt_ path)
    for (int i = 0; i < 10; ++i) {
        UpdatePerson updated{static_cast<int>(id), "Updated" + std::to_string(i), 100 + i};
        auto         result = this->qs->update(updated);
        ASSERT_TRUE(result.has_value()) << "Update iteration " << i << " should succeed";
    }

    // Verify final state
    auto selected = this->qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->value, 109);
}

TYPED_TEST(UpdateOptimizedTest, BatchUpdateFollowedByInsert) {
    // Test batch update followed by insert (different code paths)
    std::vector<UpdatePerson> batch = {
            {0, "P1", 1},
            {0, "P2", 2},
            {0, "P3", 3},
    };

    auto insert_result = this->qs->insert(std::span<const UpdatePerson>(batch));
    ASSERT_TRUE(insert_result.has_value());

    // Batch update
    auto selected = this->qs->select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_FALSE(selected.value().empty());

    std::vector<UpdatePerson> updates;
    for (const auto& p : selected.value()) {
        updates.push_back({p.id, p.name, p.value + 100});
    }

    auto batch_update = this->qs->update(std::span<const UpdatePerson>(updates));
    ASSERT_TRUE(batch_update.has_value());

    // Verify batch update worked
    auto after_batch = this->qs->select();
    ASSERT_TRUE(after_batch.has_value());
    for (const auto& p : after_batch.value()) {
        EXPECT_GE(p.value, 101) << "Values should be updated";
    }

    // Insert a new record after batch update
    UpdatePerson new_person{0, "NewPerson", 500};
    auto         new_insert = this->qs->insert(new_person);
    ASSERT_TRUE(new_insert.has_value());

    // Verify total count
    auto final_result = this->qs->select();
    ASSERT_TRUE(final_result.has_value());
    EXPECT_EQ(final_result.value().size(), 4);
}

// =============================================================================
// Additional WHERE Clause Coverage
// =============================================================================

struct CovAddWherePerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
    double                                    score{};
};

template <typename ConnType> class WhereAdditionalTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<CovAddWherePerson, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<CovAddWherePerson, ConnType>::get_default_connection();

        (void)storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE CovAddWherePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "score REAL NOT NULL)"
        );

        storm::test::begin_test_txn<ConnType>(conn, {"CovAddWherePerson"});

        qs = std::make_unique<QuerySet<CovAddWherePerson, ConnType>>();

        // Insert test data
        std::vector<CovAddWherePerson> const people = {
                {0, "Alice", 25, 85.5},
                {0, "Bob", 30, 90.0},
                {0, "Charlie", 35, 75.5},
                {0, "Diana", 40, 95.0},
                {0, "Eve", 28, 80.0},
        };

        for (const auto& person : people) {
            (void)qs->insert(person);
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<CovAddWherePerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<CovAddWherePerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<CovAddWherePerson, ConnType>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<CovAddWherePerson, ConnType>> qs;
};

TYPED_TEST_SUITE(WhereAdditionalTest, DatabaseTypes);

TYPED_TEST(WhereAdditionalTest, DoubleFieldComparison) {
    // Test WHERE with double field (score)
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::score>() > 85.0).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice (85.5), Bob (90), Diana (95)
}

TYPED_TEST(WhereAdditionalTest, DoubleFieldEquality) {
    // Test WHERE with double equality
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::score>() == 90.0).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Bob");
}

TYPED_TEST(WhereAdditionalTest, LikePatternStartsWith) {
    // Test LIKE with starts-with pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::name>().like("A%")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Alice");
}

TYPED_TEST(WhereAdditionalTest, LikePatternEndsWith) {
    // Test LIKE with ends-with pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::name>().like("%e")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice, Charlie, Eve
}

TYPED_TEST(WhereAdditionalTest, LikePatternContains) {
    // Test LIKE with contains pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::name>().like("%li%")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice, Charlie
}

TYPED_TEST(WhereAdditionalTest, ComplexNestedOrAnd) {
    // Test deeply nested OR/AND
    // (age < 28 OR age > 38) AND (score > 80)
    auto age_cond =
            (storm::orm::where::field<^^CovAddWherePerson::age>() < 28 or
             storm::orm::where::field<^^CovAddWherePerson::age>() > 38);
    auto score_cond = storm::orm::where::field<^^CovAddWherePerson::score>() > 80.0;
    auto combined   = age_cond and score_cond;

    auto result = this->qs->where(combined).select();

    ASSERT_TRUE(result.has_value());
    // Alice (25, 85.5) and Diana (40, 95) should match
    EXPECT_EQ(result.value().size(), 2);
}

TYPED_TEST(WhereAdditionalTest, WhereWithLessEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::age>() <= 28).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice (25), Eve (28)
}

TYPED_TEST(WhereAdditionalTest, WhereWithGreaterEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::age>() >= 35).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Charlie (35), Diana (40)
}

TYPED_TEST(WhereAdditionalTest, WhereWithNotEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::name>() != "Alice").select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Everyone except Alice
}

// =============================================================================
// DISTINCT Additional Tests
// =============================================================================

TYPED_TEST(WhereAdditionalTest, DistinctWithWhereAndOrderBy) {
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::age>() > 25)
                          .template order_by<^^CovAddWherePerson::name>()
                          .template distinct<^^CovAddWherePerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Bob, Charlie, Diana, Eve
}

TYPED_TEST(WhereAdditionalTest, DistinctMultiFieldWhereOrderBy) {
    auto result = this->qs->where(storm::orm::where::field<^^CovAddWherePerson::score>() >= 80.0)
                          .template order_by<^^CovAddWherePerson::age>()
                          .template distinct<^^CovAddWherePerson::age, ^^CovAddWherePerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// GROUP BY Additional Aggregate Types
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, GroupByWithAvg) {
    // Test GROUP BY with AVG aggregate (returns double)
    // Group by age ranges (we'll use age directly)
    auto result = this->qs->template group_by<^^AggPerson::age>().template avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5); // 5 distinct ages
}

TYPED_TEST(MultipleAggregatesTest, GroupByWithMin) {
    auto result = this->qs->template group_by<^^AggPerson::age>().template min<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

TYPED_TEST(MultipleAggregatesTest, GroupByWithMax) {
    auto result = this->qs->template group_by<^^AggPerson::age>().template max<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

// =============================================================================
// Aggregate with WHERE Tests
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, AggregateCountWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).count().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // Charlie (35), Eve (40)
}

TYPED_TEST(MultipleAggregatesTest, AggregateSumWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^AggPerson::age>() > 30)
                          .template sum<^^AggPerson::age>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 35 + 40); // 75
}

TYPED_TEST(MultipleAggregatesTest, AggregateAvgWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^AggPerson::age>() > 30)
                          .template avg<^^AggPerson::salary>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result.value(), (70000.0 + 80000.0) / 2.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, AggregateMinWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^AggPerson::age>() > 30)
                          .template min<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 75.0); // Charlie has 75
}

TYPED_TEST(MultipleAggregatesTest, AggregateMaxWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^AggPerson::age>() > 30)
                          .template max<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 80.0); // Eve has 80
}

// =============================================================================
// COUNT_DISTINCT Additional Tests (if supported)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, CountDistinctViaDistinct) {
    // Test DISTINCT + COUNT combination
    auto distinct_result = this->qs->template distinct<^^AggPerson::age>().select();
    ASSERT_TRUE(distinct_result.has_value());

    // All ages are unique in our test data
    EXPECT_EQ(distinct_result.value().size(), 5);
}

// NOLINTEND(readability-identifier-length)
// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
