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
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTBEGIN(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)

import storm;

import <expected>;
import <string>;
import <vector>;
import <optional>;
import <tuple>;

#include "test_models.h"

using namespace storm;

// =============================================================================
// Test Fixture
// =============================================================================

template <typename ConnType> class CoverageGapsTest : public StormTestFixture<CovPerson, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        ASSERT_TRUE((storm::test::ensure_table<CovPerson, ConnType>(conn).has_value()));
        ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value()));
        ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()));

        qs     = std::make_unique<QuerySet<CovPerson, ConnType>>();
        msg_qs = std::make_unique<QuerySet<Message, ConnType>>();

        insert_test_data();
    }

    auto TearDown() -> void override {
        qs     = nullptr;
        msg_qs = nullptr;
        StormTestFixture<CovPerson, ConnType>::TearDown();
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
            auto r = qs->insert(person).execute();
            ASSERT_TRUE(r.has_value());
        }

        // Insert users for JOIN tests (CovUser::score maps to Person::score as optional<int>)
        QuerySet<Person, ConnType> user_qs;
        std::ignore = user_qs.insert(Person{.name = "User1", .score = std::optional<int>(100)}).execute();
        std::ignore = user_qs.insert(Person{.name = "User2", .score = std::optional<int>(200)}).execute();
        std::ignore = user_qs.insert(Person{.name = "User3", .score = std::optional<int>(150)}).execute();

        // Insert messages
        QuerySet<Message, ConnType> cov_msg_qs;
        std::ignore = cov_msg_qs.insert(Message{.content = "Msg1", .value = 10, .sender = {.id = 1}}).execute();
        std::ignore = cov_msg_qs.insert(Message{.content = "Msg2", .value = 20, .sender = {.id = 1}}).execute();
        std::ignore = cov_msg_qs.insert(Message{.content = "Msg3", .value = 30, .sender = {.id = 2}}).execute();
        std::ignore = cov_msg_qs.insert(Message{.content = "Msg4", .value = 40, .sender = {.id = 2}}).execute();
        std::ignore = cov_msg_qs.insert(Message{.content = "Msg5", .value = 50, .sender = {.id = 3}}).execute();
    }

    std::unique_ptr<QuerySet<CovPerson, ConnType>> qs;
    std::unique_ptr<QuerySet<Message, ConnType>>   msg_qs;
};

TYPED_TEST_SUITE(CoverageGapsTest, DatabaseTypes);

// =============================================================================
// GROUP BY + ORDER BY Tests (execute_simple with modifiers path)
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupByWithOrderByAscending) {
    // GROUP BY + ORDER BY - tests has_modifiers=true path in execute_simple()
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

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

TYPED_TEST(CoverageGapsTest, GroupByWithOrderByDescending) {
    // GROUP BY + ORDER BY DESC (false = descending)
    auto result = this->qs->template order_by<^^CovPerson::department, false>()
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

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

TYPED_TEST(CoverageGapsTest, GroupByWithOrderByAndLimit) {
    // GROUP BY + ORDER BY + LIMIT - tests full modifiers path
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(2)
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 2) << "Should return exactly 2 groups";
}

TYPED_TEST(CoverageGapsTest, GroupByWithOrderByLimitOffset) {
    // GROUP BY + ORDER BY + LIMIT + OFFSET - complete modifiers test
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(2)
                          .offset(1)
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY + LIMIT + OFFSET should succeed";
    EXPECT_EQ(result.value().size(), 2) << "Should return 2 groups after offset";
}

TYPED_TEST(CoverageGapsTest, GroupByWithOffsetOnly) {
    // GROUP BY + OFFSET (uses LIMIT -1 internally)
    auto result = this->qs->offset(1).template group_by<^^CovPerson::department>().count().select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + OFFSET should succeed";
    // We have 4 departments, offset 1 should give us 3
    EXPECT_EQ(result.value().size(), 3);
}

// =============================================================================
// GROUP BY + SUM + ORDER BY (different aggregate types with modifiers)
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupBySumWithOrderBy) {
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .template group_by<^^CovPerson::department>()
                          .template sum<^^CovPerson::salary>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());
}

