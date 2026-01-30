/**
 * @file test_coverage_gaps.cpp
 * @brief Tests specifically targeting uncovered code paths for improved coverage
 *
 * This file tests edge cases and code paths that weren't covered by existing tests:
 * - GROUP BY + ORDER BY combinations
 * - GROUP BY + LIMIT + OFFSET combinations
 * - Aggregate execution paths with modifiers
 * - Base statement utility functions
 */

#include <gtest/gtest.h>

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTBEGIN(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)

import storm;
import storm_db_sqlite;

import <expected>;
import <string>;
import <vector>;
import <optional>;
import <tuple>;

using namespace storm;

// =============================================================================
// Test Models
// =============================================================================

struct CovPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
    std::string                               department;
    double                                    salary{};
};

struct CovUser {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       score{};
};

struct CovMessage {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               content;
    int                                       value{};
    [[= storm::meta::FieldAttr::fk]] CovUser  sender;
};

// =============================================================================
// Test Fixture
// =============================================================================

class CoverageGapsTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<CovPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<CovPerson>::get_default_connection();

        // Create CovPerson table
        auto create_person = conn->execute(
                "CREATE TABLE CovPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "department TEXT NOT NULL, "
                "salary REAL NOT NULL)"
        );
        ASSERT_TRUE(create_person.has_value());

        // Create CovUser table
        auto create_user = conn->execute(
                "CREATE TABLE CovUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "score INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create_user.has_value());

        // Create CovMessage table with FK
        auto create_msg = conn->execute(
                "CREATE TABLE CovMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "content TEXT NOT NULL, "
                "value INTEGER NOT NULL, "
                "sender_id INTEGER NOT NULL, "
                "FOREIGN KEY (sender_id) REFERENCES CovUser(id))"
        );
        ASSERT_TRUE(create_msg.has_value());

        qs     = std::make_unique<QuerySet<CovPerson>>();
        msg_qs = std::make_unique<QuerySet<CovMessage>>();

        insert_test_data();
    }

    auto TearDown() -> void override {
        qs     = nullptr;
        msg_qs = nullptr;
        QuerySet<CovPerson>::clear_default_connection();
    }

    auto insert_test_data() -> void {
        // Insert persons with varied departments for GROUP BY tests
        std::vector<CovPerson> const people = {
                {0, "Alice", 25, "Engineering", 80000.0},
                {0, "Bob", 30, "Engineering", 90000.0},
                {0, "Charlie", 35, "Sales", 70000.0},
                {0, "Diana", 28, "Sales", 75000.0},
                {0, "Eve", 40, "Marketing", 85000.0},
                {0, "Frank", 45, "Marketing", 95000.0},
                {0, "Grace", 32, "Engineering", 88000.0},
                {0, "Henry", 38, "HR", 65000.0},
        };

        for (const auto& person : people) {
            auto r = qs->insert(person);
            ASSERT_TRUE(r.has_value());
        }

        // Insert users for JOIN tests
        const auto& conn = QuerySet<CovPerson>::get_default_connection();

        (void)conn->execute("INSERT INTO CovUser (name, score) VALUES ('User1', 100)");
        (void)conn->execute("INSERT INTO CovUser (name, score) VALUES ('User2', 200)");
        (void)conn->execute("INSERT INTO CovUser (name, score) VALUES ('User3', 150)");

        // Insert messages
        (void)conn->execute("INSERT INTO CovMessage (content, value, sender_id) VALUES ('Msg1', 10, 1)");
        (void)conn->execute("INSERT INTO CovMessage (content, value, sender_id) VALUES ('Msg2', 20, 1)");
        (void)conn->execute("INSERT INTO CovMessage (content, value, sender_id) VALUES ('Msg3', 30, 2)");
        (void)conn->execute("INSERT INTO CovMessage (content, value, sender_id) VALUES ('Msg4', 40, 2)");
        (void)conn->execute("INSERT INTO CovMessage (content, value, sender_id) VALUES ('Msg5', 50, 3)");
    }

    std::unique_ptr<QuerySet<CovPerson>>  qs;
    std::unique_ptr<QuerySet<CovMessage>> msg_qs;
};

// =============================================================================
// GROUP BY + ORDER BY Tests (execute_simple with modifiers path)
// =============================================================================

TEST_F(CoverageGapsTest, GroupByWithOrderByAscending) {
    // GROUP BY + ORDER BY - tests has_modifiers=true path in execute_simple()
    auto result = qs->order_by<^^CovPerson::department>().group_by<^^CovPerson::department>().count().select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());

    // Results should be ordered by department alphabetically
    auto        it   = result.value().begin();
    std::string prev = "";
    while (it != result.value().end()) {
        const auto& [dept, count] = *it;
        EXPECT_GE(dept, prev) << "Results should be ordered ascending by department";
        prev = dept;
        ++it;
    }
}

