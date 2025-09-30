#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;

// Test model with various field types
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Test fixture for SELECT operations
class SelectTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up in-memory SQLite database
        auto result = QuerySet<Person>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<Person>::get_default_connection();

        // Create table with AUTOINCREMENT for proper ID generation
        auto create_result = conn.execute(
                "CREATE TABLE Person ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();
    }

    void TearDown() override {
        QuerySet<Person>::clear_default_connection();
    }
};

// Test: SELECT from empty table returns empty vector
TEST_F(SelectTest, SelectFromEmptyTable) {
    QuerySet<Person> queryset;

    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_TRUE(people.empty()) << "Expected empty result from empty table";
}

// Test: SELECT single row
TEST_F(SelectTest, SelectSingleRow) {
    QuerySet<Person> queryset;

    // Insert one person
    Person alice{0, "Alice", 30};
    auto   insert_result = queryset.insert(alice);
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    int64_t inserted_id = insert_result.value();

    // Select all rows
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected exactly one row";

    // Verify row contents
    EXPECT_EQ(people[0].id, inserted_id);
    EXPECT_EQ(people[0].name, "Alice");
    EXPECT_EQ(people[0].age, 30);
}

// Test: SELECT multiple rows
TEST_F(SelectTest, SelectMultipleRows) {
    QuerySet<Person> queryset;

    // Insert multiple people (use explicit IDs for batch insert)
    std::vector<Person> people_to_insert = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value())
            << "INSERT failed: code=" << insert_result.error().code() << " message=" << insert_result.error().message();

    const auto& inserted_ids = insert_result.value();
    ASSERT_EQ(inserted_ids.size(), 3) << "Expected 3 IDs";

    // Select all rows
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected exactly 3 rows";

    // Verify row contents
    EXPECT_EQ(people[0].id, inserted_ids[0]);
    EXPECT_EQ(people[0].name, "Alice");
    EXPECT_EQ(people[0].age, 30);

    EXPECT_EQ(people[1].id, inserted_ids[1]);
    EXPECT_EQ(people[1].name, "Bob");
    EXPECT_EQ(people[1].age, 25);

    EXPECT_EQ(people[2].id, inserted_ids[2]);
    EXPECT_EQ(people[2].name, "Charlie");
    EXPECT_EQ(people[2].age, 35);
}

// Test: SELECT with different field types (int and std::string)
TEST_F(SelectTest, SelectDifferentFieldTypes) {
    QuerySet<Person> queryset;

    // Insert person with specific values
    Person dave{0, "Dave", 40};
    auto   insert_result = queryset.insert(dave);
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    // Select and verify field types are correctly handled
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);

    // Verify int field
    EXPECT_EQ(people[0].age, 40);
    EXPECT_TRUE((std::is_same_v<decltype(people[0].age), int>));

    // Verify string field
    EXPECT_EQ(people[0].name, "Dave");
    EXPECT_TRUE((std::is_same_v<decltype(people[0].name), std::string>));
}

// Test: SELECT after INSERT and DELETE
TEST_F(SelectTest, SelectAfterInsertAndDelete) {
    QuerySet<Person> queryset;

    // Insert multiple people (use explicit IDs for batch insert)
    std::vector<Person> people_to_insert = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    const auto& inserted_ids = insert_result.value();

    // Delete Bob (middle row)
    Person bob_to_delete{static_cast<int>(inserted_ids[1]), "Bob", 25};
    auto   delete_result = queryset.remove(bob_to_delete);
    ASSERT_TRUE(delete_result.has_value()) << "DELETE failed: " << delete_result.error().message();

    // Select all rows
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 2) << "Expected 2 rows after deletion";

    // Verify remaining rows
    EXPECT_EQ(people[0].name, "Alice");
    EXPECT_EQ(people[1].name, "Charlie");
}

// Test: SELECT with large dataset
TEST_F(SelectTest, SelectLargeDataset) {
    QuerySet<Person> queryset;

    // Insert 100 people (use explicit IDs for batch insert)
    std::vector<Person> people_to_insert;
    for (int i = 1; i <= 100; ++i) {
        people_to_insert.push_back({i, "Person" + std::to_string(i), 20 + i});
    }

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    // Select all rows
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value()) << "SELECT failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 100) << "Expected 100 rows";

    // Verify a few rows
    EXPECT_EQ(people[0].name, "Person1");
    EXPECT_EQ(people[0].age, 21);

    EXPECT_EQ(people[50].name, "Person51");
    EXPECT_EQ(people[50].age, 71);

    EXPECT_EQ(people[99].name, "Person100");
    EXPECT_EQ(people[99].age, 120);
}

// Test: Multiple SELECT calls reuse cached statement
TEST_F(SelectTest, MultipleSelectCallsUseCaching) {
    QuerySet<Person> queryset;

    // Insert test data
    Person alice{0, "Alice", 30};
    auto   insert_result = queryset.insert(alice);
    ASSERT_TRUE(insert_result.has_value());

    // First SELECT
    auto result1 = queryset.select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 1);

    // Second SELECT (should use cached statement)
    auto result2 = queryset.select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 1);

    // Third SELECT
    auto result3 = queryset.select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 1);
}

// Test: SELECT with empty strings
TEST_F(SelectTest, SelectWithEmptyString) {
    QuerySet<Person> queryset;

    // Insert person with empty name
    Person anonymous{0, "", 25};
    auto   insert_result = queryset.insert(anonymous);
    ASSERT_TRUE(insert_result.has_value());

    // Select and verify
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    EXPECT_EQ(people[0].name, "");
    EXPECT_EQ(people[0].age, 25);
}

// Test: SELECT preserves row order
TEST_F(SelectTest, SelectPreservesRowOrder) {
    QuerySet<Person> queryset;

    // Insert in specific order (use explicit IDs for batch insert)
    std::vector<Person> people_to_insert =
            {{1, "First", 1}, {2, "Second", 2}, {3, "Third", 3}, {4, "Fourth", 4}, {5, "Fifth", 5}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // Select and verify order is preserved
    auto result = queryset.select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 5);

    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(people[i].age, static_cast<int>(i + 1)) << "Row order not preserved at index " << i;
    }
}