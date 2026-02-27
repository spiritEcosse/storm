#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;
using namespace storm::orm::where;

#include "test_models.h" // NOSONAR cpp:S954

// Test fixture for LIMIT/OFFSET operations — templated on database backend
template <typename ConnType> class LimitOffsetTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
            return;
        }

        const auto& conn = QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<Person, ConnType>(conn);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"Person"});

        // Insert 20 test records for comprehensive testing
        QuerySet<Person, ConnType> queryset;
        std::vector<Person>        people;
        for (int i = 1; i <= 20; i++) {
            people.emplace_back(i, "Person" + std::to_string(i), 20 + i);
        }
        auto insert_result = queryset.insert(std::span<const Person>(people)).execute();
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
    }
};

TYPED_TEST_SUITE(LimitOffsetTest, DatabaseTypes);

// ===== Basic LIMIT Tests =====

TYPED_TEST(LimitOffsetTest, LimitOnly) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(5).select().execute();
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

TYPED_TEST(LimitOffsetTest, LimitZero) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(0).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT 0 failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0) << "Expected 0 rows with LIMIT 0";
}

TYPED_TEST(LimitOffsetTest, LimitGreaterThanResultSet) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(100).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT with large LIMIT failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 20) << "Expected all 20 rows when LIMIT > result set size";
}

// ===== Basic OFFSET Tests =====

TYPED_TEST(LimitOffsetTest, OffsetOnly) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.offset(10).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 10) << "Expected 10 rows (20 total - 10 offset)";

    // Verify we got records 11-20
    size_t i = 0;
    for (auto it = people.begin(); it != people.end(); ++it, ++i) {
        EXPECT_EQ(it->id, i + 11);
    }
}

TYPED_TEST(LimitOffsetTest, OffsetZero) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.offset(0).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET 0 failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 20) << "Expected all rows with OFFSET 0";
}

TYPED_TEST(LimitOffsetTest, OffsetGreaterThanResultSet) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.offset(100).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT with large OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0) << "Expected 0 rows when OFFSET > result set size";
}

// ===== Combined LIMIT/OFFSET Tests =====

TYPED_TEST(LimitOffsetTest, LimitAndOffset) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(5).offset(5).select().execute();
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

TYPED_TEST(LimitOffsetTest, LimitOffsetPagination) {
    QuerySet<Person, TypeParam> qs;

    // Page 1: records 1-5
    auto page1 = qs.limit(5).offset(0).select().execute();
    ASSERT_TRUE(page1.has_value());
    ASSERT_EQ(page1.value().size(), 5);
    EXPECT_EQ(page1.value().begin()->id, 1);
    EXPECT_EQ(std::ranges::next(page1.value().begin(), 4)->id, 5);

    // Page 2: records 6-10
    auto page2 = qs.limit(5).offset(5).select().execute();
    ASSERT_TRUE(page2.has_value());
    ASSERT_EQ(page2.value().size(), 5);
    EXPECT_EQ(page2.value().begin()->id, 6);
    EXPECT_EQ(std::ranges::next(page2.value().begin(), 4)->id, 10);

    // Page 3: records 11-15
    auto page3 = qs.limit(5).offset(10).select().execute();
    ASSERT_TRUE(page3.has_value());
    ASSERT_EQ(page3.value().size(), 5);
    EXPECT_EQ(page3.value().begin()->id, 11);
    EXPECT_EQ(std::ranges::next(page3.value().begin(), 4)->id, 15);

    // Page 4: records 16-20
    auto page4 = qs.limit(5).offset(15).select().execute();
    ASSERT_TRUE(page4.has_value());
    ASSERT_EQ(page4.value().size(), 5);
    EXPECT_EQ(page4.value().begin()->id, 16);
    EXPECT_EQ(std::ranges::next(page4.value().begin(), 4)->id, 20);

    // Page 5: no more records
    auto page5 = qs.limit(5).offset(20).select().execute();
    ASSERT_TRUE(page5.has_value());
    EXPECT_EQ(page5.value().size(), 0);
}

// ===== LIMIT/OFFSET with WHERE =====

TYPED_TEST(LimitOffsetTest, LimitWithWhere) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(field<^^Person::age>() > 25).limit(3).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with LIMIT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 rows matching WHERE with LIMIT";

    // Verify all ages > 25
    for (const auto& person : people) {
        EXPECT_GT(person.age, 25);
    }
}

TYPED_TEST(LimitOffsetTest, LimitOffsetWithWhere) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(field<^^Person::age>() > 25).limit(3).offset(2).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with LIMIT/OFFSET failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 rows";

    // Verify all ages > 25
    for (const auto& person : people) {
        EXPECT_GT(person.age, 25);
    }
}