TEST_F(CoverageGapsTest, GroupByWithOrderByDescending) {
    // GROUP BY + ORDER BY DESC (false = descending)
    auto result = qs->order_by<^^CovPerson::department, false>().group_by<^^CovPerson::department>().count().select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY DESC should succeed";
    ASSERT_FALSE(result.value().empty());

    // Results should be ordered by department in descending order
    auto        it   = result.value().begin();
    std::string prev = "ZZZZZZ"; // Start with high value
    while (it != result.value().end()) {
        const auto& [dept, count] = *it;
        EXPECT_LE(dept, prev) << "Results should be ordered descending by department";
        prev = dept;
        ++it;
    }
}

TEST_F(CoverageGapsTest, GroupByWithOrderByAndLimit) {
    // GROUP BY + ORDER BY + LIMIT - tests full modifiers path
    auto result = qs->order_by<^^CovPerson::department>().limit(2).group_by<^^CovPerson::department>().count().select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 2) << "Should return exactly 2 groups";
}

TEST_F(CoverageGapsTest, GroupByWithOrderByLimitOffset) {
    // GROUP BY + ORDER BY + LIMIT + OFFSET - complete modifiers test
    auto result = qs->order_by<^^CovPerson::department>()
                          .limit(2)
                          .offset(1)
                          .group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY + LIMIT + OFFSET should succeed";
    EXPECT_EQ(result.value().size(), 2) << "Should return 2 groups after offset";
}

TEST_F(CoverageGapsTest, GroupByWithOffsetOnly) {
    // GROUP BY + OFFSET (uses LIMIT -1 internally)
    auto result = qs->offset(1).group_by<^^CovPerson::department>().count().select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + OFFSET should succeed";
    // We have 4 departments, offset 1 should give us 3
    EXPECT_EQ(result.value().size(), 3);
}

// =============================================================================
// GROUP BY + SUM + ORDER BY (different aggregate types with modifiers)
// =============================================================================

TEST_F(CoverageGapsTest, GroupBySumWithOrderBy) {
    auto result = qs->order_by<^^CovPerson::department>()
                          .group_by<^^CovPerson::department>()
                          .sum<^^CovPerson::salary>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());
}

TEST_F(CoverageGapsTest, GroupByAvgWithOrderByAndLimit) {
    auto result = qs->order_by<^^CovPerson::department>()
                          .limit(3)
                          .group_by<^^CovPerson::department>()
                          .avg<^^CovPerson::salary>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + AVG + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 3);
}

TEST_F(CoverageGapsTest, GroupByMinMaxWithModifiers) {
    // Test MIN aggregate with full modifiers
    auto min_result = qs->order_by<^^CovPerson::department>()
                              .limit(2)
                              .offset(1)
                              .group_by<^^CovPerson::department>()
                              .min<^^CovPerson::age>()
                              .select();

    ASSERT_TRUE(min_result.has_value()) << "GROUP BY + MIN with modifiers should succeed";
    EXPECT_EQ(min_result.value().size(), 2);

    // Test MAX aggregate with full modifiers
    auto max_result = qs->order_by<^^CovPerson::department>()
                              .limit(2)
                              .offset(1)
                              .group_by<^^CovPerson::department>()
                              .max<^^CovPerson::age>()
                              .select();

    ASSERT_TRUE(max_result.has_value()) << "GROUP BY + MAX with modifiers should succeed";
    EXPECT_EQ(max_result.value().size(), 2);
}

// =============================================================================
// GROUP BY + WHERE + ORDER BY (execute_where_impl with modifiers)
// =============================================================================

TEST_F(CoverageGapsTest, GroupByWhereWithOrderBy) {
    // WHERE + GROUP BY + ORDER BY
    auto result = qs->where(storm::orm::where::field<^^CovPerson::age>() > 30)
                          .order_by<^^CovPerson::department>()
                          .group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + GROUP BY + ORDER BY should succeed";

    // All results should have age > 30
    for (const auto& [dept, count] : result.value()) {
        EXPECT_GT(count, 0) << "Each group should have at least one person";
    }
}

TEST_F(CoverageGapsTest, GroupByWhereWithOrderByAndLimit) {
    // WHERE + GROUP BY + ORDER BY + LIMIT
    auto result = qs->where(storm::orm::where::field<^^CovPerson::salary>() > 70000.0)
                          .order_by<^^CovPerson::department>()
                          .limit(2)
                          .group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_LE(result.value().size(), 2);
}

// =============================================================================
// GROUP BY + JOIN + ORDER BY (execute_join_impl with modifiers)
// =============================================================================

