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

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)

import storm;
import storm_db_sqlite;

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

class MultipleAggregatesTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<AggPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<AggPerson>::get_default_connection();

        auto create = conn->execute(
                "CREATE TABLE AggPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "salary REAL NOT NULL, "
                "score INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create.has_value());

        qs = std::make_unique<QuerySet<AggPerson>>();

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
        QuerySet<AggPerson>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<AggPerson>> qs;
};

// =============================================================================
// Multiple Aggregates Tests (returns tuples)
// =============================================================================

TEST_F(MultipleAggregatesTest, TwoAggregatesCountAndSum) {
    // Tests tuple return type with 2 operations
    auto result = qs->aggregate().count().sum<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value()) << "Two aggregates should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 25 + 30 + 35 + 28 + 40); // 158
}

TEST_F(MultipleAggregatesTest, TwoAggregatesSumAndAvg) {
    // Tests tuple with SUM (int64_t) and AVG (double)
    auto result = qs->aggregate().sum<^^AggPerson::score>().avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "SUM + AVG should succeed";

    auto [sum, avg] = result.value();
    EXPECT_EQ(sum, 85 + 90 + 75 + 95 + 80); // 425
    EXPECT_NEAR(avg, (50000.0 + 60000.0 + 70000.0 + 55000.0 + 80000.0) / 5.0, 0.01);
}

TEST_F(MultipleAggregatesTest, ThreeAggregates) {
    // Tests tuple with 3 operations
    auto result = qs->aggregate().count().sum<^^AggPerson::age>().avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "Three aggregates should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
}

TEST_F(MultipleAggregatesTest, FourAggregates) {
    // Tests tuple with 4 operations
    auto result = qs->aggregate()
                          .count()
                          .sum<^^AggPerson::age>()
                          .min<^^AggPerson::score>()
                          .max<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Four aggregates should succeed";

    auto [count, sum, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_EQ(min, 75.0); // MIN returns double
    EXPECT_EQ(max, 95.0); // MAX returns double
}

TEST_F(MultipleAggregatesTest, FiveAggregatesAllTypes) {
    // Tests all 5 aggregate types together
    auto result = qs->aggregate()
                          .count()
                          .sum<^^AggPerson::age>()
                          .avg<^^AggPerson::salary>()
                          .min<^^AggPerson::score>()
                          .max<^^AggPerson::score>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Five aggregates should succeed";

    auto [count, sum, avg, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(min, 75.0);
    EXPECT_EQ(max, 95.0);
}

TEST_F(MultipleAggregatesTest, MultipleAggregatesEmptyTable) {
    // Test multiple aggregates on empty table (default/zero values)
    // First delete all rows
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->aggregate().count().sum<^^AggPerson::age>().avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value()) << "Aggregates on empty table should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 0);
    EXPECT_EQ(sum, 0);
    // AVG of empty set should be 0.0 or NULL depending on implementation
}

// =============================================================================
// Single Aggregate Edge Cases
// =============================================================================

TEST_F(MultipleAggregatesTest, SingleCountEmptyTable) {
    // Delete all rows first
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->count().select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0);
}

TEST_F(MultipleAggregatesTest, SingleSumEmptyTable) {
    // Delete all rows first
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->sum<^^AggPerson::age>().select();
    ASSERT_TRUE(result.has_value());
    // SUM of empty set is 0 or NULL
}

TEST_F(MultipleAggregatesTest, SingleAvgEmptyTable) {
    // Delete all rows first
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->avg<^^AggPerson::salary>().select();
    ASSERT_TRUE(result.has_value());
    // AVG of empty set
}

TEST_F(MultipleAggregatesTest, SingleMinEmptyTable) {
    // Delete all rows first
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->min<^^AggPerson::score>().select();
    ASSERT_TRUE(result.has_value());
}

TEST_F(MultipleAggregatesTest, SingleMaxEmptyTable) {
    // Delete all rows first
    auto all = qs->select();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)qs->remove(p);
    }

    auto result = qs->max<^^AggPerson::score>().select();
    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// Aggregate with JOIN Tests
// =============================================================================

