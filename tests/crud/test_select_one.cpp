#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;

using namespace storm;
using namespace storm::orm::where;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

// Test fixture for first/get operations — templated on database backend
template <typename ConnType> class SelectOneTest : public StormTestFixture<Person, ConnType> {};

TYPED_TEST_SUITE(SelectOneTest, DatabaseTypes);

// =====================================================================
// first() tests — only non-YAML-migratable tests remain here
// =====================================================================

// Test: Multiple first() calls reuse cached statement
TYPED_TEST(SelectOneTest, FirstMultipleCallsUseCaching) {
    QuerySet<Person, TypeParam> queryset;

    Person const alice{.id = 0, .name = "Alice", .age = 30};
    auto         insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());

    for (int i = 0; i < 3; ++i) {
        auto result = queryset.first().execute();
        ASSERT_TRUE(result.has_value()) << "first() call " << i << " failed";
        ASSERT_TRUE(result.value().has_value()) << "Expected a row on call " << i;
        EXPECT_EQ(result.value().value().name, "Alice");
    }
}

// =====================================================================
// get() tests — only error-code/message tests remain here
// =====================================================================

// Test: get() from empty table returns error
TYPED_TEST(SelectOneTest, GetFromEmptyTable) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error for empty table";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "No row found matching query");
}

// Test: get() with multiple rows returns error (uniqueness violation)
TYPED_TEST(SelectOneTest, GetMultipleRowsReturnsError) {
    QuerySet<Person, TypeParam> queryset;

    const std::vector<Person> people = {{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}};
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(people)));

    auto result = queryset.get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error for multiple rows";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "Multiple rows found matching query");
}

// Test: get() with WHERE matching multiple rows returns error
TYPED_TEST(SelectOneTest, GetWhereMultipleMatch) {
    const QuerySet<Person, TypeParam> queryset;

    const std::vector<Person> people = {{0, "Alice", 30}, {0, "Bob", 30}, {0, "Charlie", 25}};
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(people)));

    // Two people have age 30
    auto result = queryset.where(field<^^Person::age>() == 30).get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error when WHERE matches multiple rows";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "Multiple rows found matching query");
}

// Test: first() with WHERE (no JOIN) — covers execute_one() non-join lambda path
TYPED_TEST(SelectOneTest, FirstWithWhereNoJoin) {
    QuerySet<Person, TypeParam> queryset;

    Person const alice{.id = 0, .name = "Alice", .age = 30};
    Person const bob{.id = 0, .name = "Bob", .age = 25};
    auto         r1 = queryset.insert(alice).execute();
    auto         r2 = queryset.insert(bob).execute();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    auto result = queryset.where(field<^^Person::age>() == 30).first().execute();
    ASSERT_TRUE(result.has_value()) << "first() with WHERE failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected a row";
    EXPECT_EQ(result.value().value().name, "Alice");
}

// Test: first() with WHERE returns nullopt when no match
TYPED_TEST(SelectOneTest, FirstWithWhereNoMatch) {
    QuerySet<Person, TypeParam> queryset;

    Person const alice{.id = 0, .name = "Alice", .age = 30};
    ASSERT_TRUE(queryset.insert(alice).execute().has_value());

    auto result = queryset.where(field<^^Person::age>() == 999).first().execute();
    ASSERT_TRUE(result.has_value()) << "first() should succeed even with no match";
    EXPECT_FALSE(result.value().has_value()) << "Expected nullopt when no rows match";
}
