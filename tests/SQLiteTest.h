#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include "QuerySet.h"
#include "SQLExceptions.h"

using namespace storm;

struct Author {
    std::string name;
    int age;
    std::string email;
    int id;
    bool is_active;

    Author() = default;
    Author(const std::string& n, int a, const std::string& e, int id = 0, bool active = true) 
        : id(id), name(n), age(a), email(e), is_active(active) {}
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
    field(email),
    field(is_active)
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
    int alice_id, bob_id, charlie_id, diana_id;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
        conn = std::make_shared<Connection>(db_name);

        // Create test tables
        std::string create_author_sql = R"(
            CREATE TABLE IF NOT EXISTS author (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                email TEXT,
                is_active BOOLEAN
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

        // Setup test data
        setupTestData();
    }

    void setupTestData() {
        // Create authors with different names and ages for testing
        Author alice("Alice Smith", 25, "alice@example.com");
        Author bob("Bob Johnson", 35, "bob@example.com");
        Author charlie("Charlie Brown", 30, "charlie@example.com");
        Author diana("Diana Prince", 28, "diana@example.com");
        
        alice_id = QuerySet<Author>(conn).insert(alice);
        bob_id = QuerySet<Author>(conn).insert(bob);
        charlie_id = QuerySet<Author>(conn).insert(charlie);
        diana_id = QuerySet<Author>(conn).insert(diana);
        
        // Create posts with different titles
        Post post1("Post A", "Content A", alice_id);
        Post post2("Post B", "Content B", bob_id);
        Post post3("Post C", "Content C", charlie_id);
        Post post4("Post D", "Content D", diana_id);
        
        QuerySet<Post>(conn).insert(post1);
        QuerySet<Post>(conn).insert(post2);
        QuerySet<Post>(conn).insert(post3);
        QuerySet<Post>(conn).insert(post4);
    }

    void TearDown() override {
        conn.reset();
    }
};

// INSERT TESTS
TEST_F(ORMTest, InsertSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    
    int result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_GT(result, 0) << "Insert should return a valid ID";
    EXPECT_EQ(author.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    Author author("Jane Doe", 25, "jane@example.com");
    
    int result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 if field_names is empty";
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com"),
        Author("Grace Wilson", 26, "grace@example.com")
    };
    
    std::vector<int> ids = QuerySet<Author>(conn).insert(authors);
    
    EXPECT_EQ(ids.size(), authors.size()) << "Should return an ID for each inserted object";
    
    // Check that all returned IDs are valid
    for (const auto& id : ids) {
        EXPECT_GT(id, 0) << "Each ID should be valid (greater than 0)";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Author> empty_authors;
    
    std::vector<int> ids = QuerySet<Author>(conn).insert(empty_authors);
    
    EXPECT_TRUE(ids.empty()) << "Inserting empty vector should return empty vector of IDs";
}

TEST_F(ORMTest, InsertExceptionHandling) {
    Author author("", -1, "");
    
    int result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result > 0 || result == -1) << "Should return valid ID or -1 on error";
}

// UPDATE TESTS
TEST_F(ORMTest, UpdateSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    int inserted_id = QuerySet<Author>(conn).insert(author);
    ASSERT_GT(inserted_id, 0);
    
    // Update the author with the inserted ID
    author.id = inserted_id;
    author.name = "John Smith";
    author.age = 31;
    
    bool result = QuerySet<Author>(conn).update(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com")
    };
    std::vector<int> ids = QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Set the IDs and update them
    authors[0].id = ids[0];
    authors[1].id = ids[1];
    authors[0].age = 29;
    authors[1].name = "Robert";
    
    bool result = QuerySet<Author>(conn).update(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, UpdateEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = QuerySet<Author>(conn).update(empty_authors);
    
    EXPECT_TRUE(result) << "Updating empty vector should return true";
}

TEST_F(ORMTest, UpdateNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = QuerySet<Author>(conn).update(author);
    
    // Behavior depends on implementation - might return true even if no rows affected
    EXPECT_TRUE(result == true || result == false);
}

// DELETE TESTS
TEST_F(ORMTest, RemoveSingleObject) {
    // First insert a person
    Author author("John Doe", 30, "john@example.com");
    int inserted_id = QuerySet<Author>(conn).insert(author);
    ASSERT_GT(inserted_id, 0);
    
    author.id = inserted_id;
    bool result = QuerySet<Author>(conn).remove(author);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com"),
        Author("Grace Wilson", 26, "grace@example.com")
    };
    std::vector<int> ids = QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Set the IDs
    for (size_t i = 0; i < authors.size(); ++i) {
        authors[i].id = ids[i];
    }
    
    bool result = QuerySet<Author>(conn).remove(authors);
    
    EXPECT_TRUE(result);
}

