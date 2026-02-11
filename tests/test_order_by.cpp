// test_order_by.cpp - Comprehensive tests for ORDER BY functionality
#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;
import <cstdint>;

using namespace storm;
using namespace storm::orm::where;

// Test model
struct OrderByPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
    bool                                      is_active{};
};

// Test model for nullable field ordering
struct OrderByNullable {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::optional<int>                        score;
    std::string                               name;
};

// Test model for BLOB field ordering
struct OrderByBlob {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::vector<uint8_t>                      data;
    std::string                               label;
};

template <typename ConnType> class OrderByTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<OrderByPerson, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<OrderByPerson, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE OrderByPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "is_active INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"OrderByPerson"});

        // Insert test data with varying values
        std::vector<OrderByPerson> const test_data = {
                {1, "Alice", 30, true},
                {2, "Bob", 25, true},
                {3, "Charlie", 35, false},
                {4, "David", 25, true},
                {5, "Eve", 30, false},
                {6, "Frank", 40, true},
                {7, "Grace", 25, false},
                {8, "Henry", 35, true},
                {9, "Ivy", 30, true},
                {10, "Jack", 40, false},
        };

        QuerySet<OrderByPerson, ConnType> qs;
        for (const auto& person : test_data) {
            auto insert_result = qs.insert(person);
            ASSERT_TRUE(insert_result.has_value()) << "Failed to insert person: " << person.name;
        }
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<OrderByPerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<OrderByPerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<OrderByPerson, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(OrderByTest, DatabaseTypes);

// ============================================================================
// Basic ORDER BY Tests
// ============================================================================

TYPED_TEST(OrderByTest, SingleFieldDefaultAsc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age (default ASC)
    auto result = qs.template order_by<^^OrderByPerson::age>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify ascending order by age
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 25); // Bob, David, or Grace
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->age, 30); // Alice, Eve, or Ivy
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 6)->age, 35); // Charlie or Henry
    EXPECT_EQ(std::ranges::next(people.begin(), 7)->age, 35);
    EXPECT_EQ(std::ranges::next(people.begin(), 8)->age, 40); // Frank or Jack
    EXPECT_EQ(std::ranges::next(people.begin(), 9)->age, 40);
}

TYPED_TEST(OrderByTest, SingleFieldExplicitAsc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age (explicit ASC)
    auto result = qs.template order_by<^^OrderByPerson::age, true>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify ascending order
    auto prev_it = people.begin();
    auto it      = std::ranges::next(prev_it);
    for (; it != people.end(); ++prev_it, ++it) {
        EXPECT_LE(prev_it->age, it->age);
    }
}

TYPED_TEST(OrderByTest, SingleFieldDesc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age DESC
    auto result = qs.template order_by<^^OrderByPerson::age, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify descending order
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 40); // Frank or Jack
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 40);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 35); // Charlie or Henry
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->age, 35);
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->age, 30); // Alice, Eve, or Ivy
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 6)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 7)->age, 25); // Bob, David, or Grace
    EXPECT_EQ(std::ranges::next(people.begin(), 8)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 9)->age, 25);
}

TYPED_TEST(OrderByTest, StringFieldAsc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by name ASC
    auto result = qs.template order_by<^^OrderByPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify alphabetical order
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "Alice");
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "Bob");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Charlie");
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->name, "Frank");
    EXPECT_EQ(std::ranges::next(people.begin(), 6)->name, "Grace");
    EXPECT_EQ(std::ranges::next(people.begin(), 7)->name, "Henry");
    EXPECT_EQ(std::ranges::next(people.begin(), 8)->name, "Ivy");
    EXPECT_EQ(std::ranges::next(people.begin(), 9)->name, "Jack");
}

TYPED_TEST(OrderByTest, StringFieldDesc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by name DESC
    auto result = qs.template order_by<^^OrderByPerson::name, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify reverse alphabetical order
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "Jack");
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "Ivy");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Henry");
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->name, "Grace");
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->name, "Frank");
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 6)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 7)->name, "Charlie");
    EXPECT_EQ(std::ranges::next(people.begin(), 8)->name, "Bob");
    EXPECT_EQ(std::ranges::next(people.begin(), 9)->name, "Alice");
}

