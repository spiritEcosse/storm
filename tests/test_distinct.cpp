#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;
import <algorithm>;
import <set>;

using namespace storm;

// Test model for DISTINCT operations
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Test fixture for DISTINCT operations
class DistinctTest : public ::testing::Test {
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

// Test: DISTINCT on name field with duplicates
TEST_F(DistinctTest, DistinctNameFieldWithDuplicates) {
    QuerySet<Person> queryset;

    // Insert people with duplicate names
    std::vector<Person> people_to_insert = {
            {1, "Alice", 30}, {2, "Bob", 25}, {3, "Alice", 35}, {4, "Charlie", 40}, {5, "Bob", 28}
    };

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    // SELECT DISTINCT name
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value()) << "SELECT DISTINCT failed: " << result.error().message();

    const auto& names = result.value();
    EXPECT_EQ(names.size(), 3) << "Expected 3 unique names";

    // Convert to set for easier verification
    std::set<std::string> unique_names(names.begin(), names.end());
    EXPECT_EQ(unique_names.count("Alice"), 1);
    EXPECT_EQ(unique_names.count("Bob"), 1);
    EXPECT_EQ(unique_names.count("Charlie"), 1);
}

// Test: DISTINCT on age field
TEST_F(DistinctTest, DistinctAgeFieldWithDuplicates) {
    QuerySet<Person> queryset;

    // Insert people with duplicate ages
    std::vector<Person> people_to_insert = {
            {1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 30}, {4, "Dave", 25}, {5, "Eve", 35}
    };

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT age
    auto result = queryset.distinct<&Person::age>().select();
    ASSERT_TRUE(result.has_value());

    const auto& ages = result.value();
    EXPECT_EQ(ages.size(), 3) << "Expected 3 unique ages";

    // Convert to set for verification
    std::set<int> unique_ages(ages.begin(), ages.end());
    EXPECT_EQ(unique_ages.count(25), 1);
    EXPECT_EQ(unique_ages.count(30), 1);
    EXPECT_EQ(unique_ages.count(35), 1);
}

// Test: DISTINCT on primary key (defaults to PK when no template arg)
TEST_F(DistinctTest, DistinctDefaultsToPrimaryKey) {
    QuerySet<Person> queryset;

    // Insert people
    std::vector<Person> people_to_insert = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT (defaults to PK)
    auto result = queryset.distinct().select();
    ASSERT_TRUE(result.has_value());

    const auto& ids = result.value();
    EXPECT_EQ(ids.size(), 3) << "Expected 3 unique IDs";

    // IDs should match what we inserted
    std::set<int> id_set(ids.begin(), ids.end());
    EXPECT_TRUE(id_set.count(1));
    EXPECT_TRUE(id_set.count(2));
    EXPECT_TRUE(id_set.count(3));
}

// Test: DISTINCT on name with all unique values
TEST_F(DistinctTest, DistinctNameFieldAllUnique) {
    QuerySet<Person> queryset;

    // Insert people with all unique names
    std::vector<Person> people_to_insert = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT name
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_EQ(names.size(), 3) << "Expected all 3 unique names";
}

// Test: DISTINCT on empty table
TEST_F(DistinctTest, DistinctFromEmptyTable) {
    QuerySet<Person> queryset;

    // SELECT DISTINCT name from empty table
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_TRUE(names.empty()) << "Expected empty result from empty table";
}

// Test: DISTINCT with single row
TEST_F(DistinctTest, DistinctWithSingleRow) {
    QuerySet<Person> queryset;

    // Insert one person
    Person alice{0, "Alice", 30};
    auto   insert_result = queryset.insert(alice);
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT name
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 1);
    EXPECT_EQ(names[0], "Alice");
}

// Test: DISTINCT with large dataset
TEST_F(DistinctTest, DistinctWithLargeDataset) {
    QuerySet<Person> queryset;

    // Insert 1000 people with 10 unique names (100 duplicates each)
    std::vector<Person> people_to_insert;
    for (int i = 1; i <= 1000; ++i) {
        int pattern = (i - 1) % 10;
        people_to_insert.push_back({i, "Name" + std::to_string(pattern), 20 + pattern});
    }

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT name
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_EQ(names.size(), 10) << "Expected 10 unique names";
}

// Test: DISTINCT with empty strings
TEST_F(DistinctTest, DistinctWithEmptyStrings) {
    QuerySet<Person> queryset;

    // Insert people with empty and non-empty names
    std::vector<Person> people_to_insert = {{1, "", 25}, {2, "", 30}, {3, "Alice", 25}, {4, "", 35}};

    auto insert_result = queryset.insert(std::span<const Person>(people_to_insert));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT DISTINCT name
    auto result = queryset.distinct<&Person::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_EQ(names.size(), 2) << "Expected 2 unique names (empty string and 'Alice')";

    std::set<std::string> name_set(names.begin(), names.end());
    EXPECT_TRUE(name_set.count(""));
    EXPECT_TRUE(name_set.count("Alice"));
}

// Test: Return type verification
TEST_F(DistinctTest, VerifyReturnTypes) {
    QuerySet<Person> queryset;

    // Insert test data
    queryset.insert(Person{0, "Alice", 30});

    // Verify distinct<&Person::name>() returns std::vector<std::string>
    auto names_result = queryset.distinct<&Person::name>().select();
    static_assert(std::is_same_v<decltype(names_result.value()), std::vector<std::string>&>);

    // Verify distinct<&Person::age>() returns std::vector<int>
    auto ages_result = queryset.distinct<&Person::age>().select();
    static_assert(std::is_same_v<decltype(ages_result.value()), std::vector<int>&>);

    // Verify distinct() (PK) returns std::vector<int>
    auto ids_result = queryset.distinct().select();
    static_assert(std::is_same_v<decltype(ids_result.value()), std::vector<int>&>);
}