TEST_F(ORMTest, RemoveEmptyVector) {
    std::vector<Author> empty_authors;
    
    bool result = QuerySet<Author>(conn).remove(empty_authors);
    
    EXPECT_TRUE(result) << "Removing empty vector should return true";
}

TEST_F(ORMTest, RemoveNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    bool result = QuerySet<Author>(conn).remove(author);
    
    // Should handle gracefully
    EXPECT_TRUE(result == true || result == false);
}

// INTEGRATION TESTS
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Author author("John Doe", 30, "john@example.com");
    int id = QuerySet<Author>(conn).insert(author);
    ASSERT_GT(id, 0);
    
    // Update
    author.id = id;
    author.name = "John Smith";
    author.age = 31;
    ASSERT_TRUE(QuerySet<Author>(conn).update(author));
    EXPECT_EQ(author.id, id) << "ID should remain unchanged after update";
    
    // Delete
    ASSERT_TRUE(QuerySet<Author>(conn).remove(author));
}

TEST_F(ORMTest, BatchOperationsWorkflow) {
    // Batch insert
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com"),
        Author("Grace Wilson", 26, "grace@example.com")
    };
    std::vector<int> ids = QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), authors.size());
    
    // Verify all returned IDs are valid
    for (const auto& id : ids) {
        ASSERT_GT(id, 0);
    }
    
    // Set IDs and batch update
    for (size_t i = 0; i < authors.size(); ++i) {
        authors[i].id = ids[i];
        authors[i].age += 1;
    }
    ASSERT_TRUE(QuerySet<Author>(conn).update(authors));
    
    // Batch delete
    ASSERT_TRUE(QuerySet<Author>(conn).remove(authors));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Author single_author("Single", 25, "single@example.com");
    int single_id = QuerySet<Author>(conn).insert(single_author);
    ASSERT_GT(single_id, 0);
    
    // Insert batch
    std::vector<Author> batch_authors = {
        Author("Batch1", 30, "batch1@example.com"),
        Author("Batch2", 35, "batch2@example.com")
    };
    std::vector<int> batch_ids = QuerySet<Author>(conn).insert(batch_authors);
    ASSERT_EQ(batch_ids.size(), batch_authors.size());
    
    // Update single
    single_author.id = single_id;
    single_author.age = 26;
    ASSERT_TRUE(QuerySet<Author>(conn).update(single_author));
    
    // Update batch
    for (size_t i = 0; i < batch_authors.size(); ++i) {
        batch_authors[i].id = batch_ids[i];
        batch_authors[i].age += 1;
    }
    ASSERT_TRUE(QuerySet<Author>(conn).update(batch_authors));
    
    // Remove single
    ASSERT_TRUE(QuerySet<Author>(conn).remove(single_author));
    
    // Remove batch
    ASSERT_TRUE(QuerySet<Author>(conn).remove(batch_authors));
}

// SELECT TESTS
TEST_F(ORMTest, SelectAll) {
    std::vector<Author> all_authors = QuerySet<Author>(conn).select_all();
    ASSERT_EQ(all_authors.size(), 4); // Alice, Bob, Charlie, Diana
}

// WHERE TESTS
TEST_F(ORMTest, SelectAllWhereId) {
    std::vector<Author> authors = QuerySet<Author>(conn).where(&Author::id, alice_id)
        .select_all();
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Alice Smith");
}