// ============================================================================
// Multiple Fields ORDER BY Tests
// ============================================================================

TYPED_TEST(OrderByTest, MultipleFieldsAllAsc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age ASC, then name ASC
    auto result = qs.template order_by<^^OrderByPerson::age, ^^OrderByPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify ordering: age ASC, then name ASC within same age
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "Bob"); // Age 25: Bob, David, Grace
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Grace");
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->name, "Alice"); // Age 30: Alice, Eve, Ivy
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->name, "Ivy");
}

TYPED_TEST(OrderByTest, MultipleFieldsMixedDirections) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age ASC, then name DESC
    auto result = qs.template order_by<^^OrderByPerson::age, true, ^^OrderByPerson::name, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify ordering: age ASC, then name DESC within same age
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "Grace"); // Age 25: Grace, David, Bob (DESC)
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Bob");
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->name, "Ivy"); // Age 30: Ivy, Eve, Alice (DESC)
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 4)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->age, 30);
    EXPECT_EQ(std::ranges::next(people.begin(), 5)->name, "Alice");
}

TYPED_TEST(OrderByTest, MultipleFieldsAllDesc) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by age DESC, then name DESC
    auto result = qs.template order_by<^^OrderByPerson::age, false, ^^OrderByPerson::name, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // Verify ordering: age DESC, then name DESC
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 40);
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "Jack"); // Age 40: Jack, Frank (DESC)
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 40);
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "Frank");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 35);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Henry"); // Age 35: Henry, Charlie (DESC)
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->age, 35);
    EXPECT_EQ(std::ranges::next(people.begin(), 3)->name, "Charlie");
}

// ============================================================================
// Combined with WHERE Tests
// ============================================================================

TYPED_TEST(OrderByTest, WithWhereClause) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Filter active users and order by age DESC
    auto result = qs.where(field<^^OrderByPerson::is_active>() == true) // NOLINT(readability-simplify-boolean-expr)
                          .template order_by<^^OrderByPerson::age, false>()
                          .select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 6); // Alice, Bob, David, Frank, Henry, Ivy

    // Verify all are active and ordered by age DESC
    for (const auto& person : people) {
        EXPECT_TRUE(person.is_active);
    }
    EXPECT_GE(std::ranges::next(people.begin(), 0)->age, std::ranges::next(people.begin(), 1)->age);
    EXPECT_GE(std::ranges::next(people.begin(), 1)->age, std::ranges::next(people.begin(), 2)->age);
}

TYPED_TEST(OrderByTest, WhereWithMultipleOrderBy) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Filter age >= 30 and order by age ASC, name DESC
    auto result = qs.where(field<^^OrderByPerson::age>() >= 30)
                          .template order_by<^^OrderByPerson::age, true, ^^OrderByPerson::name, false>()
                          .select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 7); // Alice, Charlie, Eve, Frank, Henry, Ivy, Jack

    // Verify age filter
    for (const auto& person : people) {
        EXPECT_GE(person.age, 30);
    }
}

// ============================================================================
// Combined with LIMIT/OFFSET Tests
// ============================================================================

TYPED_TEST(OrderByTest, WithLimit) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Get youngest 3 people
    auto result = qs.template order_by<^^OrderByPerson::age>().limit(3).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 3);

    // All should be age 25
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 25);
}

TYPED_TEST(OrderByTest, WithLimitAndOffset) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Get people ranked 4-6 by name
    auto result = qs.template order_by<^^OrderByPerson::name>().limit(3).offset(3).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 3);

    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Frank");
}

TYPED_TEST(OrderByTest, OrderByBeforeLimitOffset) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Test that order_by works correctly with limit and offset
    auto result = qs.template order_by<^^OrderByPerson::age, false>().limit(5).offset(2).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 5);

    // Should get ages in descending order, skipping first 2 (both 40)
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 35); // Charlie or Henry
}

// ============================================================================
// Edge Cases
// ============================================================================

