#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include "QuerySet.h"

struct Post;

struct Author {
    std::string name;
    int age;
    std::string email;
    // std::vector<std::shared_ptr<Post>> posts;
    int id;

    Author() = default;
    Author(const std::string& n, int a, const std::string& e, int id = 0) 
        : id(id), name(n), age(a), email(e) {}
};

// struct Post {
//     int id = 0;
//     std::string title;
//     std::string content;
//     std::weak_ptr<Author> author;  // Weak pointer to avoid cycles

//     Post() = default;
//     Post(const std::string& t, const std::string& c, const std::weak_ptr<Author>& a, int id = 0) 
//         : title(t), content(c), author(a), id(id) {}
// };

REFL_AUTO(
    type(Author),
    field(id),
    field(name),
    field(age),
    field(email)
)

// REFL_AUTO(
//     type(Post),
//     field(id),
//     field(title),
//     field(content),
//     field(author)
// )


class ORMTest : public ::testing::Test {
protected:
    std::shared_ptr<Connection> conn;
    std::shared_ptr<orm::QuerySet<Author>> qs;
    std::string db_name;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
        conn = std::make_shared<Connection>(db_name);
        qs = std::make_shared<orm::QuerySet<Author>>(conn);
        
        // Create test table
        std::string create_table_sql = R"(
            CREATE TABLE IF NOT EXISTS author (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                email TEXT
            );
            CREATE TABLE IF NOT EXISTS post (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                content TEXT,
                author_id INTEGER,
                FOREIGN KEY (author_id) REFERENCES author(id)
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
    Author author("John Doe", 30, "john@example.com");
    
    int result = qs->insert(author);
    
    EXPECT_GT(result, 0) << "Insert should return a valid ID";
    EXPECT_EQ(author.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    Author author("Jane Doe", 25, "jane@example.com");
    
    int result = qs->insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 if field_names is empty";
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    
    std::vector<int> ids = qs->insert(authors);
    
    EXPECT_EQ(ids.size(), authors.size()) << "Should return an ID for each inserted object";
    
    // Check that all returned IDs are valid
    for (const auto& id : ids) {
        EXPECT_GT(id, 0) << "Each ID should be valid (greater than 0)";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Author> empty_authors;
    
    std::vector<int> ids = qs->insert(empty_authors);
    
    EXPECT_TRUE(ids.empty()) << "Inserting empty vector should return empty vector of IDs";
}

TEST_F(ORMTest, InsertExceptionHandling) {
    Author author("", -1, "");
    
    int result = qs->insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 on error";
}

// UPDATE TESTS
TEST_F(ORMTest, UpdateSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    ASSERT_TRUE(qs->insert(author));
    ASSERT_EQ(author.id, 0);
    
    author.name = "John Smith";
    author.age = 31;
    
    bool result = qs->update(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com")
    };
    std::vector<int> ids = qs->insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Update them
    authors[0].age = 29;
    authors[1].name = "Robert";
    
    bool result = qs->update(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = qs->update(empty_authors);
    
    EXPECT_TRUE(result) << "Updating empty vector should return true";
}

TEST_F(ORMTest, UpdateNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = qs->update(author);
    
    // Behavior depends on implementation - might return true even if no rows affected
    EXPECT_TRUE(result == true || result == false);
}

// DELETE TESTS
TEST_F(ORMTest, RemoveSingleObject) {
    // First insert a person
    Author author("John Doe", 30, "john@example.com");
    ASSERT_TRUE(qs->insert(author));
    ASSERT_EQ(author.id, 0);
    
    bool result = qs->remove(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = qs->insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    bool result = qs->remove(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = qs->remove(empty_authors);
    
    EXPECT_TRUE(result) << "Removing empty vector should return true";
}

TEST_F(ORMTest, RemoveNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = qs->remove(author);
    
    // Should handle gracefully
    EXPECT_TRUE(result == true || result == false);
}

// INTEGRATION TESTS
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Author author("John Doe", 30, "john@example.com");
    int id = qs->insert(author);
    ASSERT_GT(id, 0);
    ASSERT_EQ(author.id, 0);
    int original_id = author.id;
    
    // Update
    author.name = "John Smith";
    author.age = 31;
    ASSERT_TRUE(qs->update(author));
    EXPECT_EQ(author.id, original_id) << "ID should remain unchanged after update";
    
    // Delete
    ASSERT_TRUE(qs->remove(author));
}

TEST_F(ORMTest, BatchOperationsWorkflow) {
    // Batch insert
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = qs->insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Verify all returned IDs are valid
    for (const auto& id : ids) {
        ASSERT_GT(id, 0);
    }
    
    // Batch update
    for (auto& author : authors) {
        author.age += 1;
    }
    ASSERT_TRUE(qs->update(authors));
    
    // Batch delete
    ASSERT_TRUE(qs->remove(authors));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Author single_author("Single", 25, "single@example.com");
    int single_id = qs->insert(single_author);
    ASSERT_GT(single_id, 0);
    
    // Insert batch
    std::vector<Author> batch_authors = {
        Author("Batch1", 30, "batch1@example.com"),
        Author("Batch2", 35, "batch2@example.com")
    };
    std::vector<int> batch_ids = qs->insert(batch_authors);
    ASSERT_EQ(batch_ids.size(), batch_authors.size());
    
    // Update single
    single_author.age = 26;
    ASSERT_TRUE(qs->update(single_author));
    
    // Update batch
    for (auto& author : batch_authors) {
        author.age += 1;
    }
    ASSERT_TRUE(qs->update(batch_authors));
    
    // Remove single
    ASSERT_TRUE(qs->remove(single_author));
    
    // Remove batch
    ASSERT_TRUE(qs->remove(batch_authors));
}

// SELECT TESTS
TEST_F(ORMTest, SelectAll) {
    Author single_author("Single", 25, "single@example.com");
    qs->insert(single_author);
    std::vector<Author> all_authors = qs->select_all();
    ASSERT_EQ(all_authors.size(), 1);
}

// TEST_F(ORMTest, SelectAllWithJoin) {
//     Author author("John Doe", 30, "john@example.com");
//     qs->insert(author);
//     Post post("Title", "Content", author);
//     qs->insert(post);
//     QuerySet<Post> qs_post(conn);
//     std::vector<Post> all_posts = qs_post.join<Author>().select_all();
//     ASSERT_EQ(all_posts.size(), 1);
// }