TYPED_TEST(CoverageGapsTest, GroupByAvgWithOrderByAndLimit) {
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(3)
                          .template group_by<^^CovPerson::department>()
                          .template avg<^^CovPerson::salary>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + AVG + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 3);
}

TYPED_TEST(CoverageGapsTest, GroupByMinMaxWithModifiers) {
    // Test MIN aggregate with full modifiers
    auto min_result = this->qs->template order_by<^^CovPerson::department>()
                              .limit(2)
                              .offset(1)
                              .template group_by<^^CovPerson::department>()
                              .template min<^^CovPerson::age>()
                              .select();

    ASSERT_TRUE(min_result.has_value()) << "GROUP BY + MIN with modifiers should succeed";
    EXPECT_EQ(min_result.value().size(), 2);

    // Test MAX aggregate with full modifiers
    auto max_result = this->qs->template order_by<^^CovPerson::department>()
                              .limit(2)
                              .offset(1)
                              .template group_by<^^CovPerson::department>()
                              .template max<^^CovPerson::age>()
                              .select();

    ASSERT_TRUE(max_result.has_value()) << "GROUP BY + MAX with modifiers should succeed";
    EXPECT_EQ(max_result.value().size(), 2);
}

// =============================================================================
// GROUP BY + WHERE + ORDER BY (execute_where_impl with modifiers)
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupByWhereWithOrderBy) {
    // WHERE + GROUP BY + ORDER BY
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() > 30)
                          .template order_by<^^CovPerson::department>()
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + GROUP BY + ORDER BY should succeed";

    // All results should have age > 30
    for (const auto& [dept, count] : result.value()) {
        EXPECT_GT(count, 0) << "Each group should have at least one person";
    }
}

TYPED_TEST(CoverageGapsTest, GroupByWhereWithOrderByAndLimit) {
    // WHERE + GROUP BY + ORDER BY + LIMIT
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::salary>() > 70000.0)
                          .template order_by<^^CovPerson::department>()
                          .limit(2)
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_LE(result.value().size(), 2);
}

// =============================================================================
// GROUP BY + JOIN + ORDER BY (execute_join_impl with modifiers)
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupByJoinWithOrderBy) {
    // JOIN + GROUP BY + ORDER BY (group by value, an int field)
    auto result = this->msg_qs->template join<&Message::sender>()
                          .template order_by<^^Message::value>()
                          .template group_by<^^Message::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());
}

TYPED_TEST(CoverageGapsTest, GroupByJoinWithOrderByAndLimit) {
    // JOIN + GROUP BY + ORDER BY + LIMIT
    auto result = this->msg_qs->template join<&Message::sender>()
                          .template order_by<^^Message::value>()
                          .limit(3)
                          .template group_by<^^Message::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + ORDER BY + LIMIT should succeed";
    EXPECT_LE(result.value().size(), 3);
}

TYPED_TEST(CoverageGapsTest, GroupByJoinSumWithModifiers) {
    // JOIN + GROUP BY + SUM + ORDER BY + LIMIT + OFFSET
    auto result = this->msg_qs->template join<&Message::sender>()
                          .template order_by<^^Message::content>()
                          .limit(3)
                          .offset(0)
                          .template group_by<^^Message::content>()
                          .template sum<^^Message::value>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + SUM with modifiers should succeed";
}

// =============================================================================
// GROUP BY + WHERE + JOIN + ORDER BY (execute_where_join_impl with modifiers)
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupByWhereJoinWithOrderBy) {
    // WHERE + JOIN + GROUP BY + ORDER BY (full chain with modifiers)
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 10)
                          .template join<&Message::sender>()
                          .template order_by<^^Message::value>()
                          .template group_by<^^Message::value>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + GROUP BY + ORDER BY should succeed";
}

TYPED_TEST(CoverageGapsTest, GroupByWhereJoinWithAllModifiers) {
    // WHERE + JOIN + GROUP BY + ORDER BY + LIMIT + OFFSET
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 20)
                          .template join<&Message::sender>()
                          .template order_by<^^Message::content>()
                          .limit(3)
                          .offset(0)
                          .template group_by<^^Message::content>()
                          .template sum<^^Message::value>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Full chain with all modifiers should succeed";
}

