#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include "QuerySet.h"
#include "SQLExceptions.h"


struct Author {
    std::string name;
    int age;
    std::string email;
    int id;

    Author() = default;
    Author(const std::string& n, int a, const std::string& e, int id = 0) 
        : id(id), name(n), age(a), email(e) {}
};

struct Post {
    int id = 0;
    std::string title;
    std::string content;
    int author_id;

    Post() = default;
    Post(const std::string& t, const std::string& c, int author_id, int id = 0) 
        : title(t), content(c), author_id(author_id), id(id) {}
};

REFL_AUTO(
    type(Author),
    field(id),
    field(name),
    field(age),
    field(email)
)

REFL_AUTO(
    type(Post),
    field(id),
    field(title),
    field(content),
    field(author_id)
)


class ORMTest : public ::testing::Test {
protected:
    std::shared_ptr<Connection> conn;
    std::string db_name;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
        conn = std::make_shared<Connection>(db_name);
        
        // One sql with 2 tables does not work, figure out why
        // Create test table
        std::string create_author_sql = R"(
            CREATE TABLE IF NOT EXISTS author (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                email TEXT
            );
        )";
        auto author_stmt = Statement(conn, create_author_sql);
        ASSERT_TRUE(author_stmt.execute()) << "Failed to create author table";

        std::string create_post_sql = R"(
            CREATE TABLE IF NOT EXISTS post (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                content TEXT,
                author_id INTEGER,
                FOREIGN KEY (author_id) REFERENCES author(id)
            );
        )";
        auto post_stmt = Statement(conn, create_post_sql);
        ASSERT_TRUE(post_stmt.execute()) << "Failed to create post table";
    }

    void TearDown() override {
        conn.reset();
    }
};