TYPED_TEST(OrderByTest, EmptyResult) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Filter that returns no results
    auto result = qs.where(field<^^OrderByPerson::age>() > 100).template order_by<^^OrderByPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0);
}

TYPED_TEST(OrderByTest, BooleanField) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Order by boolean field
    auto result = qs.template order_by<^^OrderByPerson::is_active, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // true comes before false in DESC order (1 > 0)
    EXPECT_TRUE(std::ranges::next(people.begin(), 0)->is_active);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(OrderByTest, ChainedWithMultipleClauses) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Complex query: WHERE + ORDER BY + LIMIT + OFFSET
    auto result = qs.where(field<^^OrderByPerson::age>() >= 25)
                          .template order_by<^^OrderByPerson::age, ^^OrderByPerson::name>()
                          .limit(5)
                          .offset(1)
                          .select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 5);

    // Verify ordering maintained with all clauses
    auto prev_it = people.begin();
    auto it      = std::ranges::next(prev_it);
    for (; it != people.end(); ++prev_it, ++it) {
        if (prev_it->age == it->age) {
            EXPECT_LE(prev_it->name, it->name);
        } else {
            EXPECT_LE(prev_it->age, it->age);
        }
    }
}

// ============================================================================
// Nullable Field ORDER BY Tests (NULL ordering)
// ============================================================================

template <typename ConnType> class OrderByNullableTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<OrderByNullable, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<OrderByNullable, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE OrderByNullable ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "score INTEGER, "
                "name TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"OrderByNullable"});

        // Insert test data with mix of NULL and non-NULL values
        std::vector<OrderByNullable> const test_data = {
                {1, std::optional<int>(100), "Alice"},
                {2, std::nullopt, "Bob"},
                {3, std::optional<int>(50), "Charlie"},
                {4, std::nullopt, "David"},
                {5, std::optional<int>(75), "Eve"},
                {6, std::nullopt, "Frank"},
                {7, std::optional<int>(25), "Grace"},
                {8, std::optional<int>(100), "Henry"},
        };

        QuerySet<OrderByNullable, ConnType> qs;
        auto                                insert_result = qs.insert(test_data);
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<OrderByNullable, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<OrderByNullable, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<OrderByNullable, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(OrderByNullableTest, DatabaseTypes);

TYPED_TEST(OrderByNullableTest, NullableFieldAsc) {
    QuerySet<OrderByNullable, TypeParam> qs;

    // Order by nullable score ASC
    // In SQLite, NULL values sort first in ASC order by default
    auto result = qs.template order_by<^^OrderByNullable::score>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // SQLite default: NULL values come first in ASC order
    // First 3 should be NULL (Bob, David, Frank - sorted by insert order within NULLs)
    auto it = items.begin();
    EXPECT_FALSE(it->score.has_value()); // NULL
    ++it;
    EXPECT_FALSE(it->score.has_value()); // NULL
    ++it;
    EXPECT_FALSE(it->score.has_value()); // NULL

    // Then non-NULL values in ascending order: 25, 50, 75, 100, 100
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 25); // Grace
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 50); // Charlie
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 75); // Eve
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 100); // Alice or Henry
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 100); // Alice or Henry
}

TYPED_TEST(OrderByNullableTest, NullableFieldDesc) {
    QuerySet<OrderByNullable, TypeParam> qs;

    // Order by nullable score DESC
    // In SQLite, NULL values sort last in DESC order by default
    auto result = qs.template order_by<^^OrderByNullable::score, false>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // Non-NULL values first in descending order: 100, 100, 75, 50, 25
    auto it = items.begin();
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 100);
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 100);
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 75);
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 50);
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 25);

    // Then NULL values at the end
    ++it;
    EXPECT_FALSE(it->score.has_value());
    ++it;
    EXPECT_FALSE(it->score.has_value());
    ++it;
    EXPECT_FALSE(it->score.has_value());
}