// =============================================================================
// Multi-field GROUP BY with modifiers
// =============================================================================

TYPED_TEST(CoverageGapsTest, MultiFieldGroupByWithOrderBy) {
    // Multi-field GROUP BY + ORDER BY
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .template group_by<^^CovPerson::department, ^^CovPerson::age>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY + ORDER BY should succeed";
}

TYPED_TEST(CoverageGapsTest, MultiFieldGroupByWithAllModifiers) {
    // Multi-field GROUP BY + ORDER BY + LIMIT + OFFSET
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(5)
                          .offset(1)
                          .template group_by<^^CovPerson::department, ^^CovPerson::age>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY with all modifiers should succeed";
    EXPECT_LE(result.value().size(), 5);
}

// =============================================================================
// Edge cases for GROUP BY execution paths
// =============================================================================

TYPED_TEST(CoverageGapsTest, GroupByEmptyResult) {
    // GROUP BY that returns no results (WHERE filters everything)
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() > 1000)
                          .template group_by<^^CovPerson::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY with no matching rows should succeed";
    EXPECT_TRUE(result.value().empty()) << "Should return empty result";
}

TYPED_TEST(CoverageGapsTest, GroupByRepeatedExecution) {
    // Execute same GROUP BY multiple times (tests caching)
    for (int i = 0; i < 5; ++i) {
        auto result = this->qs->template order_by<^^CovPerson::department>()
                              .template group_by<^^CovPerson::department>()
                              .count()
                              .select();

        ASSERT_TRUE(result.has_value()) << "Repeated GROUP BY should succeed on iteration " << i;
        EXPECT_EQ(result.value().size(), 4) << "Should have 4 departments";
    }
}

TYPED_TEST(CoverageGapsTest, GroupByWithDifferentAggregatesSequentially) {
    // Test different aggregates with same GROUP BY pattern sequentially
    auto count_result = this->qs->template group_by<^^CovPerson::department>().count().select();
    ASSERT_TRUE(count_result.has_value());

    auto sum_result =
            this->qs->template group_by<^^CovPerson::department>().template sum<^^CovPerson::salary>().select();
    ASSERT_TRUE(sum_result.has_value());

    auto avg_result = this->qs->template group_by<^^CovPerson::department>().template avg<^^CovPerson::age>().select();
    ASSERT_TRUE(avg_result.has_value());

    // All should have same number of groups
    EXPECT_EQ(count_result.value().size(), sum_result.value().size());
    EXPECT_EQ(count_result.value().size(), avg_result.value().size());
}

// =============================================================================
// Aggregate (non-GROUP BY) with modifiers - additional coverage
// =============================================================================

TYPED_TEST(CoverageGapsTest, SimpleAggregateWithWhereAndJoin) {
    // Simple aggregate (no GROUP BY) with WHERE + JOIN
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 15)
                          .template join<&Message::sender>()
                          .count()
                          .get();

    ASSERT_TRUE(result.has_value()) << "Simple aggregate with WHERE + JOIN should succeed";
    EXPECT_GT(result.value(), 0);
}

TYPED_TEST(CoverageGapsTest, MultipleAggregatesWithJoin) {
    // Multiple aggregates with JOIN
    auto result = this->msg_qs->template join<&Message::sender>().count().template sum<^^Message::value>().get();

    ASSERT_TRUE(result.has_value()) << "Multiple aggregates with JOIN should succeed";
    auto [count, sum] = result.value();
    EXPECT_GT(count, 0);
    EXPECT_GT(sum, 0);
}

// =============================================================================
// DISTINCT edge cases
// =============================================================================

TYPED_TEST(CoverageGapsTest, DistinctWithOrderByAndLimit) {
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(2)
                          .template distinct<^^CovPerson::department>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "DISTINCT + ORDER BY + LIMIT should succeed";
    EXPECT_EQ(result.value().size(), 2);
}