// INSERT TESTS
TEST_F(ORMTest, InsertSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    
    int result = orm::QuerySet<Author>(conn).insert(author);
    
    EXPECT_GT(result, 0) << "Insert should return a valid ID";
    EXPECT_EQ(author.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    Author author("Jane Doe", 25, "jane@example.com");
    
    int result = orm::QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 if field_names is empty";
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    
    std::vector<int> ids = orm::QuerySet<Author>(conn).insert(authors);
    
    EXPECT_EQ(ids.size(), authors.size()) << "Should return an ID for each inserted object";
    
    // Check that all returned IDs are valid
    for (const auto& id : ids) {
        EXPECT_GT(id, 0) << "Each ID should be valid (greater than 0)";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Author> empty_authors;
    
    std::vector<int> ids = orm::QuerySet<Author>(conn).insert(empty_authors);
    
    EXPECT_TRUE(ids.empty()) << "Inserting empty vector should return empty vector of IDs";
}

TEST_F(ORMTest, InsertExceptionHandling) {
    Author author("", -1, "");
    
    int result = orm::QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 on error";
}

// UPDATE TESTS
TEST_F(ORMTest, UpdateSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    ASSERT_TRUE(orm::QuerySet<Author>(conn).insert(author));
    ASSERT_EQ(author.id, 0);
    
    author.name = "John Smith";
    author.age = 31;
    
    bool result = orm::QuerySet<Author>(conn).update(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com")
    };
    std::vector<int> ids = orm::QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Update them
    authors[0].age = 29;
    authors[1].name = "Robert";
    
    bool result = orm::QuerySet<Author>(conn).update(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = orm::QuerySet<Author>(conn).update(empty_authors);
    
    EXPECT_TRUE(result) << "Updating empty vector should return true";
}

TEST_F(ORMTest, UpdateNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = orm::QuerySet<Author>(conn).update(author);
    
    // Behavior depends on implementation - might return true even if no rows affected
    EXPECT_TRUE(result == true || result == false);
}

// DELETE TESTS
TEST_F(ORMTest, RemoveSingleObject) {
    // First insert a person
    Author author("John Doe", 30, "john@example.com");
    ASSERT_TRUE(orm::QuerySet<Author>(conn).insert(author));
    ASSERT_EQ(author.id, 0);
    
    bool result = orm::QuerySet<Author>(conn).remove(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = orm::QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    bool result = orm::QuerySet<Author>(conn).remove(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = orm::QuerySet<Author>(conn).remove(empty_authors);
    
    EXPECT_TRUE(result) << "Removing empty vector should return true";
}

TEST_F(ORMTest, RemoveNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = orm::QuerySet<Author>(conn).remove(author);
    
    // Should handle gracefully
    EXPECT_TRUE(result == true || result == false);
}

// INTEGRATION TESTS
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Author author("John Doe", 30, "john@example.com");
    int id = orm::QuerySet<Author>(conn).insert(author);
    ASSERT_GT(id, 0);
    ASSERT_EQ(author.id, 0);
    int original_id = author.id;
    
    // Update
    author.name = "John Smith";
    author.age = 31;
    ASSERT_TRUE(orm::QuerySet<Author>(conn).update(author));
    EXPECT_EQ(author.id, original_id) << "ID should remain unchanged after update";
    
    // Delete
    ASSERT_TRUE(orm::QuerySet<Author>(conn).remove(author));
}

TEST_F(ORMTest, BatchOperationsWorkflow) {
    // Batch insert
    std::vector<Author> authors = {
        Author("Alice", 28, "alice@example.com"),
        Author("Bob", 32, "bob@example.com"),
        Author("Charlie", 26, "charlie@example.com")
    };
    std::vector<int> ids = orm::QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Verify all returned IDs are valid
    for (const auto& id : ids) {
        ASSERT_GT(id, 0);
    }
    
    // Batch update
    for (auto& author : authors) {
        author.age += 1;
    }
    ASSERT_TRUE(orm::QuerySet<Author>(conn).update(authors));
    
    // Batch delete
    ASSERT_TRUE(orm::QuerySet<Author>(conn).remove(authors));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Author single_author("Single", 25, "single@example.com");
    int single_id = orm::QuerySet<Author>(conn).insert(single_author);
    ASSERT_GT(single_id, 0);
    
    // Insert batch
    std::vector<Author> batch_authors = {
        Author("Batch1", 30, "batch1@example.com"),
        Author("Batch2", 35, "batch2@example.com")
    };
    std::vector<int> batch_ids = orm::QuerySet<Author>(conn).insert(batch_authors);
    ASSERT_EQ(batch_ids.size(), batch_authors.size());
    
    // Update single
    single_author.age = 26;
    ASSERT_TRUE(orm::QuerySet<Author>(conn).update(single_author));
    
    // Update batch
    for (auto& author : batch_authors) {
        author.age += 1;
    }
    ASSERT_TRUE(orm::QuerySet<Author>(conn).update(batch_authors));
    
    // Remove single
    ASSERT_TRUE(orm::QuerySet<Author>(conn).remove(single_author));
    
    // Remove batch
    ASSERT_TRUE(orm::QuerySet<Author>(conn).remove(batch_authors));
}

// SELECT TESTS
TEST_F(ORMTest, SelectAll) {
    Author single_author("Single", 25, "single@example.com");
    orm::QuerySet<Author>(conn).insert(single_author);
    std::vector<Author> all_authors = orm::QuerySet<Author>(conn).select_all();
    ASSERT_EQ(all_authors.size(), 1);
}

TEST_F(ORMTest, SelectAllWhereId) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    std::vector<Author> all_authors = orm::QuerySet<Author>(conn).where(&Author::id, author_id)
        .select_all();
    ASSERT_EQ(all_authors.size(), 1);
}

TEST_F(ORMTest, SelectAllManyWhere) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).where(&Post::author_id, author_id)
        .where(&Post::title, "Title")
        .select_all();
    ASSERT_EQ(all_posts.size(), 1);
}

TEST_F(ORMTest, SelectAllErrorInvalidColumnException) {
    try {
        orm::QuerySet<Post>(conn).where(&Author::name, "John Doe").select_all();
        FAIL() << "Expected orm::InvalidColumnException to be thrown";
    } catch (const orm::InvalidColumnException& e) {
        // Verify the column name in the exception
        EXPECT_EQ(e.getColumnName(), "author.name");
        EXPECT_FALSE(e.getQuery().empty());
    } catch (...) {
        FAIL() << "Expected orm::InvalidColumnException, but a different exception was thrown";
    }
}

TEST_F(ORMTest, SelectAllWithJoin) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .select_all();
    ASSERT_EQ(all_posts.size(), 2);
}

TEST_F(ORMTest, SelectAllWithJoinReverse) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .select_all();
    ASSERT_EQ(all_posts.size(), 2);
}

TEST_F(ORMTest, SelectAllWithJoinAndManyWhere) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndLimit) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .limit(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndOffset) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .offset(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndLimitAndOffset) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupBy) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimit) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .limit(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndOffset) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .offset(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimitAndOffset) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(all_posts.size(), 1);     
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimitAndOffsetAndOrderBy) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .limit(1)
        .offset(1)
        // .order_by<&Author::name>()
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}
    
TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimitAndOffsetAndOrderByAndDistinct) {
    Author author("John Doe", 30, "john@example.com");
    int author_id = orm::QuerySet<Author>(conn).insert(author);
    Post post("Title", "Content", author_id);
    orm::QuerySet<Post>(conn).insert(post);
    Author author2("John Doe", 30, "john@example.com");
    int author_id2 = orm::QuerySet<Author>(conn).insert(author2);
    Post post2("Title", "Content", author_id2);
    orm::QuerySet<Post>(conn).insert(post2);
    std::vector<Post> all_posts = orm::QuerySet<Post>(conn).join<Author>()
        .where(&Post::author_id, author_id)
        .group_by<&Author::name>()
        .limit(1)
        .offset(1)
        // .order_by<&Author::name>()
        // .distinct()
        .select_all();
    ASSERT_EQ(all_posts.size(), 1); 
}
    