TYPED_TEST(OrderByNullableTest, NullableFieldWithSecondarySort) {
    QuerySet<OrderByNullable, TypeParam> qs;

    // Order by score ASC, then name ASC (to have deterministic ordering within same scores)
    auto result = qs.template order_by<^^OrderByNullable::score, true, ^^OrderByNullable::name, true>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // NULLs first, sorted by name: Bob, David, Frank
    auto it = items.begin();
    EXPECT_FALSE(it->score.has_value());
    EXPECT_EQ(it->name, "Bob");
    ++it;
    EXPECT_FALSE(it->score.has_value());
    EXPECT_EQ(it->name, "David");
    ++it;
    EXPECT_FALSE(it->score.has_value());
    EXPECT_EQ(it->name, "Frank");

    // Non-NULL values sorted by score, then name
    ++it;
    EXPECT_EQ(it->score.value(), 25);
    EXPECT_EQ(it->name, "Grace");
    ++it;
    EXPECT_EQ(it->score.value(), 50);
    EXPECT_EQ(it->name, "Charlie");
    ++it;
    EXPECT_EQ(it->score.value(), 75);
    EXPECT_EQ(it->name, "Eve");
    ++it;
    EXPECT_EQ(it->score.value(), 100);
    EXPECT_EQ(it->name, "Alice"); // Alice before Henry alphabetically
    ++it;
    EXPECT_EQ(it->score.value(), 100);
    EXPECT_EQ(it->name, "Henry");
}

TYPED_TEST(OrderByNullableTest, AllNullValues) {
    // Create a new table with only NULL values
    const auto& conn = QuerySet<OrderByNullable, TypeParam>::get_default_connection();
    (void)conn->execute("DELETE FROM OrderByNullable");

    QuerySet<OrderByNullable, TypeParam> qs;
    std::vector<OrderByNullable> const   all_nulls = {
            {1, std::nullopt, "First"},
            {2, std::nullopt, "Second"},
            {3, std::nullopt, "Third"},
    };
    auto insert_result = qs.insert(all_nulls);
    ASSERT_TRUE(insert_result.has_value());

    // Order by nullable field when all values are NULL
    auto result = qs.template order_by<^^OrderByNullable::score>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 3);

    // All should be NULL
    for (const auto& item : items) {
        EXPECT_FALSE(item.score.has_value());
    }
}

// ============================================================================
// BLOB Field ORDER BY Tests
// ============================================================================

template <typename ConnType> class OrderByBlobTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result =
                QuerySet<OrderByBlob, ConnType>::set_default_connection(storm::test::get_connection_string<ConnType>());
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<OrderByBlob, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE OrderByBlob ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "data BLOB, "
                "label TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"OrderByBlob"});

        // Insert test data with various BLOB values
        // SQLite compares BLOBs byte-by-byte in memcmp order
        std::vector<OrderByBlob> const test_data = {
                {1, {0x03, 0x00}, "C"},  // Starts with 0x03
                {2, {0x01, 0x00}, "A"},  // Starts with 0x01
                {3, {0x02, 0x00}, "B"},  // Starts with 0x02
                {4, {0x01, 0x01}, "A2"}, // Same first byte, different second
                {5, {}, "Empty"},        // Empty BLOB
                {6, {0x01}, "A_short"},  // Shorter BLOB
        };

        QuerySet<OrderByBlob, ConnType> qs;
        auto                            insert_result = qs.insert(test_data);
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<OrderByBlob, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<OrderByBlob, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<OrderByBlob, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(OrderByBlobTest, DatabaseTypes);