TYPED_TEST(CoverageGapsTest, DistinctWithOrderByLimitOffset) {
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(2)
                          .offset(1)
                          .template distinct<^^CovPerson::department>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "DISTINCT + ORDER BY + LIMIT + OFFSET should succeed";
    EXPECT_LE(result.value().size(), 2);
}

TYPED_TEST(CoverageGapsTest, DistinctMultiFieldWithModifiers) {
    auto result = this->qs->template order_by<^^CovPerson::department>()
                          .limit(5)
                          .template distinct<^^CovPerson::department, ^^CovPerson::age>()
                          .select();

    ASSERT_TRUE(result.has_value()) << "Multi-field DISTINCT with modifiers should succeed";
    EXPECT_LE(result.value().size(), 5);
}

// =============================================================================
// Large Batch Operations - testing chunked execution paths
// =============================================================================

template <typename ConnType> class LargeBatchTest : public StormTestFixture<SimpleRecord, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<SimpleRecord, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<SimpleRecord, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<SimpleRecord, ConnType>::TearDown();
    }

    auto insert_batch(size_t count) -> void {
        std::vector<SimpleRecord> people;
        people.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            people.push_back({0, "Person" + std::to_string(i), static_cast<int>(i)});
        }
        auto result = qs->insert(std::span<const SimpleRecord>(people)).execute();
        ASSERT_TRUE(result.has_value()) << "Failed to insert batch";
    }

    std::unique_ptr<QuerySet<SimpleRecord, ConnType>> qs;
};

TYPED_TEST_SUITE(LargeBatchTest, DatabaseTypes);

TYPED_TEST(LargeBatchTest, RemoveBulkSmall) {
    // Test bulk delete path (2-799 objects, uses IN clause)
    this->insert_batch(50);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 50);

    // Get all persons and remove them in bulk

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(50);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Bulk remove should succeed";

    // Verify all removed
    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveBulkAtLimit) {
    // Test bulk delete at MAX_CHUNK_SIZE boundary (799 objects)
    this->insert_batch(799);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 799);

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(799);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Bulk remove at limit should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveChunkedMinimal) {
    // Test chunked delete path (800 objects - just over MAX_CHUNK_SIZE)
    // This triggers: 1 full chunk of 799 + remainder of 1
    this->insert_batch(800);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 800);

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(800);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Chunked remove should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveChunkedWithRemainder) {
    // Test chunked delete with significant remainder
    // 850 = 799 + 51 (tests remainder path)
    this->insert_batch(850);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 850);

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(850);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Chunked remove with remainder should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveChunkedMultipleChunks) {
    // Test multiple full chunks (1600 = 2*799 + 2)
    this->insert_batch(1600);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1600);

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(1600);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Multiple chunk remove should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveChunkedExactMultiple) {
    // Test exact multiple of chunk size (799*2 = 1598)
    // No remainder - tests the "no remainder" branch
    this->insert_batch(1598);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1598);

    std::vector<SimpleRecord> to_remove;
    to_remove.reserve(1598);
    for (const auto& p : select_result.value()) {
        to_remove.push_back(p);
    }

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(to_remove)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Exact multiple chunk remove should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 0);
}

TYPED_TEST(LargeBatchTest, RemoveEmpty) {
    // Test empty batch (early return path)

    std::vector<SimpleRecord> empty;

    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(empty)).execute();
    ASSERT_TRUE(remove_result.has_value()) << "Empty remove should succeed";
}

TYPED_TEST(LargeBatchTest, UpdateBulkSmall) {
    // Test bulk update path
    this->insert_batch(50);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());

    std::vector<SimpleRecord> to_update;
    to_update.reserve(50);
    for (const auto& p : select_result.value()) {
        to_update.push_back({p.id, p.name, p.value + 1000});
    }

    auto update_result = this->qs->update(std::span<const SimpleRecord>(to_update)).execute();
    ASSERT_TRUE(update_result.has_value()) << "Bulk update should succeed";

    // Verify updates
    auto verify_result = this->qs->select().execute();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GE(p.value, 1000) << "Values should be updated";
    }
}