TYPED_TEST(LimitOffsetTest, WhereWithOffsetNoLimit) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(field<^^Person::age>() < 30).offset(2).select().execute();
    ASSERT_TRUE(result.has_value()) << "SELECT WHERE with OFFSET failed: " << result.error().message();

    // Should get all matching records except first 2
    const auto& people = result.value();
    EXPECT_GT(people.size(), 0);
    for (const auto& person : people) {
        EXPECT_LT(person.age, 30);
    }
}

// ===== LIMIT/OFFSET with DISTINCT =====

TYPED_TEST(LimitOffsetTest, DistinctWithLimit) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(5).template distinct<^^Person::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT with LIMIT failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 5) << "Expected 5 distinct names";
}

TYPED_TEST(LimitOffsetTest, DistinctWithLimitOffset) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(5).offset(3).template distinct<^^Person::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT with LIMIT/OFFSET failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 5) << "Expected 5 distinct names";
}

TYPED_TEST(LimitOffsetTest, DistinctMultiFieldWithLimit) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(10).template distinct<^^Person::name, ^^Person::age>().select();
    ASSERT_TRUE(result.has_value()) << "Multi-field DISTINCT with LIMIT failed: " << result.error().message();

    const auto& pairs = result.value();
    ASSERT_EQ(pairs.size(), 10) << "Expected 10 distinct name/age pairs";
}

TYPED_TEST(LimitOffsetTest, DistinctWithWhereAndLimit) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(field<^^Person::age>() > 30).limit(3).template distinct<^^Person::name>().select();
    ASSERT_TRUE(result.has_value()) << "DISTINCT WHERE with LIMIT failed: " << result.error().message();

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 3) << "Expected 3 distinct names";
}

// ===== Reset State Tests =====

TYPED_TEST(LimitOffsetTest, ResetClearsLimitOffset) {
    QuerySet<Person, TypeParam> qs;

    // First query with LIMIT/OFFSET
    auto result1 = qs.limit(5).offset(5).select().execute();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);

    // Reset and query again
    qs.reset();
    auto result2 = qs.select().execute();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 20) << "Expected all rows after reset";
}

TYPED_TEST(LimitOffsetTest, ReuseLimitOffset) {
    QuerySet<Person, TypeParam> qs;

    // Set LIMIT/OFFSET once
    qs.limit(5).offset(10);

    // First query
    auto result1 = qs.select().execute();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);
    EXPECT_EQ(result1.value().begin()->id, 11);

    // Second query reuses same LIMIT/OFFSET
    auto result2 = qs.select().execute();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5);
    EXPECT_EQ(result2.value().begin()->id, 11);
}

TYPED_TEST(LimitOffsetTest, OverwriteLimitOffset) {
    QuerySet<Person, TypeParam> qs;

    // First LIMIT/OFFSET
    auto result1 = qs.limit(5).offset(0).select().execute();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5);
    EXPECT_EQ(result1.value().begin()->id, 1);

    // Change LIMIT/OFFSET
    auto result2 = qs.limit(3).offset(10).select().execute();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 3);
    EXPECT_EQ(result2.value().begin()->id, 11);
}

// ===== Edge Cases =====

TYPED_TEST(LimitOffsetTest, LimitOffsetAtBoundary) {
    QuerySet<Person, TypeParam> qs;

    // Exactly at the end
    auto result = qs.limit(5).offset(15).select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 5) << "Expected exactly 5 rows (records 16-20)";
    EXPECT_EQ(result.value().begin()->id, 16);
    EXPECT_EQ(std::ranges::next(result.value().begin(), 4)->id, 20);
}

TYPED_TEST(LimitOffsetTest, LimitOffsetPartialPage) {
    QuerySet<Person, TypeParam> qs;

    // Request more than available
    auto result = qs.limit(10).offset(15).select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 5) << "Expected only 5 rows (records 16-20)";
}

TYPED_TEST(LimitOffsetTest, LimitOne) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(1).select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->id, 1);
}

TYPED_TEST(LimitOffsetTest, OffsetOne) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.limit(1).offset(1).select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->id, 2);
}

// ===== SQL Clause Ordering =====

TYPED_TEST(LimitOffsetTest, MethodChainingOrder) {
    QuerySet<Person, TypeParam> qs;

    // Methods can be chained in any order
    auto result1 = qs.limit(5).offset(3).select().execute();
    ASSERT_TRUE(result1.has_value());

    qs.reset();
    auto result2 = qs.offset(3).limit(5).select().execute();
    ASSERT_TRUE(result2.has_value());

    // Results should be identical
    ASSERT_EQ(result1.value().size(), result2.value().size());
    auto it1 = result1.value().begin();
    auto it2 = result2.value().begin();
    for (; it1 != result1.value().end() && it2 != result2.value().end(); ++it1, ++it2) {
        EXPECT_EQ(it1->id, it2->id);
    }
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