TYPED_TEST(OrderByBlobTest, BlobFieldAsc) {
    QuerySet<OrderByBlob, TypeParam> qs;

    // Order by BLOB field ASC
    // SQLite sorts BLOBs by memcmp order (byte-by-byte comparison)
    auto result = qs.template order_by<^^OrderByBlob::data>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 6);

    // Expected order:
    // 1. Empty BLOB (sorts first - zero-length)
    // 2. {0x01} - shortest starting with 0x01
    // 3. {0x01, 0x00} - 0x01 prefix, then 0x00
    // 4. {0x01, 0x01} - 0x01 prefix, then 0x01
    // 5. {0x02, 0x00} - starts with 0x02
    // 6. {0x03, 0x00} - starts with 0x03

    auto it = items.begin();
    EXPECT_EQ(it->label, "Empty");
    EXPECT_TRUE(it->data.empty());
    ++it;
    EXPECT_EQ(it->label, "A_short");
    EXPECT_EQ(it->data, (std::vector<uint8_t>{0x01}));
    ++it;
    EXPECT_EQ(it->label, "A");
    EXPECT_EQ(it->data, (std::vector<uint8_t>{0x01, 0x00}));
    ++it;
    EXPECT_EQ(it->label, "A2");
    EXPECT_EQ(it->data, (std::vector<uint8_t>{0x01, 0x01}));
    ++it;
    EXPECT_EQ(it->label, "B");
    EXPECT_EQ(it->data, (std::vector<uint8_t>{0x02, 0x00}));
    ++it;
    EXPECT_EQ(it->label, "C");
    EXPECT_EQ(it->data, (std::vector<uint8_t>{0x03, 0x00}));
}

TYPED_TEST(OrderByBlobTest, BlobFieldDesc) {
    QuerySet<OrderByBlob, TypeParam> qs;

    // Order by BLOB field DESC
    auto result = qs.template order_by<^^OrderByBlob::data, false>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 6);

    // Expected order (reverse of ASC):
    // 1. {0x03, 0x00}
    // 2. {0x02, 0x00}
    // 3. {0x01, 0x01}
    // 4. {0x01, 0x00}
    // 5. {0x01}
    // 6. Empty

    auto it = items.begin();
    EXPECT_EQ(it->label, "C");
    ++it;
    EXPECT_EQ(it->label, "B");
    ++it;
    EXPECT_EQ(it->label, "A2");
    ++it;
    EXPECT_EQ(it->label, "A");
    ++it;
    EXPECT_EQ(it->label, "A_short");
    ++it;
    EXPECT_EQ(it->label, "Empty");
}

TYPED_TEST(OrderByBlobTest, BlobWithSecondarySort) {
    QuerySet<OrderByBlob, TypeParam> qs;

    // Order by BLOB ASC, then label ASC
    auto result = qs.template order_by<^^OrderByBlob::data, true, ^^OrderByBlob::label, true>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 6);

    // Verify ordering is consistent
    EXPECT_EQ(items.begin()->label, "Empty"); // Empty BLOB first
}

TYPED_TEST(OrderByBlobTest, EmptyBlobsOnly) {
    // Test with only empty BLOBs
    const auto& conn = QuerySet<OrderByBlob, TypeParam>::get_default_connection();
    (void)conn->execute("DELETE FROM OrderByBlob");

    QuerySet<OrderByBlob, TypeParam> qs;
    std::vector<OrderByBlob> const   empty_blobs = {
            {1, {}, "First"},
            {2, {}, "Second"},
            {3, {}, "Third"},
    };
    auto insert_result = qs.insert(empty_blobs);
    ASSERT_TRUE(insert_result.has_value());

    auto result = qs.template order_by<^^OrderByBlob::data>().select();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 3);

    // All empty BLOBs should be present
    for (const auto& item : items) {
        EXPECT_TRUE(item.data.empty());
    }
}

// ============================================================================
// ORDER BY with Empty Result Set (Additional scenarios)
// ============================================================================

TYPED_TEST(OrderByTest, EmptyResultWithMultipleOrderBy) {
    QuerySet<OrderByPerson, TypeParam> qs;

    // Complex ORDER BY with no matching results
    auto result = qs.where(field<^^OrderByPerson::age>() > 100)
                          .template order_by<^^OrderByPerson::age, false, ^^OrderByPerson::name, true>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0);
}

TYPED_TEST(OrderByTest, EmptyTableOrderBy) {
    // Create empty table and try ORDER BY
    const auto& conn = QuerySet<OrderByPerson, TypeParam>::get_default_connection();
    (void)conn->execute("DELETE FROM OrderByPerson");

    QuerySet<OrderByPerson, TypeParam> qs;
    auto                               result = qs.template order_by<^^OrderByPerson::age>().select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0);
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