TYPED_TEST(LargeBatchTest, UpdateChunkedWithRemainder) {
    // Test chunked update with remainder
    this->insert_batch(850);

    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 850);

    std::vector<SimpleRecord> to_update;
    to_update.reserve(850);
    for (const auto& p : select_result.value()) {
        to_update.push_back({p.id, p.name, p.value + 2000});
    }

    auto update_result = this->qs->update(std::span<const SimpleRecord>(to_update)).execute();
    ASSERT_TRUE(update_result.has_value()) << "Chunked update should succeed";

    // Verify updates
    auto verify_result = this->qs->select().execute();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GE(p.value, 2000);
    }
}

TYPED_TEST(LargeBatchTest, InsertLargeBatch) {
    // Test large batch insert

    std::vector<SimpleRecord> people;
    people.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        people.push_back({0, "LargeBatch" + std::to_string(i), i});
    }

    auto insert_result = this->qs->insert(std::span<const SimpleRecord>(people)).execute();
    ASSERT_TRUE(insert_result.has_value()) << "Large batch insert should succeed";

    auto count_result = this->qs->count().get();
    ASSERT_TRUE(count_result.has_value());
    EXPECT_EQ(count_result.value(), 1000);
}

// =============================================================================
// Additional Optional Type Coverage
// =============================================================================

template <typename ConnType> class OptionalTypesTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(OptionalTypesTest, DatabaseTypes);

TYPED_TEST(OptionalTypesTest, OptionalDoubleWithValue) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = 3.14159, .label = "pi"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_double.has_value());
    EXPECT_NEAR(selected.value().begin()->opt_double.value(), 3.14159, 0.0001);
}

TYPED_TEST(OptionalTypesTest, OptionalDoubleNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = std::nullopt, .label = "null_double"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, OptionalDoubleBatch) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .opt_double = 1.1, .label = "first"},
            {.id = 0, .opt_double = std::nullopt, .label = "second"},
            {.id = 0, .opt_double = 2.2, .label = "third"},
            {.id = 0, .opt_double = std::nullopt, .label = "fourth"},
    };

    auto result = qs.insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    auto it = selected.value().begin();
    EXPECT_TRUE(it->opt_double.has_value());
    ++it;
    EXPECT_FALSE(it->opt_double.has_value());
    ++it;
    EXPECT_TRUE(it->opt_double.has_value());
    ++it;
    EXPECT_FALSE(it->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, OptionalInt64WithValue) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = 9223372036854775807LL, .label = "max_int64"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_int64.has_value());
    EXPECT_EQ(selected.value().begin()->opt_int64.value(), 9223372036854775807LL);
}

TYPED_TEST(OptionalTypesTest, OptionalInt64Null) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = std::nullopt, .label = "null_int64"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_int64.has_value());
}

TYPED_TEST(OptionalTypesTest, UpdateOptionalDoubleToNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = 100.5, .label = "to_null"};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .opt_double = std::nullopt, .label = "now_null"};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, UpdateOptionalInt64FromNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = std::nullopt, .label = "from_null"};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .opt_int64 = 42LL, .label = "now_has_value"};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_int64.has_value());
    EXPECT_EQ(selected.value().begin()->opt_int64.value(), 42LL);
}

// =============================================================================
// Complex WHERE Conditions (OR operator coverage)
// =============================================================================

template <typename ConnType> class ComplexWhereTest : public StormTestFixture<CovPerson, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<CovPerson, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;

        qs = std::make_unique<QuerySet<CovPerson, ConnType>>();

        // Insert test data
        std::vector<CovPerson> const people = {
                {.id = 0, .name = "Alice", .age = 25, .department = "Engineering"},
                {.id = 0, .name = "Bob", .age = 30, .department = "Sales"},
                {.id = 0, .name = "Charlie", .age = 35, .department = "Engineering"},
                {.id = 0, .name = "Diana", .age = 40, .department = "Marketing"},
                {.id = 0, .name = "Eve", .age = 28, .department = "Sales"},
        };

        for (const auto& person : people) {
            (void)qs->insert(person).execute();
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<CovPerson, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<CovPerson, ConnType>> qs;
};

TYPED_TEST_SUITE(ComplexWhereTest, DatabaseTypes);