TEST_F(CoverageGapsTest, GroupByJoinWithOrderBy) {
    // JOIN + GROUP BY + ORDER BY (group by value, an int field)
    auto result = msg_qs->join<&CovMessage::sender>()
                          .order_by<^^CovMessage::value>()
                          .group_by<^^CovMessage::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());
}

TEST_F(CoverageGapsTest, GroupByJoinWithOrderByAndLimit) {
    // JOIN + GROUP BY + ORDER BY + LIMIT
    auto result = msg_qs->join<&CovMessage::sender>()
                          .order_by<^^CovMessage::value>()
                          .limit(3)
                          .group_by<^^CovMessage::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_LE(result.value().size(), 3);
}

TEST_F(CoverageGapsTest, GroupByJoinSumWithModifiers) {
    // JOIN + GROUP BY + SUM + ORDER BY + LIMIT + OFFSET
    auto result = msg_qs->join<&CovMessage::sender>()
                          .order_by<^^CovMessage::content>()
                          .limit(3)
                          .offset(0)
                          .group_by<^^CovMessage::content>()
                          .sum<^^CovMessage::value>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + SUM with modifiers should succeed";
}

// =============================================================================
// GROUP BY + WHERE + JOIN + ORDER BY (execute_where_join_impl with modifiers)
// =============================================================================

TEST_F(CoverageGapsTest, GroupByWhereJoinWithOrderBy) {
    // WHERE + JOIN + GROUP BY + ORDER BY (full chain with modifiers)
    auto result = msg_qs->where(storm::orm::where::field<^^CovMessage::value>() > 10)
                          .join<&CovMessage::sender>()
                          .order_by<^^CovMessage::value>()
                          .group_by<^^CovMessage::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + GROUP BY + ORDER BY should succeed";
}

TEST_F(CoverageGapsTest, GroupByWhereJoinWithAllModifiers) {
    // WHERE + JOIN + GROUP BY + ORDER BY + LIMIT + OFFSET
    auto result = msg_qs->where(storm::orm::where::field<^^CovMessage::value>() >= 20)
                          .join<&CovMessage::sender>()
                          .order_by<^^CovMessage::content>()
                          .limit(3)
                          .offset(0)
                          .group_by<^^CovMessage::content>()
                          .sum<^^CovMessage::value>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Full chain with all modifiers should succeed";
}

// =============================================================================
// Multi-field GROUP BY with modifiers
// =============================================================================

TEST_F(CoverageGapsTest, MultiFieldGroupByWithOrderBy) {
    // Multi-field GROUP BY + ORDER BY
    auto result = qs->order_by<^^CovPerson::department>()
                          .group_by<^^CovPerson::department, ^^CovPerson::age>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY + ORDER BY should succeed";
}

TEST_F(CoverageGapsTest, MultiFieldGroupByWithAllModifiers) {
    // Multi-field GROUP BY + ORDER BY + LIMIT + OFFSET
    auto result = qs->order_by<^^CovPerson::department>()
                          .limit(5)
                          .offset(1)
                          .group_by<^^CovPerson::department, ^^CovPerson::age>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY with all modifiers should succeed";
    EXPECT_LE(result.value().size(), 5);
}

// =============================================================================
// Edge cases for GROUP BY execution paths
// =============================================================================

TEST_F(CoverageGapsTest, GroupByEmptyResult) {
    // GROUP BY that returns no results (WHERE filters everything)
    auto result = qs->where(storm::orm::where::field<^^CovPerson::age>() > 1000)
                          .group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY with no matching rows should succeed";
    EXPECT_TRUE(result.value().empty()) << "Should return empty result";
}

TEST_F(CoverageGapsTest, GroupByRepeatedExecution) {
    // Execute same GROUP BY multiple times (tests caching)
    for (int i = 0; i < 5; ++i) {
        auto result = qs->order_by<^^CovPerson::department>().group_by<^^CovPerson::department>().count().select();

        ASSERT_TRUE(result.has_value()) << "Repeated GROUP BY should succeed on iteration " << i;
        EXPECT_EQ(result.value().size(), 4) << "Should have 4 departments";
    }
}

TEST_F(CoverageGapsTest, GroupByWithDifferentAggregatesSequentially) {
    // Test different aggregates with same GROUP BY pattern sequentially
    auto count_result = qs->group_by<^^CovPerson::department>().count().select();
    ASSERT_TRUE(count_result.has_value());

    auto sum_result = qs->group_by<^^CovPerson::department>().sum<^^CovPerson::salary>().select();
    ASSERT_TRUE(sum_result.has_value());

    auto avg_result = qs->group_by<^^CovPerson::department>().avg<^^CovPerson::age>().select();
    ASSERT_TRUE(avg_result.has_value());

    // All should have same number of groups
    EXPECT_EQ(count_result.value().size(), sum_result.value().size());
    EXPECT_EQ(count_result.value().size(), avg_result.value().size());
}

