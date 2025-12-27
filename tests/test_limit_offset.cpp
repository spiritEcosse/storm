#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;
using namespace storm::orm::where;

// Test model for LIMIT/OFFSET operations
struct LimitOffsetPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Test fixture for LIMIT/OFFSET operations
class LimitOffsetTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        // Set up in-memory SQLite database
        auto result = QuerySet<LimitOffsetPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<LimitOffsetPerson>::get_default_connection();

        // Create table
        auto create_result = conn->execute(
                "CREATE TABLE LimitOffsetPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert 20 test records for comprehensive testing
        QuerySet<LimitOffsetPerson>    queryset;
        std::vector<LimitOffsetPerson> people;
        for (int i = 1; i <= 20; i++) {
            people.emplace_back(i, "Person" + std::to_string(i), 20 + i);
        }
        auto insert_result = queryset.insert(std::span<const LimitOffsetPerson>(people));
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
    }

    auto TearDown() -> void override {
        QuerySet<LimitOffsetPerson>::clear_default_connection();
    }
};

// ===== Basic LIMIT Tests =====

TEST_F(LimitOffsetTest, LimitOnly) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(5).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 5) << "Expected exactly 5 rows";

    // Verify we got the first 5 records
    size_t i = 0;
    for (auto it = people.begin(); it != people.end(); ++it, ++i) {
        EXPECT_EQ(it->id, i + 1);
        EXPECT_EQ(it->name, "Person" + std::to_string(i + 1));
    }
}

TEST_F(LimitOffsetTest, LimitZero) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(0).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT 0 failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0) << "Expected 0 rows with LIMIT 0";
}

TEST_F(LimitOffsetTest, LimitGreaterThanResultSet) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(100).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with large LIMIT failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 20) << "Expected all 20 rows when LIMIT > result set size";
}

// ===== Basic OFFSET Tests =====

TEST_F(LimitOffsetTest, OffsetOnly) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.offset(10).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 10) << "Expected 10 rows (20 total - 10 offset)";

    // Verify we got records 11-20
    size_t i = 0;
    for (auto it = people.begin(); it != people.end(); ++it, ++i) {
        EXPECT_EQ(it->id, i + 11);
    }
}

TEST_F(LimitOffsetTest, OffsetZero) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.offset(0).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET 0 failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 20) << "Expected all rows with OFFSET 0";
}

TEST_F(LimitOffsetTest, OffsetGreaterThanResultSet) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.offset(100).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with large OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0) << "Expected 0 rows when OFFSET > result set size";
}

// ===== Combined LIMIT/OFFSET Tests =====

TEST_F(LimitOffsetTest, LimitAndOffset) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(5).offset(5).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT/OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 5) << "Expected 5 rows (skip 5, take 5)";

    // Verify we got records 6-10
    size_t i = 0;
    for (auto it = people.begin(); it != people.end(); ++it, ++i) {
        EXPECT_EQ(it->id, i + 6);
        EXPECT_EQ(it->name, "Person" + std::to_string(i + 6));
    }
}

TEST_F(LimitOffsetTest, LimitOffsetPagination) {
    QuerySet<LimitOffsetPerson> qs;

    // Page 1: records 1-5
    auto page1 = qs.limit(5).offset(0).select();
    ASSERT_TRUE(page1.has_value());
    ASSERT_EQ(page1.value().size(), 5);
    EXPECT_EQ(page1.value().begin()->id, 1);
    EXPECT_EQ(std::ranges::next(page1.value().begin(), 4)->id, 5);

    // Page 2: records 6-10
    auto page2 = qs.limit(5).offset(5).select();
    ASSERT_TRUE(page2.has_value());
    ASSERT_EQ(page2.value().size(), 5);
    EXPECT_EQ(page2.value().begin()->id, 6);
    EXPECT_EQ(std::ranges::next(page2.value().begin(), 4)->id, 10);

    // Page 3: records 11-15
    auto page3 = qs.limit(5).offset(10).select();
    ASSERT_TRUE(page3.has_value());
    ASSERT_EQ(page3.value().size(), 5);
    EXPECT_EQ(page3.value().begin()->id, 11);
    EXPECT_EQ(std::ranges::next(page3.value().begin(), 4)->id, 15);

    // Page 4: records 16-20
    auto page4 = qs.limit(5).offset(15).select();
    ASSERT_TRUE(page4.has_value());
    ASSERT_EQ(page4.value().size(), 5);
    EXPECT_EQ(page4.value().begin()->id, 16);
    EXPECT_EQ(std::ranges::next(page4.value().begin(), 4)->id, 20);

    // Page 5: no more records
    auto page5 = qs.limit(5).offset(20).select();
    ASSERT_TRUE(page5.has_value());
    EXPECT_EQ(page5.value().size(), 0);
}

// ===== LIMIT/OFFSET with WHERE =====

TEST_F(LimitOffsetTest, LimitWithWhere) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.where(field<^^LimitOffsetPerson::age>() > 25).limit(3).select();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with LIMIT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 rows matching WHERE with LIMIT";

    // Verify all ages > 25
    for (const auto& person : people) {
        EXPECT_GT(person.age, 25);
    }
}