TYPED_TEST(ComplexWhereTest, OrCondition) {
    // Test OR operator
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() < 26 or
                                  storm::orm::where::field<^^CovPerson::age>() > 35)
                          .select()
                          .execute();

    ASSERT_TRUE(result.has_value()) << "OR condition should work";
    EXPECT_EQ(result.value().size(), 2); // Alice (25) and Diana (40)
}

TYPED_TEST(ComplexWhereTest, OrWithAnd) {
    // Combined OR and AND: (age < 26) OR (age > 35 AND department = "Marketing")
    auto young    = storm::orm::where::field<^^CovPerson::age>() < 26;
    auto old      = storm::orm::where::field<^^CovPerson::age>() > 35;
    auto mkt      = storm::orm::where::field<^^CovPerson::department>() == "Marketing";
    auto combined = young or (old and mkt);

    auto result = this->qs->where(combined).select().execute();
    ASSERT_TRUE(result.has_value()) << "Complex OR/AND should work";
    EXPECT_GE(result.value().size(), 1);
}

TYPED_TEST(ComplexWhereTest, MultipleOrConditions) {
    // Test multiple ORs: age == 25 OR age == 30 OR age == 35
    auto cond = storm::orm::where::field<^^CovPerson::age>() == 25 or
                storm::orm::where::field<^^CovPerson::age>() == 30 or
                storm::orm::where::field<^^CovPerson::age>() == 35;

    auto result = this->qs->where(cond).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice (25), Bob (30), Charlie (35)
}

TYPED_TEST(ComplexWhereTest, NestedAndOr) {
    // Nested: (dept = "Engineering" AND age < 30) OR (dept = "Sales" AND age > 29)
    auto eng_young = storm::orm::where::field<^^CovPerson::department>() == "Engineering" and
                     storm::orm::where::field<^^CovPerson::age>() < 30;
    auto sales_old = storm::orm::where::field<^^CovPerson::department>() == "Sales" and
                     storm::orm::where::field<^^CovPerson::age>() > 29;

    auto result = this->qs->where(eng_young or sales_old).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}

// =============================================================================
// SELECT with full chain (WHERE + ORDER BY + LIMIT + OFFSET)
// =============================================================================

TYPED_TEST(ComplexWhereTest, FullChainSelect) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() > 25)
                          .template order_by<^^CovPerson::age>()
                          .limit(2)
                          .offset(1)
                          .select()
                          .execute();

    ASSERT_TRUE(result.has_value()) << "Full chain SELECT should work";
    EXPECT_LE(result.value().size(), 2);
}

TYPED_TEST(ComplexWhereTest, FullChainCount) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() >= 30).count().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 3); // Bob (30), Charlie (35), Diana (40)
}

TYPED_TEST(ComplexWhereTest, FullChainDistinct) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() < 35)
                          .template order_by<^^CovPerson::department>()
                          .template distinct<^^CovPerson::department>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}

// =============================================================================
// Transaction Edge Cases
// =============================================================================

template <typename ConnType> class TransactionTest : public StormTestFixture<SimpleRecord, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<SimpleRecord, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<SimpleRecord, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<SimpleRecord, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<SimpleRecord, ConnType>> qs;
};

TYPED_TEST_SUITE(TransactionTest, DatabaseTypes);

TYPED_TEST(TransactionTest, MultiRowUpdateInTransaction) {
    // Insert multiple rows
    std::vector<SimpleRecord> const people = {{0, "P1", 1}, {0, "P2", 2}, {0, "P3", 3}};

    auto insert_result = this->qs->insert(std::span<const SimpleRecord>(people)).execute();
    ASSERT_TRUE(insert_result.has_value());

    // Fetch and update all
    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());

    std::vector<SimpleRecord> updates;
    for (const auto& p : select_result.value()) {
        updates.push_back({p.id, p.name, p.value + 100});
    }

    auto update_result = this->qs->update(std::span<const SimpleRecord>(updates)).execute();
    ASSERT_TRUE(update_result.has_value());

    // Verify all updated
    auto verify_result = this->qs->select().execute();
    ASSERT_TRUE(verify_result.has_value());
    for (const auto& p : verify_result.value()) {
        EXPECT_GT(p.value, 100);
    }
}