// =============================================================================
// Aggregate (non-GROUP BY) with modifiers - additional coverage
// =============================================================================

TEST_F(CoverageGapsTest, SimpleAggregateWithWhereAndJoin) {
    // Simple aggregate (no GROUP BY) with WHERE + JOIN
    auto result = msg_qs->where(storm::orm::where::field<^^CovMessage::value>() > 15)
                          .join<&CovMessage::sender>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Simple aggregate with WHERE + JOIN should succeed";
    EXPECT_GT(result.value(), 0);
}

TEST_F(CoverageGapsTest, MultipleAggregatesWithJoin) {
    // Multiple aggregates with JOIN
    auto result = msg_qs->join<&CovMessage::sender>().count().sum<^^CovMessage::value>().select();

    ASSERT_TRUE(result.has_value()) << "Multiple aggregates with JOIN should succeed";
    auto [count, sum] = result.value();
    EXPECT_GT(count, 0);
    EXPECT_GT(sum, 0);
}

// =============================================================================
// DISTINCT edge cases
// =============================================================================

TEST_F(CoverageGapsTest, DistinctWithOrderByAndLimit) {
    auto result = qs->order_by<^^CovPerson::department>().limit(2).distinct<^^CovPerson::department>().select();

    ASSERT_TRUE(result.has_value()) << "DISTINCT + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 2);
}

TEST_F(CoverageGapsTest, DistinctWithOrderByLimitOffset) {
    auto result =
            qs->order_by<^^CovPerson::department>().limit(2).offset(1).distinct<^^CovPerson::department>().select();

    ASSERT_TRUE(result.has_value()) << "DISTINCT + ORDER BY + LIMIT + OFFSET should succeed";
    EXPECT_LE(result.value().size(), 2);
}

TEST_F(CoverageGapsTest, DistinctMultiFieldWithModifiers) {
    auto result = qs->order_by<^^CovPerson::department>()
                          .limit(5)
                          .distinct<^^CovPerson::department, ^^CovPerson::age>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field DISTINCT with modifiers should succeed";
    EXPECT_LE(result.value().size(), 5);
}

// =============================================================================
// Large Batch Operations - testing chunked execution paths
// =============================================================================

class LargeBatchTest : public ::testing::Test {
  protected:
    struct BatchPerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       value{};
    };

    auto SetUp() -> void override {
        auto result = QuerySet<BatchPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<BatchPerson>::get_default_connection();

        auto create = conn->execute(
                "CREATE TABLE BatchPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "value INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create.has_value());

        qs = std::make_unique<QuerySet<BatchPerson>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<BatchPerson>::clear_default_connection();
    }

    auto insert_batch(size_t count) -> void {
        std::vector<BatchPerson> people;
        people.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            people.push_back({0, "Person" + std::to_string(i), static_cast<int>(i)});
        }
        auto result = qs->insert(std::span<const BatchPerson>(people));
        ASSERT_TRUE(result.has_value()) << "Failed to insert batch";
    }

    std::unique_ptr<QuerySet<BatchPerson>> qs;
};

TEST_F(LargeBatchTest, RemoveBulkSmall) {
    // Test bulk delete path (2-799 objects, uses IN clause)
    insert_batch(50);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 50);

    // Get all persons and remove them in bulk
    std::vector<BatchPerson> to_remove;
    to_remove.reserve(50);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Bulk remove should succeed";

    // Verify all removed
    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveBulkAtLimit) {
    // Test bulk delete at MAX_CHUNK_SIZE boundary (799 objects)
    insert_batch(799);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 799);

    std::vector<BatchPerson> to_remove;
    to_remove.reserve(799);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Bulk remove at limit should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveChunkedMinimal) {
    // Test chunked delete path (800 objects - just over MAX_CHUNK_SIZE)
    // This triggers: 1 full chunk of 799 + remainder of 1
    insert_batch(800);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 800);

    std::vector<BatchPerson> to_remove;
    to_remove.reserve(800);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Chunked remove should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveChunkedWithRemainder) {
    // Test chunked delete with significant remainder
    // 850 = 799 + 51 (tests remainder path)
    insert_batch(850);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 850);

    std::vector<BatchPerson> to_remove;
    to_remove.reserve(850);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Chunked remove with remainder should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveChunkedMultipleChunks) {
    // Test multiple full chunks (1600 = 2*799 + 2)
    insert_batch(1600);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1600);

    std::vector<BatchPerson> to_remove;
    to_remove.reserve(1600);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Multiple chunk remove should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveChunkedExactMultiple) {
    // Test exact multiple of chunk size (799*2 = 1598)
    // No remainder - tests the "no remainder" branch
    insert_batch(1598);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1598);

    std::vector<BatchPerson> to_remove;
    to_remove.reserve(1598);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = qs->remove(std::span<const BatchPerson>(to_remove));
    ASSERT_TRUE(remove_result.has_value()) << "Exact multiple chunk remove should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TEST_F(LargeBatchTest, RemoveEmpty) {
    // Test empty batch (early return path)
    std::vector<BatchPerson> empty;

    auto remove_result = qs->remove(std::span<const BatchPerson>(empty));
    ASSERT_TRUE(remove_result.has_value()) << "Empty remove should succeed";
}

