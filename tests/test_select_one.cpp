#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;

using namespace storm;
using namespace storm::orm::where;

// Test model for first/get operations
struct SelectOnePerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
};

// Test fixture for first/get operations — templated on database backend
template <typename ConnType> class SelectOneTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<SelectOnePerson, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<SelectOnePerson, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE SelectOnePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"SelectOnePerson"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<SelectOnePerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<SelectOnePerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<SelectOnePerson, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(SelectOneTest, DatabaseTypes);

// =====================================================================
// first() tests — returns std::optional<T>, applies LIMIT 1
// =====================================================================

// Test: first() from empty table returns nullopt
TYPED_TEST(SelectOneTest, FirstFromEmptyTable) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    auto result = queryset.first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    EXPECT_FALSE(result.value().has_value()) << "Expected nullopt from empty table";
}

// Test: first() with single row returns that row
TYPED_TEST(SelectOneTest, FirstSingleRow) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    SelectOnePerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto                  insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    auto result = queryset.first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected a row, got nullopt";

    const auto& person = result.value().value();
    EXPECT_EQ(person.name, "Alice");
    EXPECT_EQ(person.age, 30);
}

// Test: first() with multiple rows returns first row (LIMIT 1)
TYPED_TEST(SelectOneTest, FirstMultipleRows) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    auto result = queryset.first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected a row, got nullopt";

    // Should return the first row
    EXPECT_EQ(result.value().value().name, "Alice");
}

// Test: first() with WHERE clause
TYPED_TEST(SelectOneTest, FirstWithWhere) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    auto result = queryset.where(field<^^SelectOnePerson::name>() == "Bob").first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected Bob, got nullopt";

    const auto& person = result.value().value();
    EXPECT_EQ(person.name, "Bob");
    EXPECT_EQ(person.age, 25);
}

// Test: first() with WHERE returning no rows returns nullopt
TYPED_TEST(SelectOneTest, FirstWhereNoMatch) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    SelectOnePerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto                  insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    auto result = queryset.where(field<^^SelectOnePerson::name>() == "NonExistent").first().execute();
    ASSERT_TRUE(result.has_value()) << "first() should not return error for no match";
    EXPECT_FALSE(result.value().has_value()) << "Expected nullopt for non-matching WHERE";
}

// Test: first() with ORDER BY ASC
TYPED_TEST(SelectOneTest, FirstWithOrderBy) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    // ORDER BY age ASC - should get Bob (age 25)
    auto result = queryset.template order_by<^^SelectOnePerson::age>().first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected a row, got nullopt";
    EXPECT_EQ(result.value().value().name, "Bob");
}

// Test: first() with ORDER BY DESC
TYPED_TEST(SelectOneTest, FirstWithOrderByDesc) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    // ORDER BY age DESC - should get Charlie (age 35)
    auto result = queryset.template order_by<^^SelectOnePerson::age, false>().first().execute();
    ASSERT_TRUE(result.has_value()) << "first() failed: " << result.error().message();
    ASSERT_TRUE(result.value().has_value()) << "Expected a row, got nullopt";
    EXPECT_EQ(result.value().value().name, "Charlie");
}

// Test: Multiple first() calls reuse cached statement
TYPED_TEST(SelectOneTest, FirstMultipleCallsUseCaching) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    SelectOnePerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto                  insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());

    for (int i = 0; i < 3; ++i) {
        auto result = queryset.first().execute();
        ASSERT_TRUE(result.has_value()) << "first() call " << i << " failed";
        ASSERT_TRUE(result.value().has_value()) << "Expected a row on call " << i;
        EXPECT_EQ(result.value().value().name, "Alice");
    }
}

// =====================================================================
// get() tests — returns exactly one row, errors on 0 or >1 rows
// =====================================================================

// Test: get() from empty table returns error
TYPED_TEST(SelectOneTest, GetFromEmptyTable) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    auto result = queryset.get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error for empty table";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "No row found matching query");
}

// Test: get() with single row returns that row
TYPED_TEST(SelectOneTest, GetSingleRow) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    SelectOnePerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto                  insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    auto result = queryset.get().execute();
    ASSERT_TRUE(result.has_value()) << "get() failed: " << result.error().message();

    EXPECT_EQ(result.value().name, "Alice");
    EXPECT_EQ(result.value().age, 30);
}

// Test: get() with multiple rows returns error (uniqueness violation)
TYPED_TEST(SelectOneTest, GetMultipleRowsReturnsError) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    auto result = queryset.get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error for multiple rows";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "Multiple rows found matching query");
}

// Test: get() with WHERE clause returning exactly one row
TYPED_TEST(SelectOneTest, GetWithWhere) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 25}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    auto result = queryset.where(field<^^SelectOnePerson::name>() == "Bob").get().execute();
    ASSERT_TRUE(result.has_value()) << "get() failed: " << result.error().message();
    EXPECT_EQ(result.value().name, "Bob");
    EXPECT_EQ(result.value().age, 25);
}

// Test: get() with WHERE returning no rows returns error
TYPED_TEST(SelectOneTest, GetWhereNoMatch) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    SelectOnePerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto                  insert_result = queryset.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    auto result = queryset.where(field<^^SelectOnePerson::name>() == "NonExistent").get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error for non-matching WHERE";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "No row found matching query");
}

// Test: get() with WHERE matching multiple rows returns error
TYPED_TEST(SelectOneTest, GetWhereMultipleMatch) {
    QuerySet<SelectOnePerson, TypeParam> queryset;

    for (const auto& p : std::vector<SelectOnePerson>{{0, "Alice", 30}, {0, "Bob", 30}, {0, "Charlie", 25}}) {
        auto insert_result = queryset.insert(p).execute();
        ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();
    }

    // Two people have age 30
    auto result = queryset.where(field<^^SelectOnePerson::age>() == 30).get().execute();
    ASSERT_FALSE(result.has_value()) << "get() should return error when WHERE matches multiple rows";
    EXPECT_EQ(result.error().code(), -1);
    EXPECT_EQ(result.error().message(), "Multiple rows found matching query");
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