TYPED_TEST(TransactionTest, EmptyBatchOperations) {
    std::vector<SimpleRecord> empty;

    // Empty insert
    auto insert_result = this->qs->insert(std::span<const SimpleRecord>(empty)).execute();
    ASSERT_TRUE(insert_result.has_value());

    // Empty update
    auto update_result = this->qs->update(std::span<const SimpleRecord>(empty)).execute();
    ASSERT_TRUE(update_result.has_value());

    // Empty remove
    auto remove_result = this->qs->remove(std::span<const SimpleRecord>(empty)).execute();
    ASSERT_TRUE(remove_result.has_value());
}

TYPED_TEST(TransactionTest, SingleRowOperations) {
    // Single row insert
    SimpleRecord const p1{0, "Single", 42};
    auto               insert_result = this->qs->insert(p1).execute();
    ASSERT_TRUE(insert_result.has_value());

    int64_t const id = insert_result.value();

    // Single row update
    SimpleRecord const updated{static_cast<int>(id), "Updated", 99};
    auto               update_result = this->qs->update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    // Verify
    auto select_result = this->qs->select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().begin()->value, 99);

    // Single row remove
    auto remove_result = this->qs->remove(updated).execute();
    ASSERT_TRUE(remove_result.has_value());

    // Verify empty
    auto empty_result = this->qs->select().execute();
    ASSERT_TRUE(empty_result.has_value());
    EXPECT_TRUE(empty_result.value().empty());
}

// =============================================================================
// Unsigned Integer Type Coverage
// =============================================================================

template <typename ConnType> class UnsignedTypesTest : public StormTestFixture<ExtendedTypes, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<ExtendedTypes, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<ExtendedTypes, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<ExtendedTypes, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<ExtendedTypes, ConnType>> qs;
};

TYPED_TEST_SUITE(UnsignedTypesTest, DatabaseTypes);

TYPED_TEST(UnsignedTypesTest, InsertMaxValues) {
    ExtendedTypes const obj{
            .id    = 0,
            .u_int = std::numeric_limits<unsigned int>::max(),
    };

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, std::numeric_limits<unsigned int>::max());
}

TYPED_TEST(UnsignedTypesTest, InsertZeroValues) {
    ExtendedTypes const obj{.id = 0, .u_int = 0};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 0u);
}

TYPED_TEST(UnsignedTypesTest, BatchUnsignedTypes) {
    std::vector<ExtendedTypes> batch = {
            {.id = 0, .u_int = 100},
            {.id = 0, .u_int = 200},
            {.id = 0, .u_int = 300},
    };

    auto result = this->qs->insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(UnsignedTypesTest, UpdateUnsignedTypes) {
    ExtendedTypes const obj{.id = 0, .u_int = 100};

    auto insert_result = this->qs->insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .u_int = 999};
    auto                update_result = this->qs->update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 999u);
}

// =============================================================================
// Float Type Coverage
// =============================================================================

template <typename ConnType> class FloatTypeTest : public StormTestFixture<ExtendedTypes, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<ExtendedTypes, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<ExtendedTypes, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<ExtendedTypes, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<ExtendedTypes, ConnType>> qs;
};

TYPED_TEST_SUITE(FloatTypeTest, DatabaseTypes);

TYPED_TEST(FloatTypeTest, InsertFloatValue) {
    ExtendedTypes const obj{.id = 0, .approx = 3.14159f, .label = "pi"};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->approx, 3.14159f, 0.0001f);
}

TYPED_TEST(FloatTypeTest, InsertNegativeFloat) {
    ExtendedTypes const obj{.id = 0, .approx = -123.456f, .label = "negative"};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->approx, -123.456f, 0.001f);
}