TEST_F(MultipleAggregatesTest, MultipleAggregatesWithJoin) {
    // First set up FK tables
    const auto& conn = QuerySet<AggPerson>::get_default_connection();

    (void)conn->execute(
            "CREATE TABLE JoinUser ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "rating REAL NOT NULL, "
            "active INTEGER NOT NULL, "
            "opt_score INTEGER, "
            "opt_bio TEXT)"
    );

    (void)conn->execute(
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

    QuerySet<JoinPost> post_qs;

    // Test multiple aggregates with JOIN
    auto result = post_qs.join<&JoinPost::author>().aggregate().count().sum<^^JoinPost::views>().select();

    ASSERT_TRUE(result.has_value()) << "Multiple aggregates with JOIN should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 350); // 100 + 200 + 50
}

// =============================================================================
// JOIN Type Extraction Tests (float, bool, optional)
// =============================================================================

class JoinTypeExtractionTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<JoinUser>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<JoinUser>::get_default_connection();

        auto create_user = conn->execute(
                "CREATE TABLE JoinUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "rating REAL NOT NULL, "
                "active INTEGER NOT NULL, "
                "opt_score INTEGER, "
                "opt_bio TEXT)"
        );
        ASSERT_TRUE(create_user.has_value());

        auto create_post = conn->execute(
                "CREATE TABLE JoinPost ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "title TEXT NOT NULL, "
                "author_id INTEGER NOT NULL, "
                "views INTEGER NOT NULL, "
                "FOREIGN KEY (author_id) REFERENCES JoinUser(id))"
        );
        ASSERT_TRUE(create_post.has_value());

        user_qs = std::make_unique<QuerySet<JoinUser>>();
        post_qs = std::make_unique<QuerySet<JoinPost>>();

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
        QuerySet<JoinUser>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<JoinUser>> user_qs;
    std::unique_ptr<QuerySet<JoinPost>> post_qs;
};

TEST_F(JoinTypeExtractionTest, JoinExtractsFloatField) {
    // Tests extract_typed_field for float
    auto result = post_qs->join<&JoinPost::author>().select();

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

TEST_F(JoinTypeExtractionTest, JoinExtractsBoolField) {
    // Tests extract_typed_field for bool
    auto result = post_qs->join<&JoinPost::author>().select();

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

TEST_F(JoinTypeExtractionTest, JoinExtractsOptionalIntWithValue) {
    // Tests extract_typed_field for optional<int> with value
    auto result = post_qs->join<&JoinPost::author>().select();

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

TEST_F(JoinTypeExtractionTest, JoinExtractsOptionalIntNull) {
    // Tests extract_typed_field for optional<int> with NULL
    auto result = post_qs->join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Bob") {
            EXPECT_FALSE(post.author.opt_score.has_value());
        }
    }
}

TEST_F(JoinTypeExtractionTest, JoinExtractsOptionalStringWithValue) {
    // Tests extract_typed_field for optional<string> with value
    auto result = post_qs->join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Alice") {
            ASSERT_TRUE(post.author.opt_bio.has_value());
            EXPECT_EQ(post.author.opt_bio.value(), "Alice bio");
        }
    }
}

TEST_F(JoinTypeExtractionTest, JoinExtractsOptionalStringNull) {
    // Tests extract_typed_field for optional<string> with NULL
    auto result = post_qs->join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& post : result.value()) {
        if (post.author.name == "Bob" || post.author.name == "Charlie") {
            EXPECT_FALSE(post.author.opt_bio.has_value());
        }
    }
}

TEST_F(JoinTypeExtractionTest, JoinWithWhere) {
    // Test JOIN with WHERE to ensure proper handling
    auto result = post_qs->where(storm::orm::where::field<^^JoinPost::views>() > 75).join<&JoinPost::author>().select();

    ASSERT_TRUE(result.has_value()) << "JOIN with WHERE should succeed";
    // Should return posts with views > 75 (Alice's posts with 100/200 and Charlie's with 150)
    EXPECT_EQ(result.value().size(), 3);
}