TEST_F(LargeBatchTest, UpdateBulkSmall) {
    // Test bulk update path
    insert_batch(50);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());

    std::vector<BatchPerson> to_update;
    to_update.reserve(50);
    for (const auto& p : select_result.value()) {
        to_update.push_back({p.id, p.name, p.value + 1000});
    }

    auto update_result = qs->update(std::span<const BatchPerson>(to_update));
    ASSERT_TRUE(update_result.has_value()) << "Bulk update should succeed";

    // Verify updates
    auto verify_result = qs->select();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GE(p.value, 1000) << "Values should be updated";
    }
}

TEST_F(LargeBatchTest, UpdateChunkedWithRemainder) {
    // Test chunked update with remainder
    insert_batch(850);

    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 850);

    std::vector<BatchPerson> to_update;
    to_update.reserve(850);
    for (const auto& p : select_result.value()) {
        to_update.push_back({p.id, p.name, p.value + 2000});
    }

    auto update_result = qs->update(std::span<const BatchPerson>(to_update));
    ASSERT_TRUE(update_result.has_value()) << "Chunked update should succeed";

    // Verify updates
    auto verify_result = qs->select();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GE(p.value, 2000);
    }
}

TEST_F(LargeBatchTest, InsertLargeBatch) {
    // Test large batch insert
    std::vector<BatchPerson> people;
    people.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        people.push_back({0, "LargeBatch" + std::to_string(i), i});
    }

    auto insert_result = qs->insert(std::span<const BatchPerson>(people));
    ASSERT_TRUE(insert_result.has_value()) << "Large batch insert should succeed";

    auto count_result = qs->count().select();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 1000);
}

// =============================================================================
// Additional Optional Type Coverage
// =============================================================================

struct OptionalDouble {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::optional<double>                     value;
    std::string                               label;
};

struct OptionalInt64 {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::optional<int64_t>                    big_value;
    std::string                               label;
};

class OptionalTypesTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<OptionalDouble>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<OptionalDouble>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE OptionalDouble ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "value REAL, "
                "label TEXT NOT NULL)"
        );

        (void)conn->execute(
                "CREATE TABLE OptionalInt64 ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "big_value INTEGER, "
                "label TEXT NOT NULL)"
        );
    }

    auto TearDown() -> void override {
        QuerySet<OptionalDouble>::clear_default_connection();
    }
};

TEST_F(OptionalTypesTest, OptionalDoubleWithValue) {
    QuerySet<OptionalDouble> qs;
    OptionalDouble const     obj{.id = 0, .value = 3.14159, .label = "pi"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->value.has_value());
    EXPECT_NEAR(selected.value().begin()->value.value(), 3.14159, 0.0001);
}

TEST_F(OptionalTypesTest, OptionalDoubleNull) {
    QuerySet<OptionalDouble> qs;
    OptionalDouble const     obj{.id = 0, .value = std::nullopt, .label = "null_double"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->value.has_value());
}