TYPED_TEST(FloatTypeTest, BatchFloatValues) {
    std::vector<ExtendedTypes> batch = {
            {.id = 0, .approx = 1.1f, .label = "first"},
            {.id = 0, .approx = 2.2f, .label = "second"},
            {.id = 0, .approx = 3.3f, .label = "third"},
    };

    auto result = this->qs->insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

// =============================================================================
// Query Reset and Reuse Coverage
// =============================================================================

template <typename ConnType> class QueryResetTest : public StormTestFixture<SimpleRecord, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<SimpleRecord, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;

        qs = std::make_unique<QuerySet<SimpleRecord, ConnType>>();

        // Insert test data
        std::vector<SimpleRecord> const people = {
                {0, "Alice", 100},
                {0, "Bob", 200},
                {0, "Charlie", 150},
                {0, "Diana", 180},
                {0, "Eve", 120},
        };

        for (const auto& person : people) {
            (void)qs->insert(person).execute();
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<SimpleRecord, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<SimpleRecord, ConnType>> qs;
};

TYPED_TEST_SUITE(QueryResetTest, DatabaseTypes);

TYPED_TEST(QueryResetTest, ResetClearsAllState) {
    // Apply various modifiers
    this->qs->where(storm::orm::where::field<^^SimpleRecord::value>() > 100)
            .template order_by<^^SimpleRecord::name>()
            .limit(2)
            .offset(1);

    auto result1 = this->qs->select().execute();
    ASSERT_TRUE(result1.has_value());
    EXPECT_LE(result1.value().size(), 2);

    // Reset should clear all state
    this->qs->reset();

    auto result2 = this->qs->select().execute();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 5); // All 5 records
}

TYPED_TEST(QueryResetTest, ReuseSameQuerySetMultipleTimes) {
    // First query
    auto result1 = this->qs->where(storm::orm::where::field<^^SimpleRecord::value>() > 150).select().execute();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 2); // Bob (200), Diana (180)

    // Second query with same WHERE (should work due to caching)
    auto result2 = this->qs->select().execute();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 2);

    // Reset and new query
    this->qs->reset();
    auto result3 = this->qs->where(storm::orm::where::field<^^SimpleRecord::value>() < 130).select().execute();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 2); // Alice (100), Eve (120)
}

TYPED_TEST(QueryResetTest, ResetBetweenDifferentOperations) {
    // COUNT
    auto count1 = this->qs->count().get();
    ASSERT_TRUE(count1.has_value());
    EXPECT_EQ(count1.value(), 5);

    this->qs->reset();

    // SUM
    auto sum1 = this->qs->template sum<^^SimpleRecord::value>().get();
    ASSERT_TRUE(sum1.has_value());
    EXPECT_EQ(sum1.value(), 750); // 100+200+150+180+120

    this->qs->reset();

    // AVG
    auto avg1 = this->qs->template avg<^^SimpleRecord::value>().get();
    ASSERT_TRUE(avg1.has_value());
    EXPECT_NEAR(avg1.value(), 150.0, 0.1);

    this->qs->reset();

    // MIN/MAX
    auto min1 = this->qs->template min<^^SimpleRecord::value>().get();
    ASSERT_TRUE(min1.has_value());
    EXPECT_EQ(min1.value(), 100);

    this->qs->reset();

    auto max1 = this->qs->template max<^^SimpleRecord::value>().get();
    ASSERT_TRUE(max1.has_value());
    EXPECT_EQ(max1.value(), 200);
}

// =============================================================================
// Aggregate Edge Cases
// =============================================================================

TYPED_TEST(QueryResetTest, AggregatesWithWhere) {
    // COUNT with WHERE
    auto count = this->qs->where(storm::orm::where::field<^^SimpleRecord::value>() >= 150).count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3); // Bob, Charlie, Diana

    this->qs->reset();

    // SUM with WHERE
    auto sum = this->qs->where(storm::orm::where::field<^^SimpleRecord::value>() >= 150)
                       .template sum<^^SimpleRecord::value>()
                       .get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 530); // 200+150+180
}

TYPED_TEST(QueryResetTest, AggregatesWithOrderByLimit) {
    // This tests aggregates with modifiers (ORDER BY doesn't affect aggregates, but LIMIT can)
    auto count = this->qs->template order_by<^^SimpleRecord::name>().count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 5);
}

// NOLINTEND(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)
// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