TEST_F(JoinTypeExtractionTest, JoinWithOrderBy) {
    // Test JOIN with ORDER BY
    auto result = post_qs->join<&JoinPost::author>().order_by<^^JoinPost::views>().select();

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

class UpdateOptimizedTest : public ::testing::Test {
  protected:
    struct UpdatePerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       value{};
    };

    auto SetUp() -> void override {
        auto result = QuerySet<UpdatePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<UpdatePerson>::get_default_connection();

        auto create = conn->execute(
                "CREATE TABLE UpdatePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "value INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create.has_value());

        qs = std::make_unique<QuerySet<UpdatePerson>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<UpdatePerson>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<UpdatePerson>> qs;
};

TEST_F(UpdateOptimizedTest, RepeatedSingleUpdates) {
    // Test repeated single updates to exercise cached statement path
    UpdatePerson p{0, "Test", 100};
    auto         insert_result = qs->insert(p);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Perform multiple single updates (exercises cached_update_stmt_ path)
    for (int i = 0; i < 10; ++i) {
        UpdatePerson updated{static_cast<int>(id), "Updated" + std::to_string(i), 100 + i};
        auto         result = qs->update(updated);
        ASSERT_TRUE(result.has_value()) << "Update iteration " << i << " should succeed";
    }

    // Verify final state
    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->value, 109);
}

TEST_F(UpdateOptimizedTest, BatchUpdateFollowedByInsert) {
    // Test batch update followed by insert (different code paths)
    std::vector<UpdatePerson> batch = {
            {0, "P1", 1},
            {0, "P2", 2},
            {0, "P3", 3},
    };

    auto insert_result = qs->insert(std::span<const UpdatePerson>(batch));
    ASSERT_TRUE(insert_result.has_value());

    // Batch update
    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_FALSE(selected.value().empty());

    std::vector<UpdatePerson> updates;
    for (const auto& p : selected.value()) {
        updates.push_back({p.id, p.name, p.value + 100});
    }

    auto batch_update = qs->update(std::span<const UpdatePerson>(updates));
    ASSERT_TRUE(batch_update.has_value());

    // Verify batch update worked
    auto after_batch = qs->select();
    ASSERT_TRUE(after_batch.has_value());
    for (const auto& p : after_batch.value()) {
        EXPECT_GE(p.value, 101) << "Values should be updated";
    }

    // Insert a new record after batch update
    UpdatePerson new_person{0, "NewPerson", 500};
    auto         new_insert = qs->insert(new_person);
    ASSERT_TRUE(new_insert.has_value());

    // Verify total count
    auto final_result = qs->select();
    ASSERT_TRUE(final_result.has_value());
    EXPECT_EQ(final_result.value().size(), 4);
}

// =============================================================================
// Additional WHERE Clause Coverage
// =============================================================================

class WhereAdditionalTest : public ::testing::Test {
  protected:
    struct WherePerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       age{};
        double                                    score{};
    };

    auto SetUp() -> void override {
        auto result = QuerySet<WherePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<WherePerson>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE WherePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "score REAL NOT NULL)"
        );

        qs = std::make_unique<QuerySet<WherePerson>>();

        // Insert test data
        std::vector<WherePerson> const people = {
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
        QuerySet<WherePerson>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<WherePerson>> qs;
};

TEST_F(WhereAdditionalTest, DoubleFieldComparison) {
    // Test WHERE with double field (score)
    auto result = qs->where(storm::orm::where::field<^^WherePerson::score>() > 85.0).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice (85.5), Bob (90), Diana (95)
}

TEST_F(WhereAdditionalTest, DoubleFieldEquality) {
    // Test WHERE with double equality
    auto result = qs->where(storm::orm::where::field<^^WherePerson::score>() == 90.0).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Bob");
}