TEST_F(OptionalTypesTest, OptionalDoubleBatch) {
    QuerySet<OptionalDouble>    qs;
    std::vector<OptionalDouble> batch = {
            {0, 1.1, "first"},
            {0, std::nullopt, "second"},
            {0, 2.2, "third"},
            {0, std::nullopt, "fourth"},
    };

    auto result = qs.insert(std::span<const OptionalDouble>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    auto it = selected.value().begin();
    EXPECT_TRUE(it->value.has_value());
    ++it;
    EXPECT_FALSE(it->value.has_value());
    ++it;
    EXPECT_TRUE(it->value.has_value());
    ++it;
    EXPECT_FALSE(it->value.has_value());
}

TEST_F(OptionalTypesTest, OptionalInt64WithValue) {
    QuerySet<OptionalInt64> qs;
    OptionalInt64 const     obj{.id = 0, .big_value = 9223372036854775807LL, .label = "max_int64"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->big_value.has_value());
    EXPECT_EQ(selected.value().begin()->big_value.value(), 9223372036854775807LL);
}

TEST_F(OptionalTypesTest, OptionalInt64Null) {
    QuerySet<OptionalInt64> qs;
    OptionalInt64 const     obj{.id = 0, .big_value = std::nullopt, .label = "null_int64"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->big_value.has_value());
}

TEST_F(OptionalTypesTest, UpdateOptionalDoubleToNull) {
    QuerySet<OptionalDouble> qs;
    OptionalDouble const     obj{.id = 0, .value = 100.5, .label = "to_null"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    OptionalDouble const updated{.id = static_cast<int>(id), .value = std::nullopt, .label = "now_null"};
    auto                 update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->value.has_value());
}

TEST_F(OptionalTypesTest, UpdateOptionalInt64FromNull) {
    QuerySet<OptionalInt64> qs;
    OptionalInt64 const     obj{.id = 0, .big_value = std::nullopt, .label = "from_null"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    OptionalInt64 const updated{.id = static_cast<int>(id), .big_value = 42LL, .label = "now_has_value"};
    auto                update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->big_value.has_value());
    EXPECT_EQ(selected.value().begin()->big_value.value(), 42LL);
}

// =============================================================================
// Complex WHERE Conditions (OR operator coverage)
// =============================================================================

class ComplexWhereTest : public ::testing::Test {
  protected:
    struct WherePerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       age{};
        std::string                               department;
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
                "department TEXT NOT NULL)"
        );

        qs = std::make_unique<QuerySet<WherePerson>>();

        // Insert test data
        std::vector<WherePerson> const people = {
                {0, "Alice", 25, "Engineering"},
                {0, "Bob", 30, "Sales"},
                {0, "Charlie", 35, "Engineering"},
                {0, "Diana", 40, "Marketing"},
                {0, "Eve", 28, "Sales"},
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

TEST_F(ComplexWhereTest, OrCondition) {
    // Test OR operator
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() < 26 or
                            storm::orm::where::field<^^WherePerson::age>() > 35)
                          .select();

    ASSERT_TRUE(result.has_value()) << "OR condition should work";
    EXPECT_EQ(result.value().size(), 2); // Alice (25) and Diana (40)
}

TEST_F(ComplexWhereTest, OrWithAnd) {
    // Combined OR and AND: (age < 26) OR (age > 35 AND department = "Marketing")
    auto young    = storm::orm::where::field<^^WherePerson::age>() < 26;
    auto old      = storm::orm::where::field<^^WherePerson::age>() > 35;
    auto mkt      = storm::orm::where::field<^^WherePerson::department>() == "Marketing";
    auto combined = young or (old and mkt);

    auto result = qs->where(combined).select();
    ASSERT_TRUE(result.has_value()) << "Complex OR/AND should work";
    EXPECT_GE(result.value().size(), 1);
}

TEST_F(ComplexWhereTest, MultipleOrConditions) {
    // Test multiple ORs: age == 25 OR age == 30 OR age == 35
    auto cond = storm::orm::where::field<^^WherePerson::age>() == 25 or
                storm::orm::where::field<^^WherePerson::age>() == 30 or
                storm::orm::where::field<^^WherePerson::age>() == 35;

    auto result = qs->where(cond).select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice (25), Bob (30), Charlie (35)
}

TEST_F(ComplexWhereTest, NestedAndOr) {
    // Nested: (dept = "Engineering" AND age < 30) OR (dept = "Sales" AND age > 29)
    auto eng_young = storm::orm::where::field<^^WherePerson::department>() == "Engineering" and
                     storm::orm::where::field<^^WherePerson::age>() < 30;
    auto sales_old = storm::orm::where::field<^^WherePerson::department>() == "Sales" and
                     storm::orm::where::field<^^WherePerson::age>() > 29;

    auto result = qs->where(eng_young or sales_old).select();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}

// =============================================================================
// SELECT with full chain (WHERE + ORDER BY + LIMIT + OFFSET)
// =============================================================================

TEST_F(ComplexWhereTest, FullChainSelect) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() > 25)
                          .order_by<^^WherePerson::age>()
                          .limit(2)
                          .offset(1)
                          .select();

    ASSERT_TRUE(result.has_value()) << "Full chain SELECT should work";
    EXPECT_LE(result.value().size(), 2);
}

TEST_F(ComplexWhereTest, FullChainCount) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() >= 30).count().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 3); // Bob (30), Charlie (35), Diana (40)
}

TEST_F(ComplexWhereTest, FullChainDistinct) {
    auto result = qs->where(storm::orm::where::field<^^WherePerson::age>() < 35)
                          .order_by<^^WherePerson::department>()
                          .distinct<^^WherePerson::department>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}

// =============================================================================
// Transaction Edge Cases
// =============================================================================

class TransactionTest : public ::testing::Test {
  protected:
    struct TxnPerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       value{};
    };

    auto SetUp() -> void override {
        auto result = QuerySet<TxnPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<TxnPerson>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE TxnPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "value INTEGER NOT NULL)"
        );

        qs = std::make_unique<QuerySet<TxnPerson>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<TxnPerson>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<TxnPerson>> qs;
};

