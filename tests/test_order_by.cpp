// test_order_by.cpp - Comprehensive tests for ORDER BY functionality
#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;
using namespace storm::orm::where;

// Test model
struct OrderByPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
    bool                                      is_active;
};

class OrderByTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up in-memory SQLite database
        auto result = QuerySet<OrderByPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<OrderByPerson>::get_default_connection();

        // Create table
        auto create_result = conn->execute(
                "CREATE TABLE OrderByPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "is_active INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert test data with varying values
        std::vector<OrderByPerson> test_data = {
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

        QuerySet<OrderByPerson> qs;
        for (const auto& person : test_data) {
            auto insert_result = qs.insert(person);
            ASSERT_TRUE(insert_result.has_value()) << "Failed to insert person: " << person.name;
        }
    }

    void TearDown() override {
        QuerySet<OrderByPerson>::clear_default_connection();
    }
};

// ============================================================================
// Basic ORDER BY Tests
// ============================================================================

TEST_F(OrderByTest, SingleFieldDefaultAsc) {
    QuerySet<OrderByPerson> qs;

    // Order by age (default ASC)
    auto result = qs.order_by<^^OrderByPerson::age>().select();
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

TEST_F(OrderByTest, SingleFieldExplicitAsc) {
    QuerySet<OrderByPerson> qs;

    // Order by age (explicit ASC)
    auto result = qs.order_by<^^OrderByPerson::age, true>().select();
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

TEST_F(OrderByTest, SingleFieldDesc) {
    QuerySet<OrderByPerson> qs;

    // Order by age DESC
    auto result = qs.order_by<^^OrderByPerson::age, false>().select();
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

TEST_F(OrderByTest, StringFieldAsc) {
    QuerySet<OrderByPerson> qs;

    // Order by name ASC
    auto result = qs.order_by<^^OrderByPerson::name>().select();
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

TEST_F(OrderByTest, StringFieldDesc) {
    QuerySet<OrderByPerson> qs;

    // Order by name DESC
    auto result = qs.order_by<^^OrderByPerson::name, false>().select();
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

TEST_F(OrderByTest, MultipleFieldsAllAsc) {
    QuerySet<OrderByPerson> qs;

    // Order by age ASC, then name ASC
    auto result = qs.order_by<^^OrderByPerson::age, ^^OrderByPerson::name>().select();
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

TEST_F(OrderByTest, MultipleFieldsMixedDirections) {
    QuerySet<OrderByPerson> qs;

    // Order by age ASC, then name DESC
    auto result = qs.order_by<^^OrderByPerson::age, true, ^^OrderByPerson::name, false>().select();
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

TEST_F(OrderByTest, MultipleFieldsAllDesc) {
    QuerySet<OrderByPerson> qs;

    // Order by age DESC, then name DESC
    auto result = qs.order_by<^^OrderByPerson::age, false, ^^OrderByPerson::name, false>().select();
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

TEST_F(OrderByTest, WithWhereClause) {
    QuerySet<OrderByPerson> qs;

    // Filter active users and order by age DESC
    auto result =
            qs.where(field<^^OrderByPerson::is_active>() == true).order_by<^^OrderByPerson::age, false>().select();
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

TEST_F(OrderByTest, WhereWithMultipleOrderBy) {
    QuerySet<OrderByPerson> qs;

    // Filter age >= 30 and order by age ASC, name DESC
    auto result = qs.where(field<^^OrderByPerson::age>() >= 30)
                          .order_by<^^OrderByPerson::age, true, ^^OrderByPerson::name, false>()
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

TEST_F(OrderByTest, WithLimit) {
    QuerySet<OrderByPerson> qs;

    // Get youngest 3 people
    auto result = qs.order_by<^^OrderByPerson::age>().limit(3).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 3);

    // All should be age 25
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->age, 25);
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->age, 25);
}

TEST_F(OrderByTest, WithLimitAndOffset) {
    QuerySet<OrderByPerson> qs;

    // Get people ranked 4-6 by name
    auto result = qs.order_by<^^OrderByPerson::name>().limit(3).offset(3).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 3);

    EXPECT_EQ(std::ranges::next(people.begin(), 0)->name, "David");
    EXPECT_EQ(std::ranges::next(people.begin(), 1)->name, "Eve");
    EXPECT_EQ(std::ranges::next(people.begin(), 2)->name, "Frank");
}

TEST_F(OrderByTest, OrderByBeforeLimitOffset) {
    QuerySet<OrderByPerson> qs;

    // Test that order_by works correctly with limit and offset
    auto result = qs.order_by<^^OrderByPerson::age, false>().limit(5).offset(2).select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 5);

    // Should get ages in descending order, skipping first 2 (both 40)
    EXPECT_EQ(std::ranges::next(people.begin(), 0)->age, 35); // Charlie or Henry
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(OrderByTest, EmptyResult) {
    QuerySet<OrderByPerson> qs;

    // Filter that returns no results
    auto result = qs.where(field<^^OrderByPerson::age>() > 100).order_by<^^OrderByPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    EXPECT_EQ(people.size(), 0);
}

TEST_F(OrderByTest, BooleanField) {
    QuerySet<OrderByPerson> qs;

    // Order by boolean field
    auto result = qs.order_by<^^OrderByPerson::is_active, false>().select();
    ASSERT_TRUE(result.has_value());

    auto people = result.value();
    ASSERT_EQ(people.size(), 10);

    // true comes before false in DESC order (1 > 0)
    EXPECT_TRUE(std::ranges::next(people.begin(), 0)->is_active);
}

TEST_F(OrderByTest, ChainedWithMultipleClauses) {
    QuerySet<OrderByPerson> qs;

    // Complex query: WHERE + ORDER BY + LIMIT + OFFSET
    auto result = qs.where(field<^^OrderByPerson::age>() >= 25)
                          .order_by<^^OrderByPerson::age, ^^OrderByPerson::name>()
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