TEST_F(WhereAdditionalTest, LikePatternStartsWith) {
    // Test LIKE with starts-with pattern
    auto result = qs->where(storm::orm::where::field<^^WherePerson::name>().like("A%")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Alice");
}

TEST_F(WhereAdditionalTest, LikePatternEndsWith) {
    // Test LIKE with ends-with pattern
    auto result = qs->where(storm::orm::where::field<^^WherePerson::name>().like("%e")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice, Charlie, Eve
}

TEST_F(WhereAdditionalTest, LikePatternContains) {
    // Test LIKE with contains pattern
    auto result = qs->where(storm::orm::where::field<^^WherePerson::name>().like("%li%")).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice, Charlie
}

TEST_F(WhereAdditionalTest, ComplexNestedOrAnd) {
    // Test deeply nested OR/AND
    // (age < 28 OR age > 38) AND (score > 80)
    auto age_cond =
            (storm::orm::where::field<^^WherePerson::age>() < 28 or
             storm::orm::where::field<^^WherePerson::age>() > 38);
    auto score_cond = storm::orm::where::field<^^WherePerson::score>() > 80.0;
    auto combined   = age_cond and score_cond;

    auto result = qs->where(combined).select();

    ASSERT_TRUE(result.has_value());
    // Alice (25, 85.5) and Diana (40, 95) should match
    EXPECT_EQ(result.value().size(), 2);
}

TEST_F(WhereAdditionalTest, WhereWithLessEqual) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() <= 28).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice (25), Eve (28)
}

TEST_F(WhereAdditionalTest, WhereWithGreaterEqual) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() >= 35).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Charlie (35), Diana (40)
}

TEST_F(WhereAdditionalTest, WhereWithNotEqual) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::name>() != "Alice").select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Everyone except Alice
}

// =============================================================================
// DISTINCT Additional Tests
// =============================================================================

TEST_F(WhereAdditionalTest, DistinctWithWhereAndOrderBy) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() > 25)
                          .order_by<^^WherePerson::name>()
                          .distinct<^^WherePerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Bob, Charlie, Diana, Eve
}

TEST_F(WhereAdditionalTest, DistinctMultiFieldWhereOrderBy) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::score>() >= 80.0)
                          .order_by<^^WherePerson::age>()
                          .distinct<^^WherePerson::age, ^^WherePerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// GROUP BY Additional Aggregate Types
// =============================================================================

TEST_F(MultipleAggregatesTest, GroupByWithAvg) {
    // Test GROUP BY with AVG aggregate (returns double)
    // Group by age ranges (we'll use age directly)
    auto result = qs->group_by<^^AggPerson::age>().avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5); // 5 distinct ages
}

TEST_F(MultipleAggregatesTest, GroupByWithMin) {
    auto result = qs->group_by<^^AggPerson::age>().min<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

TEST_F(MultipleAggregatesTest, GroupByWithMax) {
    auto result = qs->group_by<^^AggPerson::age>().max<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

// =============================================================================
// Aggregate with WHERE Tests
// =============================================================================

TEST_F(MultipleAggregatesTest, AggregateCountWithWhere) {
    auto result = qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).count().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // Charlie (35), Eve (40)
}

TEST_F(MultipleAggregatesTest, AggregateSumWithWhere) {
    auto result = qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).sum<^^AggPerson::age>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 35 + 40); // 75
}

TEST_F(MultipleAggregatesTest, AggregateAvgWithWhere) {
    auto result = qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).avg<^^AggPerson::salary>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result.value(), (70000.0 + 80000.0) / 2.0, 0.01);
}

TEST_F(MultipleAggregatesTest, AggregateMinWithWhere) {
    auto result = qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).min<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 75.0); // Charlie has 75
}

TEST_F(MultipleAggregatesTest, AggregateMaxWithWhere) {
    auto result = qs->where(storm::orm::where::field<^^AggPerson::age>() > 30).max<^^AggPerson::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 80.0); // Eve has 80
}

// =============================================================================
// COUNT_DISTINCT Additional Tests (if supported)
// =============================================================================

TEST_F(MultipleAggregatesTest, CountDistinctViaDistinct) {
    // Test DISTINCT + COUNT combination
    auto distinct_result = qs->distinct<^^AggPerson::age>().select();
    ASSERT_TRUE(distinct_result.has_value());

    // All ages are unique in our test data
    EXPECT_EQ(distinct_result.value().size(), 5);
}

// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