TEST_F(TransactionTest, MultiRowUpdateInTransaction) {
    // Insert multiple rows
    std::vector<TxnPerson> const people = {{0, "P1", 1}, {0, "P2", 2}, {0, "P3", 3}};

    auto insert_result = qs->insert(std::span<const TxnPerson>(people));
    ASSERT_TRUE(insert_result.has_value());

    // Fetch and update all
    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());

    std::vector<TxnPerson> updates;
    for (const auto& p : select_result.value()) {
        updates.push_back({p.id, p.name, p.value + 100});
    }

    auto update_result = qs->update(std::span<const TxnPerson>(updates));
    ASSERT_TRUE(update_result.has_value());

    // Verify all updated
    auto verify_result = qs->select();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GT(p.value, 100);
    }
}

TEST_F(TransactionTest, EmptyBatchOperations) {
    std::vector<TxnPerson> empty;

    // Empty insert
    auto insert_result = qs->insert(std::span<const TxnPerson>(empty));
    ASSERT_TRUE(insert_result.has_value());

    // Empty update
    auto update_result = qs->update(std::span<const TxnPerson>(empty));
    ASSERT_TRUE(update_result.has_value());

    // Empty remove
    auto remove_result = qs->remove(std::span<const TxnPerson>(empty));
    ASSERT_TRUE(remove_result.has_value());
}

TEST_F(TransactionTest, SingleRowOperations) {
    // Single row insert
    TxnPerson const p1{0, "Single", 42};
    auto            insert_result = qs->insert(p1);
    ASSERT_TRUE(insert_result.has_value());

    int64_t const id = insert_result.value();

    // Single row update
    TxnPerson const updated{static_cast<int>(id), "Updated", 99};
    auto            update_result = qs->update(updated);
    ASSERT_TRUE(update_result.has_value());

    // Verify
    auto select_result = qs->select();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().begin()->value, 99);

    // Single row remove
    auto remove_result = qs->remove(updated);
    ASSERT_TRUE(remove_result.has_value());

    // Verify empty
    auto empty_result = qs->select();
    ASSERT_TRUE(empty_result.has_value());
    EXPECT_TRUE(empty_result.value().empty());
}

// =============================================================================
// Unsigned Integer Type Coverage
// =============================================================================

struct CovUnsignedTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    unsigned int                              uint_val{};
    unsigned long                             ulong_val{};
    unsigned short                            ushort_val{};
};

class CovUnsignedTypesTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<CovUnsignedTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<CovUnsignedTypes>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE CovUnsignedTypes ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "uint_val INTEGER NOT NULL, "
                "ulong_val INTEGER NOT NULL, "
                "ushort_val INTEGER NOT NULL)"
        );

        qs = std::make_unique<QuerySet<CovUnsignedTypes>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<CovUnsignedTypes>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<CovUnsignedTypes>> qs;
};

TEST_F(CovUnsignedTypesTest, InsertMaxValues) {
    CovUnsignedTypes const obj{
            .id         = 0,
            .uint_val   = std::numeric_limits<unsigned int>::max(),
            .ulong_val  = std::numeric_limits<unsigned long>::max() / 2, // Avoid overflow in SQLite
            .ushort_val = std::numeric_limits<unsigned short>::max()
    };

    auto result = qs->insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->ushort_val, std::numeric_limits<unsigned short>::max());
}

TEST_F(CovUnsignedTypesTest, InsertZeroValues) {
    CovUnsignedTypes const obj{.id = 0, .uint_val = 0, .ulong_val = 0, .ushort_val = 0};

    auto result = qs->insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->uint_val, 0u);
}

TEST_F(CovUnsignedTypesTest, BatchCovUnsignedTypes) {
    std::vector<CovUnsignedTypes> batch = {{0, 100, 1000, 10}, {0, 200, 2000, 20}, {0, 300, 3000, 30}};

    auto result = qs->insert(std::span<const CovUnsignedTypes>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TEST_F(CovUnsignedTypesTest, UpdateCovUnsignedTypes) {
    CovUnsignedTypes const obj{.id = 0, .uint_val = 100, .ulong_val = 1000, .ushort_val = 10};

    auto insert_result = qs->insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    CovUnsignedTypes const updated{.id = static_cast<int>(id), .uint_val = 999, .ulong_val = 9999, .ushort_val = 99};
    auto                   update_result = qs->update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->uint_val, 999u);
}

// =============================================================================
// Float Type Coverage
// =============================================================================

struct FloatType {
    [[= storm::meta::FieldAttr::primary]] int id{};
    float                                     value{};
    std::string                               label;
};

class FloatTypeTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<FloatType>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<FloatType>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE FloatType ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "value REAL NOT NULL, "
                "label TEXT NOT NULL)"
        );

        qs = std::make_unique<QuerySet<FloatType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<FloatType>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<FloatType>> qs;
};

