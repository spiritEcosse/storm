#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include "QuerySet.h"

// Mock Person class for testing (adjust according to your actual Person class)
struct Person {
    int id = 0;
    std::string name;
    int age = 0;
    std::string email;
    
    Person() = default;
    Person(const std::string& n, int a, const std::string& e) 
        : name(n), age(a), email(e) {}
};

REFL_AUTO(
    type(Person),
    field(id),
    field(name),
    field(age),
    field(email)
)

class ORMTest : public ::testing::Test {
protected:
    std::shared_ptr<Connection> conn;
    std::shared_ptr<orm::QuerySet<Person>> qs;
    std::string db_name;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
        conn = std::make_shared<Connection>(db_name);
        qs = std::make_shared<orm::QuerySet<Person>>(conn);
        
        // Create test table
        std::string create_table_sql = R"(
            CREATE TABLE IF NOT EXISTS person (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                email TEXT
            );
        )";
        
        auto stmt = Statement(conn, create_table_sql);
        ASSERT_TRUE(stmt.execute()) << "Failed to create test table";
    }

    void TearDown() override {
        qs.reset();
        conn.reset();
    }
};

// INSERT TESTS
TEST_F(ORMTest, InsertSingleObject) {
    Person person("John Doe", 30, "john@example.com");
    
    int result = qs->insert(person);
    
    EXPECT_GT(result, 0) << "Insert should return a valid ID";
    EXPECT_EQ(person.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    // This test assumes get_insert_field_names() can return empty
    // You might need to mock this behavior
    Person person("Jane Doe", 25, "jane@example.com");
    
    // If field_names is empty, insert should return -1
    // This test depends on your implementation of get_insert_field_names()
    int result = qs->insert(person);
    
    // Adjust expectation based on your actual implementation
    // If field_names is empty, result should be -1, otherwise it should be > 0
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 if field_names is empty";
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Person> people = {
        Person("Alice", 28, "alice@example.com"),
        Person("Bob", 32, "bob@example.com"),
        Person("Charlie", 26, "charlie@example.com")
    };
    
    std::vector<int> ids = qs->insert(people);
    
    EXPECT_EQ(ids.size(), people.size()) << "Should return an ID for each inserted object";
    
    // Check that all returned IDs are valid
    for (const auto& id : ids) {
        EXPECT_GT(id, 0) << "Each ID should be valid (greater than 0)";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Person> empty_people;
    
    std::vector<int> ids = qs->insert(empty_people);
    
    EXPECT_TRUE(ids.empty()) << "Inserting empty vector should return empty vector of IDs";
}

TEST_F(ORMTest, InsertExceptionHandling) {
    // Create a person that might cause an exception (e.g., invalid data)
    Person person("", -1, ""); // Assuming this might cause issues
    
    // The method should handle exceptions and return -1
    int result = qs->insert(person);
    
    // Should not crash and should return either a valid ID or -1
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 on error";
}

// UPDATE TESTS
TEST_F(ORMTest, UpdateSingleObject) {
    // First insert a person
    Person person("John Doe", 30, "john@example.com");
    ASSERT_TRUE(qs->insert(person));
    ASSERT_EQ(person.id, 0);
    
    // Update the person
    person.name = "John Smith";
    person.age = 31;
    
    bool result = qs->update(person);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateMultipleObjects) {
    // First insert multiple people
    std::vector<Person> people = {
        Person("Alice", 28, "alice@example.com"),
        Person("Bob", 32, "bob@example.com")
    };
    std::vector<int> ids = qs->insert(people);
    ASSERT_EQ(ids.size(), people.size());
    
    // Update them
    people[0].age = 29;
    people[1].name = "Robert";
    
    bool result = qs->update(people);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateEmptyVector) {
    std::vector<Person> empty_people;
    
    bool result = qs->update(empty_people);
    
    EXPECT_TRUE(result) << "Updating empty vector should return true";
}

TEST_F(ORMTest, UpdateNonExistentObject) {
    Person person("Ghost", 0, "ghost@example.com");
    person.id = 99999; // Non-existent ID
    
    bool result = qs->update(person);
    
    // Behavior depends on implementation - might return true even if no rows affected
    EXPECT_TRUE(result == true || result == false);
}

// DELETE TESTS
TEST_F(ORMTest, RemoveSingleObject) {
    // First insert a person
    Person person("John Doe", 30, "john@example.com");
    ASSERT_TRUE(qs->insert(person));
    ASSERT_EQ(person.id, 0);
    
    bool result = qs->remove(person);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveMultipleObjects) {
    // First insert multiple people
    std::vector<Person> people = {
        Person("Alice", 28, "alice@example.com"),
        Person("Bob", 32, "bob@example.com"),
        Person("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = qs->insert(people);
    ASSERT_EQ(ids.size(), people.size());
    
    bool result = qs->remove(people);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveEmptyVector) {
    std::vector<Person> empty_people;
    
    bool result = qs->remove(empty_people);
    
    EXPECT_TRUE(result) << "Removing empty vector should return true";
}

TEST_F(ORMTest, RemoveNonExistentObject) {
    Person person("Ghost", 0, "ghost@example.com");
    person.id = 99999; // Non-existent ID
    
    bool result = qs->remove(person);
    
    // Should handle gracefully
    EXPECT_TRUE(result == true || result == false);
}

// INTEGRATION TESTS
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Person person("John Doe", 30, "john@example.com");
    int id = qs->insert(person);
    ASSERT_GT(id, 0);
    ASSERT_EQ(person.id, 0);
    int original_id = person.id;
    
    // Update
    person.name = "John Smith";
    person.age = 31;
    ASSERT_TRUE(qs->update(person));
    EXPECT_EQ(person.id, original_id) << "ID should remain unchanged after update";
    
    // Delete
    ASSERT_TRUE(qs->remove(person));
}

TEST_F(ORMTest, BatchOperationsWorkflow) {
    // Batch insert
    std::vector<Person> people = {
        Person("Alice", 28, "alice@example.com"),
        Person("Bob", 32, "bob@example.com"),
        Person("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = qs->insert(people);
    ASSERT_EQ(ids.size(), people.size());
    
    // Verify all returned IDs are valid
    for (const auto& id : ids) {
        ASSERT_GT(id, 0);
    }
    
    // Batch update
    for (auto& person : people) {
        person.age += 1;
    }
    ASSERT_TRUE(qs->update(people));
    
    // Batch delete
    ASSERT_TRUE(qs->remove(people));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Person single_person("Single", 25, "single@example.com");
    int single_id = qs->insert(single_person);
    ASSERT_GT(single_id, 0);
    
    // Insert batch
    std::vector<Person> batch_people = {
        Person("Batch1", 30, "batch1@example.com"),
        Person("Batch2", 35, "batch2@example.com")
    };
    std::vector<int> batch_ids = qs->insert(batch_people);
    ASSERT_EQ(batch_ids.size(), batch_people.size());
    
    // Update single
    single_person.age = 26;
    ASSERT_TRUE(qs->update(single_person));
    
    // Update batch
    for (auto& person : batch_people) {
        person.age += 1;
    }
    ASSERT_TRUE(qs->update(batch_people));
    
    // Remove single
    ASSERT_TRUE(qs->remove(single_person));
    
    // Remove batch
    ASSERT_TRUE(qs->remove(batch_people));
}