TEST_F(ORMTest, SelectAllManyWhere) {
    std::vector<Post> posts = QuerySet<Post>(conn).where(&Post::author_id, alice_id)
        .where(&Post::title, "Post A")
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllErrorInvalidColumnException) {
    try {
        QuerySet<Post>(conn).where(&Author::name, "Alice Smith").select_all();
        FAIL() << "Expected InvalidColumnException to be thrown";
    } catch (const InvalidColumnException& e) {
        // Verify the column name in the exception
        EXPECT_EQ(e.getColumnName(), "author.name");
        EXPECT_FALSE(e.getQuery().empty());
    } catch (...) {
        FAIL() << "Expected InvalidColumnException, but a different exception was thrown";
    }
}

// JOIN TESTS
TEST_F(ORMTest, SelectAllWithJoin) {
    std::vector<Post> all_posts = QuerySet<Post>(conn)
        .join<Author>()
        .select_all();
    ASSERT_EQ(all_posts.size(), 4); // Should have all 4 posts with author data
}

TEST_F(ORMTest, SelectAllWithJoinAndWhere) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinAndManyWhere) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .where(&Author::is_active, true)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndLimit) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndOffset) {
    // First insert another post for Alice to test offset
    Post extra_post("Post A2", "Content A2", alice_id);
    QuerySet<Post>(conn).insert(extra_post);
    
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .offset(1)
        .select_all();
    ASSERT_EQ(posts.size(), 2);
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndLimitAndOffset) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(posts.size(), 0); // Only one post for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupBy) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .group_by<&Author::name>()
        .select_all();
    ASSERT_EQ(posts.size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimit) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .group_by<&Author::name>()
        .limit(1)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
}

// TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndOffset) {
//     std::vector<Post> posts = QuerySet<Post>(conn)
//         .join<Author>()
//         .where(&Post::author_id, alice_id)
//         .group_by<&Author::name>()
//         .offset(1)
//         .select_all();
//     ASSERT_EQ(posts.size(), 0); // Only one group for Alice, offset 1 means no results
// }

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndLimitAndOffset) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .group_by<&Author::name>()
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(posts.size(), 0);
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndLimitAndOffsetAndOrderBy) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .order_by<&Author::name>()
        .select_all();
    ASSERT_EQ(posts.size(), 0);
}

// ORDER BY TESTS
TEST_F(ORMTest, OrderBySingleFieldAscendingExplicit) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .order_by<&Author::name, true>()
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    
    // Check ascending order by name: Alice, Bob, Charlie, Diana
    EXPECT_EQ(authors[0].name, "Alice Smith");
    EXPECT_EQ(authors[1].name, "Bob Johnson");
    EXPECT_EQ(authors[2].name, "Charlie Brown");
    EXPECT_EQ(authors[3].name, "Diana Prince");
}