TEST_F(FloatTypeTest, InsertFloatValue) {
    FloatType const obj{.id = 0, .value = 3.14159f, .label = "pi"};

    auto result = qs->insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->value, 3.14159f, 0.0001f);
}

TEST_F(FloatTypeTest, InsertNegativeFloat) {
    FloatType const obj{.id = 0, .value = -123.456f, .label = "negative"};

    auto result = qs->insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->value, -123.456f, 0.001f);
}

TEST_F(FloatTypeTest, BatchFloatValues) {
    std::vector<FloatType> batch = {
            {0, 1.1f, "first"},
            {0, 2.2f, "second"},
            {0, 3.3f, "third"},
    };

    auto result = qs->insert(std::span<const FloatType>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs->select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

// =============================================================================
// Query Reset and Reuse Coverage
// =============================================================================

class QueryResetTest : public ::testing::Test {
  protected:
    struct ResetPerson {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        int                                       score{};
    };

    auto SetUp() -> void override {
        auto result = QuerySet<ResetPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<ResetPerson>::get_default_connection();

        (void)conn->execute(
                "CREATE TABLE ResetPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "score INTEGER NOT NULL)"
        );

        qs = std::make_unique<QuerySet<ResetPerson>>();

        // Insert test data
        std::vector<ResetPerson> const people = {
                {0, "Alice", 100},
                {0, "Bob", 200},
                {0, "Charlie", 150},
                {0, "Diana", 180},
                {0, "Eve", 120},
        };

        for (const auto& person : people) {
            (void)qs->insert(person);
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<ResetPerson>::clear_default_connection();
    }

    std::unique_ptr<QuerySet<ResetPerson>> qs;
};

TEST_F(QueryResetTest, ResetClearsAllState) {
    // Apply various modifiers
    qs->where(storm::orm::where::field<^^ResetPerson::score>() > 100)
            .order_by<^^ResetPerson::name>()
            .limit(2)
            .offset(1);

    auto result1 = qs->select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_LE(result1.value().size(), 2);

    // Reset should clear all state
    qs->reset();

    auto result2 = qs->select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 5); // All 5 records
}

TEST_F(QueryResetTest, ReuseSameQuerySetMultipleTimes) {
    // First query
    auto result1 = qs->where(storm::orm::where::field<^^ResetPerson::score>() > 150).select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 2); // Bob (200), Diana (180)

    // Second query with same WHERE (should work due to caching)
    auto result2 = qs->select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 2);

    // Reset and new query
    qs->reset();
    auto result3 = qs->where(storm::orm::where::field<^^ResetPerson::score>() < 130).select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 2); // Alice (100), Eve (120)
}

TEST_F(QueryResetTest, ResetBetweenDifferentOperations) {
    // COUNT
    auto count1 = qs->count().select();
    ASSERT_TRUE(count1.has_value());
    EXPECT_EQ(count1.value(), 5);

    qs->reset();

    // SUM
    auto sum1 = qs->sum<^^ResetPerson::score>().select();
    ASSERT_TRUE(sum1.has_value());
    EXPECT_EQ(sum1.value(), 750); // 100+200+150+180+120

    qs->reset();

    // AVG
    auto avg1 = qs->avg<^^ResetPerson::score>().select();
    ASSERT_TRUE(avg1.has_value());
    EXPECT_NEAR(avg1.value(), 150.0, 0.1);

    qs->reset();

    // MIN/MAX
    auto min1 = qs->min<^^ResetPerson::score>().select();
    ASSERT_TRUE(min1.has_value());
    EXPECT_EQ(min1.value(), 100);

    qs->reset();

    auto max1 = qs->max<^^ResetPerson::score>().select();
    ASSERT_TRUE(max1.has_value());
    EXPECT_EQ(max1.value(), 200);
}

// =============================================================================
// Aggregate Edge Cases
// =============================================================================

TEST_F(QueryResetTest, AggregatesWithWhere) {
    // COUNT with WHERE
    auto count = qs->where(storm::orm::where::field<^^ResetPerson::score>() >= 150).count().select();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3); // Bob, Charlie, Diana

    qs->reset();

    // SUM with WHERE
    auto sum = qs->where(storm::orm::where::field<^^ResetPerson::score>() >= 150).sum<^^ResetPerson::score>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 530); // 200+150+180
}

TEST_F(QueryResetTest, AggregatesWithOrderByLimit) {
    // This tests aggregates with modifiers (ORDER BY doesn't affect aggregates, but LIMIT can)
    auto count = qs->order_by<^^ResetPerson::name>().count().select();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 5);
}

// NOLINTEND(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)
// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