TEST_F(LimitOffsetTest, LimitOffsetWithWhere) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.where(field<^^LimitOffsetPerson::age>() > 25).limit(3).offset(2).select();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with LIMIT/OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 rows";

    // Verify all ages > 25
    for (const auto& person : people) {
        EXPECT_GT(person.age, 25);
    }
}

TEST_F(LimitOffsetTest, WhereWithOffsetNoLimit) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.where(field<^^LimitOffsetPerson::age>() < 30).offset(2).select();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with OFFSET failed: " << result.error().message();

    // Should get all matching records except first 2
    const auto& people = result.value();
    EXPECT_GT(people.size(), 0);
    for (const auto& person : people) {
        EXPECT_LT(person.age, 30);
    }
}

// ===== LIMIT/OFFSET with DISTINCT =====

TEST_F(LimitOffsetTest, DistinctWithLimit) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(5).distinct<^^LimitOffsetPerson::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT with LIMIT failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 5) << "Expected 5 distinct names";
}

TEST_F(LimitOffsetTest, DistinctWithLimitOffset) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(5).offset(3).distinct<^^LimitOffsetPerson::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT with LIMIT/OFFSET failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 5) << "Expected 5 distinct names";
}

TEST_F(LimitOffsetTest, DistinctMultiFieldWithLimit) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(10).distinct<^^LimitOffsetPerson::name, ^^LimitOffsetPerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "Multi-field DISTINCT with LIMIT failed: " << result.error().message();

    const auto& pairs = result.value();
    ASSERT_EQ(pairs.size(), 10) << "Expected 10 distinct name/age pairs";
}

TEST_F(LimitOffsetTest, DistinctWithWhereAndLimit) {
    QuerySet<LimitOffsetPerson> qs;

    auto result =
            qs.where(field<^^LimitOffsetPerson::age>() > 30).limit(3).distinct<^^LimitOffsetPerson::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT WHERE with LIMIT failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 3) << "Expected 3 distinct names";
}

// ===== Reset State Tests =====

TEST_F(LimitOffsetTest, ResetClearsLimitOffset) {
    QuerySet<LimitOffsetPerson> qs;

    // First query with LIMIT/OFFSET
    auto result1 = qs.limit(5).offset(5).select();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);

    // Reset and query again
    qs.reset();
    auto result2 = qs.select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 20) << "Expected all rows after reset";
}

TEST_F(LimitOffsetTest, ReuseLimitOffset) {
    QuerySet<LimitOffsetPerson> qs;

    // Set LIMIT/OFFSET once
    qs.limit(5).offset(10);

    // First query
    auto result1 = qs.select();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);
    EXPECT_EQ(result1.value().begin()->id, 11);

    // Second query reuses same LIMIT/OFFSET
    auto result2 = qs.select();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5);
    EXPECT_EQ(result2.value().begin()->id, 11);
}

TEST_F(LimitOffsetTest, OverwriteLimitOffset) {
    QuerySet<LimitOffsetPerson> qs;

    // First LIMIT/OFFSET
    auto result1 = qs.limit(5).offset(0).select();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);
    EXPECT_EQ(result1.value().begin()->id, 1);

    // Change LIMIT/OFFSET
    auto result2 = qs.limit(3).offset(10).select();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 3);
    EXPECT_EQ(result2.value().begin()->id, 11);
}

// ===== Edge Cases =====

TEST_F(LimitOffsetTest, LimitOffsetAtBoundary) {
    QuerySet<LimitOffsetPerson> qs;

    // Exactly at the end
    auto result = qs.limit(5).offset(15).select();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 5) << "Expected exactly 5 rows (records 16-20)";
    EXPECT_EQ(result.value().begin()->id, 16);
    EXPECT_EQ(std::ranges::next(result.value().begin(), 4)->id, 20);
}

TEST_F(LimitOffsetTest, LimitOffsetPartialPage) {
    QuerySet<LimitOffsetPerson> qs;

    // Request more than available
    auto result = qs.limit(10).offset(15).select();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 5) << "Expected only 5 rows (records 16-20)";
}

TEST_F(LimitOffsetTest, LimitOne) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(1).select();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->id, 1);
}

TEST_F(LimitOffsetTest, OffsetOne) {
    QuerySet<LimitOffsetPerson> qs;

    auto result = qs.limit(1).offset(1).select();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->id, 2);
}

// ===== SQL Clause Ordering =====

TEST_F(LimitOffsetTest, MethodChainingOrder) {
    QuerySet<LimitOffsetPerson> qs;

    // Methods can be chained in any order
    auto result1 = qs.limit(5).offset(3).select();
    ASSERT_TRUE(result1.has_value());

    qs.reset();
    auto result2 = qs.offset(3).limit(5).select();
    ASSERT_TRUE(result2.has_value());

    // Results should be identical
    ASSERT_EQ(result1.value().size(), result2.value().size());
    auto it1 = result1.value().begin();
    auto it2 = result2.value().begin();
    for (; it1 != result1.value().end() && it2 != result2.value().end(); ++it1, ++it2) {
        EXPECT_EQ(it1->id, it2->id);
    }
}