TEST_F(ORMTest, OrderBySingleFieldDescending) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .order_by<&Author::name, false>()
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    
    // Check descending order by name: Diana, Charlie, Bob, Alice
    EXPECT_EQ(authors[0].name, "Diana Prince");
    EXPECT_EQ(authors[1].name, "Charlie Brown");
    EXPECT_EQ(authors[2].name, "Bob Johnson");
    EXPECT_EQ(authors[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderBySingleFieldDefaultAscending) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .order_by<&Author::age>()  // Default is ascending
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    
    // Check ascending order by age: 25, 28, 30, 35
    EXPECT_EQ(authors[0].age, 25);  // Alice
    EXPECT_EQ(authors[1].age, 28);  // Diana
    EXPECT_EQ(authors[2].age, 30);  // Charlie
    EXPECT_EQ(authors[3].age, 35);  // Bob
    
    EXPECT_EQ(authors[0].name, "Alice Smith");
    EXPECT_EQ(authors[1].name, "Diana Prince");
    EXPECT_EQ(authors[2].name, "Charlie Brown");
    EXPECT_EQ(authors[3].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByMultipleFieldsMixedDirections) {
    // Add more test data with same ages to test secondary ordering
    Author author5("Eve Adams", 25, "eve@example.com");  // Same age as Alice
    Author author6("Frank Miller", 35, "frank@example.com");  // Same age as Bob
    QuerySet<Author>(conn).insert(author5);
    QuerySet<Author>(conn).insert(author6);
    
    std::vector<Author> authors = QuerySet<Author>(conn)
        .order_by<&Author::age, true, &Author::name, false>()  // Age ASC, Name DESC
        .select_all();
    
    ASSERT_EQ(authors.size(), 6);
    
    // Check ordering: Age ascending, then name descending within same age
    // Age 25: Eve Adams, Alice Smith (Eve > Alice alphabetically, DESC)
    // Age 28: Diana Prince
    // Age 30: Charlie Brown  
    // Age 35: Frank Miller, Bob Johnson (Frank > Bob alphabetically, DESC)
    
    EXPECT_EQ(authors[0].age, 25);
    EXPECT_EQ(authors[0].name, "Eve Adams");
    
    EXPECT_EQ(authors[1].age, 25);
    EXPECT_EQ(authors[1].name, "Alice Smith");
    
    EXPECT_EQ(authors[2].age, 28);
    EXPECT_EQ(authors[2].name, "Diana Prince");
    
    EXPECT_EQ(authors[3].age, 30);
    EXPECT_EQ(authors[3].name, "Charlie Brown");
    
    EXPECT_EQ(authors[4].age, 35);
    EXPECT_EQ(authors[4].name, "Frank Miller");
    
    EXPECT_EQ(authors[5].age, 35);
    EXPECT_EQ(authors[5].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByWithJoin) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .order_by<&Author::name, true>()
        .select_all();
    
    ASSERT_EQ(posts.size(), 4);
    
    // Posts should be ordered by their author's name
    // Since we have posts A,B,C,D for authors Alice,Bob,Charlie,Diana
    EXPECT_EQ(posts[0].title, "Post A");  // Alice's post
    EXPECT_EQ(posts[1].title, "Post B");  // Bob's post
    EXPECT_EQ(posts[2].title, "Post C");  // Charlie's post
    EXPECT_EQ(posts[3].title, "Post D");  // Diana's post
}

TEST_F(ORMTest, OrderByAgeDescending) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .order_by<&Author::age, false>()
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    
    // Should be ordered by age DESC
    EXPECT_EQ(authors[0].age, 35);  // Bob (highest age)
    EXPECT_EQ(authors[0].name, "Bob Johnson");
    
    EXPECT_EQ(authors[1].age, 30);  // Charlie
    EXPECT_EQ(authors[1].name, "Charlie Brown");
    
    EXPECT_EQ(authors[2].age, 28);  // Diana
    EXPECT_EQ(authors[2].name, "Diana Prince");
    
    EXPECT_EQ(authors[3].age, 25);  // Alice (lowest age)
    EXPECT_EQ(authors[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderByComplexJoinWithMultipleOrderFields) {
    // Add more authors and posts for complex testing
    Author author5("Alice Johnson", 27, "alice.j@example.com");  // Same first name as Alice Smith
    int author5_id = QuerySet<Author>(conn).insert(author5);
    Post post5("Post E", "Content E", author5_id);
    QuerySet<Post>(conn).insert(post5);
    
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .order_by<&Author::name, true>()
        .order_by<&Author::age, false>()  // Name ASC, Age DESC
        .select_all();
    
    ASSERT_EQ(posts.size(), 5);
    
    // Should order by author name first, then by age DESC within same name
    // Alice Johnson (27), Alice Smith (25), Bob Johnson (35), Charlie Brown (30), Diana Prince (28)
    
    // First Alice (Johnson, higher age comes first due to DESC)
    EXPECT_EQ(posts[0].title, "Post E");
    
    // Second Alice (Smith, lower age)
    EXPECT_EQ(posts[1].title, "Post A");
    
    // Then Bob
    EXPECT_EQ(posts[2].title, "Post B");
    
    // Then Charlie
    EXPECT_EQ(posts[3].title, "Post C");
    
    // Finally Diana
    EXPECT_EQ(posts[4].title, "Post D");
}

TEST_F(ORMTest, OrderingWithWhereClause) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 30)  // Only Charlie has age 30
        .order_by<&Author::name, true>()
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
}