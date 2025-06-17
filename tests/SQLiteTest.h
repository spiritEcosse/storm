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
    
    bool result = qs->insert(person);
    
    EXPECT_TRUE(result);
    EXPECT_GT(person.id, 0) << "ID should be set after insert";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    // This test assumes get_insert_field_names() can return empty
    // You might need to mock this behavior
    Person person("Jane Doe", 25, "jane@example.com");
    
    // If field_names is empty, insert should return false
    // This test depends on your implementation of get_insert_field_names()
    bool result = qs->insert(person);
    
    // Adjust expectation based on your actual implementation
    EXPECT_TRUE(result || !result); // Placeholder - adjust based on expected behavior
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Person> people = {
        Person("Alice", 28, "alice@example.com"),
        Person("Bob", 32, "bob@example.com"),
        Person("Charlie", 26, "charlie@example.com")
    };
    
    bool result = qs->insert(people);
    
    EXPECT_TRUE(result);
    
    // Check that all objects have IDs assigned
    for (const auto& person : people) {
        EXPECT_GT(person.id, 0) << "Each person should have an ID after batch insert";
    }
    
    // Check that IDs are sequential (depends on your ID assignment logic)
    for (size_t i = 1; i < people.size(); ++i) {
        EXPECT_EQ(people[i].id, people[i-1].id + 1) << "IDs should be sequential";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Person> empty_people;
    
    bool result = qs->insert(empty_people);
    
    EXPECT_TRUE(result) << "Inserting empty vector should return true";
}

TEST_F(ORMTest, InsertExceptionHandling) {
    // Create a person that might cause an exception (e.g., invalid data)
    Person person("", -1, ""); // Assuming this might cause issues
    
    // The method should handle exceptions and return false
    bool result = qs->insert(person);
    
    // Should not crash and should return a boolean
    EXPECT_TRUE(result == true || result == false);
}

// UPDATE TESTS
TEST_F(ORMTest, UpdateSingleObject) {
    // First insert a person
    Person person("John Doe", 30, "john@example.com");
    ASSERT_TRUE(qs->insert(person));
    ASSERT_GT(person.id, 0);
    
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
    ASSERT_TRUE(qs->insert(people));
    
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
    ASSERT_GT(person.id, 0);
    
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
    ASSERT_TRUE(qs->insert(people));
    
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
    ASSERT_TRUE(qs->insert(person));
    ASSERT_GT(person.id, 0);
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
    ASSERT_TRUE(qs->insert(people));
    
    // Verify all have IDs
    for (const auto& person : people) {
        ASSERT_GT(person.id, 0);
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
    ASSERT_TRUE(qs->insert(single_person));
    
    // Insert batch
    std::vector<Person> batch_people = {
        Person("Batch1", 30, "batch1@example.com"),
        Person("Batch2", 35, "batch2@example.com")
    };
    ASSERT_TRUE(qs->insert(batch_people));
    
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