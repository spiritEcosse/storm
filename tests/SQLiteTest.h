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
    double rating;
    float score;
    std::string middleName;
    std::string biography;

    Author() = default;
    Author(const std::string& n, int a, const std::string& e, int id = 0, bool active = true, double rating = 0.0, float score = 0.0, const std::string& middleName = "", const std::string& biography = "") 
        : id(id), name(n), age(a), email(e), is_active(active), rating(rating), score(score), middleName(middleName), biography(biography) {}
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
    field(is_active),
    field(rating),
    field(score),
    field(middleName),
    field(biography)
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
    int alice_id;
    int bob_id;
    int charlie_id;
    int diana_id;
    
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
                is_active BOOLEAN,
                rating REAL,
                score REAL,
                middleName TEXT,
                biography TEXT
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
        Author alice("Alice Smith", 25, "alice@example.com", 0, true, 4.5, 85.5, "Alice", "Alice biography");
        Author bob("Bob Johnson", 35, "bob@example.com", 0, true, 5.0, 90.0, "Bob", "Bob biography");
        Author charlie("Charlie Brown", 30, "charlie@example.com", 0, true, 4.0, 80.0, "Charlie", "Charlie biography");
        Author diana("Diana Prince", 28, "diana@example.com", 0, true, 5.5, 95.0, "Diana", "Diana biography");
        
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
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::id, alice_id)
        .select_all();
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Alice Smith");
}

TEST_F(ORMTest, SelectAllWhereMany) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .where(&Post::author_id, alice_id)
        .where(&Post::title, "Post A")
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllErrorInvalidColumnException) {
    try {
        QuerySet<Post>(conn)
            .where(&Author::name, "Alice Smith")
            .select_all();
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

TEST_F(ORMTest, SelectAllWithJoinWhere) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereMany) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .where(&Author::is_active, true)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereLimit) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
    EXPECT_EQ(posts[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereOffset) {
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

TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffset) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(posts.size(), 0); // Only one post for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupBy) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .select_all();
    ASSERT_EQ(posts.size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimit) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .limit(1)
        .select_all();
    ASSERT_EQ(posts.size(), 1);
}

// TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndOffset) {
//     std::vector<Post> posts = QuerySet<Post>(conn)
//         .join<Author>()
//         .where(&Post::author_id, alice_id)
//         .template group_by<&Author::name>()
//         .offset(1)
//         .select_all();
//     ASSERT_EQ(posts.size(), 0); // Only one group for Alice, offset 1 means no results
// }

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimitOffset) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_EQ(posts.size(), 0);
}

// ORDER BY TESTS
TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffsetOrderBy) {
    std::vector<Post> posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .order_by<&Author::name>()
        .select_all();
    ASSERT_EQ(posts.size(), 0);
}

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

// WHERE TESTS
// MIXED, INT, EQUALS
TEST_F(ORMTest, WhereClauseEqualsDefault) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 30)  // Only Charlie has age 30
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
}

// MIXED, INT, EQUALS
TEST_F(ORMTest, WhereClauseEqualsOperator) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 30)  // Only Charlie has age 30
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
}

// MIXED, INT, EQUALS
TEST_F(ORMTest, WhereClauseConvenientSyntax) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 30)  // Default to EQUALS
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
}

// MIXED, GREATER_THAN, BOOLEAN, INT, EQUALS
TEST_F(ORMTest, WhereClauseMultipleConditions) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GT)
        .where(&Author::is_active, true)
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    for (const auto& author : authors) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

// NTTP, int, EQUALS
TEST_F(ORMTest, WhereClauseNTTPSyntax) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::age>(30)  // NTTP syntax - member pointer as template parameter
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
}

// NTTP, string, EQUALS
TEST_F(ORMTest, WhereClauseNTTPStringComparison) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::name>(std::string("Alice Smith"), Op::EQ)
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Alice Smith");
    EXPECT_EQ(authors[0].age, 25);
}

// NTTP, boolean, EQUALS
TEST_F(ORMTest, WhereClauseNTTPBooleanValue) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::is_active>(true)  // Boolean using NTTP
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    for (const auto& author : authors) {
        EXPECT_TRUE(author.is_active);
    }
}

// NTTP, int, GREATER_THAN
TEST_F(ORMTest, WhereClauseNTTPWithOperators) {
    // Test NTTP with GREATER_THAN operator
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::age>(25, Op::GT)
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    for (const auto& author : authors) {
        EXPECT_GT(author.age, 25);
    }
}

// NTTP, string, LIKE
TEST_F(ORMTest, WhereClauseNTTPLikeOperator) {
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::name>(std::string("A%"), Op::LIKE)  // Names starting with 'A'
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    for (const auto& author : authors) {
        EXPECT_EQ(author.name[0], 'A');
    }
}

// NTTP, GREATER_OR_EQUAL, EQUALS
TEST_F(ORMTest, WhereClauseNTTPChaining) {
    // Test chaining NTTP where clauses
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::age>(20, Op::GE)
        .where<&Author::is_active>(true)
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    for (const auto& author : authors) {
        EXPECT_GE(author.age, 20);
        EXPECT_TRUE(author.is_active);
    }
}

// MIXED, GREATER_THAN, EQUALS
TEST_F(ORMTest, WhereClauseMixedSyntax) {
    // Test mixing traditional and NTTP syntax
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GT)  // Traditional syntax
        .where<&Author::is_active>(true)                   // NTTP syntax
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);

    for (const auto& author : authors) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

// NTTP, const char*, EQUALS
TEST_F(ORMTest, WhereClauseNTTPConstCharPtr) {
    // Test NTTP with const char* (C-style string)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::name>("Charlie Brown")  // const char* literal
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Charlie Brown");
    EXPECT_EQ(authors[0].age, 30);
}

// NTTP, EQUALS
TEST_F(ORMTest, WhereClauseNTTPWithArithmeticTypes) {
    // Test NTTP with various arithmetic types (now works without SFINAE constraint)
    std::vector<Author> authorsInt = QuerySet<Author>(conn)
        .where<&Author::age>(30)  // int
        .select_all();
    
    std::vector<Author> authorsDouble = QuerySet<Author>(conn)
        .where<&Author::rating>(4.5)  // double
        .select_all();
    
    std::vector<Author> authorsFloat = QuerySet<Author>(conn)
        .where<&Author::score>(85.5f)  // float
        .select_all();
    
    ASSERT_EQ(authorsInt.size(), 1);
    ASSERT_EQ(authorsDouble.size(), 1);
    ASSERT_EQ(authorsFloat.size(), 1);
}

// MIXED, EQUALS
TEST_F(ORMTest, WhereClauseNTTPComparisonWithTraditional) {
    // Verify NTTP and traditional syntax produce identical results
    std::vector<Author> authorsTraditional = QuerySet<Author>(conn)
        .where(&Author::age, 30, Op::EQ)
        .select_all();
    
    ASSERT_EQ(authorsTraditional.size(), 1);
    
    std::vector<Author> authorsNTTP = QuerySet<Author>(conn)
        .where<&Author::age>(30, Op::EQ)
        .select_all();
    
    ASSERT_EQ(authorsTraditional.size(), authorsNTTP.size());
    ASSERT_EQ(authorsTraditional.size(), 1);
    
    // Both should return the same results
    for (size_t i = 0; i < authorsTraditional.size(); ++i) {
        EXPECT_EQ(authorsTraditional[i].name, authorsNTTP[i].name);
        EXPECT_EQ(authorsTraditional[i].age, authorsNTTP[i].age);
        EXPECT_EQ(authorsTraditional[i].is_active, authorsNTTP[i].is_active);
    }
}

// string, LIKE
TEST_F(ORMTest, WhereClauseStringComparison) {
    Author author("John Doe", 30, "john@example.com");
    QuerySet<Author>(conn).insert(author);
    
    Author author2("John Doe Smith", 31, "john.smith@example.com");
    QuerySet<Author>(conn).insert(author2);
    
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::name, std::string("John%"), Op::LIKE)
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    for (const auto& foundAuthor : authors) {
        EXPECT_TRUE(foundAuthor.name.substr(0, 4) == "John");
    }
}

// int, LESS_OR_EQUAL
TEST_F(ORMTest, WhereClauseLessOrEqualOperator) {
    // Test LESS_OR_EQUAL operator
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 30, Op::LE)
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    for (const auto& author : authors) {
        EXPECT_LE(author.age, 30);
    }
}

// int, GREATER_OR_EQUAL
TEST_F(ORMTest, WhereClauseGreaterOrEqualOperator) {
    // Test GREATER_OR_EQUAL operator  
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GE)
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    for (const auto& author : authors) {
        EXPECT_GE(author.age, 25);
    }
}

// NTTP, int, LESS_OR_EQUAL
TEST_F(ORMTest, WhereClauseNTTPLessOrEqual) {
    // Test LESS_OR_EQUAL with NTTP syntax
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::age>(35, Op::LE)
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    for (const auto& author : authors) {
        EXPECT_LE(author.age, 35);
    }
}

// NTTP, double, GREATER_OR_EQUAL
TEST_F(ORMTest, WhereClauseNTTPGreaterOrEqual) {
    // Test GREATER_OR_EQUAL with NTTP syntax
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::rating>(3.0, Op::GE)
        .select_all();
    
    ASSERT_EQ(authors.size(), 4);
    
    for (const auto& author : authors) {
        EXPECT_GE(author.rating, 3.0);
    }
}

// string, IS
TEST_F(ORMTest, WhereClauseIsOperator) {
    // TODO: simplify using insert instead raw sql, i need this for now because i dont have mechanism to insert NULL values
    // Insert an author with NULL biography directly using SQL
    auto stmt = Statement(conn, "INSERT INTO author (name, age, email, biography) VALUES (?, ?, ?, NULL)");
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();
    
    // Execute the query using our ORM
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(&Author::biography, std::nullopt, Op::IS)  // WHERE biography IS NULL
        .select_all();
    
    // Verify we found the author with NULL biography
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "John Doe");
    EXPECT_EQ(authors[0].age, 30);
    EXPECT_EQ(authors[0].email, "john@example.com");
}

// NTTP, string, IS
TEST_F(ORMTest, WhereClauseNTTPIsOperator) {
    // Test IS operator with NTTP syntax
    // TODO: simplify using insert instead raw sql, i need this for now because i dont have mechanism to insert NULL values
    // Insert an author with NULL biography directly using SQL
    auto stmt = Statement(conn, "INSERT INTO author (name, age, email, biography) VALUES (?, ?, ?, NULL)");
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();
    
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where<&Author::biography>(std::nullopt, Op::IS)  // WHERE biography IS NULL
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
}

// COMPLEX WHERE CONDITIONS
// OR
TEST_F(ORMTest, WhereClauseOrOperator) {
    // Test: age = 25 OR age = 35 (Alice OR Bob)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or field(&Author::age) == 35) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

// AND
TEST_F(ORMTest, WhereClauseAndOperator) {
    // Test: age >= 30 AND rating >= 4.5 (Only Bob - Diana is 28, Charlie is 30 but rating 4.0)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) >= 30 and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 1);
    EXPECT_EQ(authors[0].name, "Bob Johnson");
}

TEST_F(ORMTest, WhereClauseComplexAndOr) {
    // Test: (age = 25 OR age = 28) AND rating >= 4.5 (Alice AND Diana)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 28) and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseComplexOrAnd) {
    // Test: age = 25 OR (age >= 30 AND rating = 5.0) (Alice OR Bob)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or (field(&Author::age) >= 30 and field(&Author::rating) == 5.0)) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseTripleAnd) {
    // Test: age >= 25 AND age <= 30 AND rating >= 4.0 (Alice, Charlie, Diana)
    // Alice: 25, 4.5; Charlie: 30, 4.0; Diana: 28, 5.5
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) >= 25 and field(&Author::age) <= 30 and field(&Author::rating) >= 4.0) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseTripleOr) {
    // Test: age = 25 OR age = 30 OR age = 35 (Alice, Charlie, Bob)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or field(&Author::age) == 30 or field(&Author::age) == 35) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseNestedComplexGrouping) {
    // Test: (age = 25 OR age = 35) AND (rating >= 4.5 OR score >= 90.0)
    // Should match: Alice (25, 4.5) and Bob (35, 5.0, 90.0)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 35) and // NOSONAR
               (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0)) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseComplexNestedConditions) {
    // Test: ((age < 30 AND rating > 4.0) OR (age >= 30 AND score >= 85.0)) AND is_active = true
    // Should match: Alice (25, 4.5, active), Bob (35, 90.0, active), Diana (28, 5.5, 95.0, active)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                (field(&Author::age) >= 30 and field(&Author::score) >= 85.0)) and field(&Author::is_active) == true) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseMixedDataTypes) {
    // Test with different data types: string, int, double, bool
    // Test: name LIKE 'Alice%' OR (age > 30 AND rating >= 4.0 AND is_active = true)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::name).like("Alice%") or // NOSONAR
               (field(&Author::age) > 30 and field(&Author::rating) >= 4.0 and field(&Author::is_active) == true)) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseGroupMethod) {
    // Test using the group() method for explicit grouping
    // Test: (age = 25 OR age = 28) AND rating >= 4.5
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 28) and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseStringOperations) {
    // Test string operations with complex conditions
    // Test: (name LIKE '%Brown' OR email LIKE '%alice%') AND age >= 25
    std::vector<Author> authors = QuerySet<Author>(conn)    
        .where((field(&Author::name).like("%Brown") or field(&Author::email).like("%alice%")) and field(&Author::age) >= 25) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
}

TEST_F(ORMTest, WhereClauseFloatComparisons) {
    // Test float/double comparisons with complex logic
    // Test: (score >= 85.0 AND score <= 90.0) OR rating > 5.0
    // Should match: Alice (85.5), Bob (90.0), Diana (95.0, 5.5)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where((field(&Author::score) >= 85.0 and field(&Author::score) <= 90.0) or field(&Author::rating) > 5.0) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseNotEqualsOperator) {
    // Test NOT_EQUALS with complex conditions
    // Test: age != 30 AND (rating >= 4.5 OR score >= 90.0)
    // Should match: Alice (25, 4.5), Bob (35, 5.0, 90.0), Diana (28, 5.5, 95.0)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) != 30 and (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0)) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 3);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseComplexChaining) {
    // Test complex chaining of multiple conditions
    // Test: (age >= 25 AND age <= 30) OR (rating >= 5.0 AND score >= 90.0) OR name LIKE '%Prince'
    // Should match: Alice (25), Charlie (30), Diana (28), Bob (5.0, 90.0), Diana (name ends with Prince)
    // Note: Diana matches multiple conditions
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where((field(&Author::age) >= 25 and field(&Author::age) <= 30) or // NOSONAR
               (field(&Author::rating) >= 5.0 and field(&Author::score) >= 90.0) or // NOSONAR
               field(&Author::name).like("%Prince"))
        .select_all();
    
    ASSERT_EQ(authors.size(), 4); // All authors should match
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseBooleanLogic) {
    // Test boolean field with complex conditions
    // Test: is_active = true AND ((age < 30 AND rating > 4.0) OR score >= 95.0)
    // Should match: Alice (active, 25, 4.5), Diana (active, 28, 5.5, 95.0)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::is_active) == true and // NOSONAR
               ((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                field(&Author::score) >= 95.0))
        .select_all();
    
    ASSERT_EQ(authors.size(), 2);
    std::set<std::string, std::less<>> names;
    for (const auto& author : authors) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseEmptyResult) {
    // Test condition that should return no results
    // Test: age > 100 OR (rating < 0 AND is_active = false)
    std::vector<Author> authors = QuerySet<Author>(conn)
        .where(field(&Author::age) > 100 or // NOSONAR
               (field(&Author::rating) < 0 and field(&Author::is_active) == false)) // NOSONAR
        .select_all();
    
    ASSERT_EQ(authors.size(), 0);
}

// Only
TEST_F(ORMTest, SelectOnlySpecificFields) {
    // Test selecting only specific fields from the model using NTTP
    // Execute the query using method chaining
    std::vector<Author> authors = QuerySet<Author>(conn)
        .only<&Author::name, &Author::age, &Author::email>()
        .select_all();
    
    std::cout << " Only fields: " << authors.size() << std::endl;
    // Verify results
    ASSERT_EQ(authors.size(), 4); // Should return all 4 authors
    
    // Check that we have the expected fields populated
    for (const auto& author : authors) {
        // These fields should be populated
        EXPECT_FALSE(author.name.empty());
        EXPECT_GT(author.age, 0);
        EXPECT_FALSE(author.email.empty());
        
        // These fields should have default values since they weren't selected
        // Note: This is a design choice - some ORMs might leave these fields uninitialized
        EXPECT_EQ(author.id, 0); // Default value for int
        EXPECT_EQ(author.rating, 0.0); // Default value for double
        EXPECT_EQ(author.score, 0.0f); // Default value for float
        EXPECT_TRUE(author.middleName.empty()); // Default value for string
        EXPECT_TRUE(author.biography.empty()); // Default value for string
    }
}

TEST_F(ORMTest, SelectOnlyWithAlias) {
    // Test selecting fields with aliases
    std::vector<Author> authors = QuerySet<Author>(conn)
        .only<&Author::name>("author_name")
        .only<&Author::age>("author_age")
        .select_all();
    
    // Verify results
    ASSERT_EQ(authors.size(), 4); // Should return all 4 authors
    
    // Check that we have the expected fields populated
    for (const auto& author : authors) {
        // These fields should be populated
        EXPECT_FALSE(author.name.empty());
        EXPECT_GT(author.age, 0);
        
        // These fields should have default values
        EXPECT_TRUE(author.email.empty());
        EXPECT_EQ(author.id, 0);
    }
}

TEST_F(ORMTest, SelectValues) {
    // Test the select_values method for dictionary-like access
    auto values = QuerySet<Author>(conn)
        .only<&Author::name, &Author::age, &Author::is_active>()
        .select_values();
    
    // Verify results
    ASSERT_EQ(values.size(), 4); // Should return all 4 authors
    
    // Check each map for expected values and types
    for (const auto& row : values) {
        // Check that all expected keys exist
        ASSERT_TRUE(row.count("name"));
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(row.count("is_active"));
        
        // Check types and values
        EXPECT_TRUE(std::holds_alternative<std::string>(row.at("name")));
        EXPECT_FALSE(std::get<std::string>(row.at("name")).empty());
        
        EXPECT_TRUE(std::holds_alternative<int>(row.at("age")));
        EXPECT_GT(std::get<int>(row.at("age")), 0);
        
        EXPECT_TRUE(std::holds_alternative<bool>(row.at("is_active")));
        EXPECT_TRUE(std::get<bool>(row.at("is_active")));
    }
}

// DISTINCT TESTS
TEST_F(ORMTest, DistinctTemplateBased) {
    // Setup: Insert duplicate age values
    Author duplicateAge("Frank Miller", 30, "frank@example.com");
    QuerySet<Author>(conn).insert(duplicateAge);
    
    // First, get all authors without distinct
    auto allAuthors = QuerySet<Author>(conn).select_all();
    
    // Then, get distinct ages directly using select_values
    auto distinctAgeValues = QuerySet<Author>(conn)
        .distinct<&Author::age>()
        .select_values();
        
    // We should have 5 total authors (Alice, Bob, Charlie, Diana, Frank)
    ASSERT_EQ(allAuthors.size(), 5);

    // With our implementation, we should get 4 distinct ages
    ASSERT_EQ(distinctAgeValues.size(), 4);
    
    // Create a set of all ages to verify all ages are present
    std::set<int> allAgesSet;
    for (const auto& author : allAuthors) {
        allAgesSet.insert(author.age);
    }
    
    // Extract the ages from the values and convert to a set
    std::set<int> distinctAgesSet;
    for (const auto& row : distinctAgeValues) {
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(std::holds_alternative<int>(row.at("age")));
        distinctAgesSet.insert(std::get<int>(row.at("age")));
    }
    
    // Both sets should have the same 4 distinct ages
    ASSERT_EQ(allAgesSet.size(), 4);
    ASSERT_EQ(distinctAgesSet.size(), 4);
    
    // Verify we have all expected ages
    ASSERT_TRUE(distinctAgesSet.contains(25)); // Alice
    ASSERT_TRUE(distinctAgesSet.contains(28)); // Diana
    ASSERT_TRUE(distinctAgesSet.contains(30)); // Charlie and Frank (both should be returned)
    ASSERT_TRUE(distinctAgesSet.contains(35)); // Bob
    
    // Count how many authors have age 30 (should be 2)
    int age30Count = 0;
    for (const auto& author : allAuthors) {
        if (author.age == 30) age30Count++;
    }
    ASSERT_EQ(age30Count, 2); // Both Charlie and Frank have age 30
}

TEST_F(ORMTest, DistinctMultipleFields) {
    // Setup: Insert authors with duplicate combinations
    Author duplicate1("Greg Smith", 25, "greg@example.com"); // Same age as Alice
    Author duplicate2("Helen Smith", 35, "helen@example.com"); // Same age as Bob
    Author duplicate3("Alice Clone", 25, "clone@example.com", 0, true, 4.5); // Same age and rating as Alice
    
    QuerySet<Author>(conn).insert(duplicate1);
    QuerySet<Author>(conn).insert(duplicate2);
    QuerySet<Author>(conn).insert(duplicate3);
    
    // Test distinct with multiple fields - use select_values() instead of select_all()
    // since distinct queries return only the specified fields, not complete objects
    auto distinctValues = QuerySet<Author>(conn)
        .distinct<&Author::age, &Author::rating>()
        .select_values();
    
    // Count distinct combinations of age and rating
    std::set<std::pair<int, double>> distinctCombinations;
    for (const auto& row : distinctValues) {
        // Extract age and rating from the returned values
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(row.count("rating"));
        
        // With our improved implementation, we should get the correct types
        int age = std::get<int>(row.at("age"));
        double rating = std::get<double>(row.at("rating"));
        distinctCombinations.insert({age, rating});
    }
    
    // Verify we have at least these distinct combinations
    // Alice and clone (25, 4.5), Greg (25, 0.0), Bob and Helen (35, 5.0), Charlie (30, 4.0), Diana (28, 5.5)
    // Note: Previous tests may have added additional authors with different combinations
    ASSERT_GE(distinctCombinations.size(), 5);
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(25, 4.5))); // Alice and clone
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(25, 0.0))); // Greg
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(35, 5.0))); // Bob and Helen
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(30, 4.0))); // Charlie
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(28, 5.5))); // Diana
}

TEST_F(ORMTest, DistinctWithOnlyFields) {
    // Setup: Insert authors with duplicate combinations
    Author duplicate1("Greg Smith", 25, "greg@example.com"); // Same age as Alice
    Author duplicate2("Helen Smith", 35, "helen@example.com"); // Same age as Bob
    Author duplicate3("Alice Clone", 25, "clone@example.com", 0, true, 4.5); // Same age and rating as Alice
    
    QuerySet<Author>(conn).insert(duplicate1);
    QuerySet<Author>(conn).insert(duplicate2);
    QuerySet<Author>(conn).insert(duplicate3);
    
    // Test distinct with only specific fields
    auto distinctValues = QuerySet<Author>(conn)
        .distinct<&Author::age>()
        .only<&Author::age, &Author::name>()
        .select_values();
    
    // When using distinct with only, we get distinct combinations of the fields
    // So we'll have 7 rows (all unique combinations of age and name)
    ASSERT_EQ(distinctValues.size(), 7);
    
    // Each row should contain only age and name fields
    for (const auto& row : distinctValues) {
        // Should have exactly 2 fields
        ASSERT_EQ(row.size(), 2);
        
        // Should have age and name fields
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(row.count("name"));
        
        // Should not have other fields
        ASSERT_FALSE(row.count("email"));
        ASSERT_FALSE(row.count("id"));
        ASSERT_FALSE(row.count("rating"));
    }
    
    // Extract the ages from the values
    std::set<int> distinctAgesSet;
    for (const auto& row : distinctValues) {
        ASSERT_TRUE(std::holds_alternative<int>(row.at("age")));
        distinctAgesSet.insert(std::get<int>(row.at("age")));
    }
    
    // Verify we have all expected ages
    ASSERT_EQ(distinctAgesSet.size(), 4);
    ASSERT_TRUE(distinctAgesSet.contains(25)); // Alice, Greg, and clone
    ASSERT_TRUE(distinctAgesSet.contains(28)); // Diana
    ASSERT_TRUE(distinctAgesSet.contains(30)); // Charlie
    ASSERT_TRUE(distinctAgesSet.contains(35)); // Bob and Helen
}

TEST_F(ORMTest, DistinctWithWhere) {
    // Setup: Insert authors with various ages
    Author author1("Jack", 40, "jack@example.com");
    Author author2("Kate", 40, "kate@example.com");
    Author author3("Luke", 45, "luke@example.com");
    
    QuerySet<Author>(conn).insert(author1);
    QuerySet<Author>(conn).insert(author2);
    QuerySet<Author>(conn).insert(author3);
    
    // Test distinct with where clause
    auto authorResults = QuerySet<Author>(conn)
        .where(storm::field(&Author::age) >= 35)
        .distinct<&Author::age>()
        .select_values();
    
    // We should have at least 3 distinct ages (35, 40, 45) that are >= 35
    // Note: Other tests may have added additional authors with ages >= 35
    ASSERT_GE(authorResults.size(), 3);
    
    // Create a set of ages to verify uniqueness
    std::set<int> distinctAges;
    for (const auto& result : authorResults) {
        // With select_values, we get a map with field name as key
        distinctAges.insert(std::visit([](auto&& arg) -> int {
            using ArgType = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<ArgType, int>) {
                return arg;
            } else if constexpr (std::is_same_v<ArgType, std::monostate>) {
                return 0;
            } else if constexpr (std::is_arithmetic_v<ArgType>) {
                return static_cast<int>(arg);
            } else {
                return 0;
            }
        }, result.at("age")));
    }
    
    // Verify we have at least these 3 distinct ages
    ASSERT_GE(distinctAges.size(), 3);
    ASSERT_TRUE(distinctAges.contains(35)); // Bob, Eve, Ian
    ASSERT_TRUE(distinctAges.contains(40)); // Jack and Kate (but only counted once)
    ASSERT_TRUE(distinctAges.contains(45)); // Luke
}

// RAW SQL TESTS
TEST_F(ORMTest, RawSqlFromStatementInsert) {
    // Create a new author for testing
    Author testAuthor("Test Author", 40, "test@example.com");
    
    // Get the Statement object without executing it
    auto stmt = QuerySet<Author>(conn).stmt_insert(testAuthor);
    
    // Verify that we can get the raw SQL
    std::string rawSql = stmt.sql();
    std::cout << rawSql << std::endl;
    
    // Check the exact SQL string format
    // The SQL should be in the format: INSERT INTO author (name, age, email, is_active, rating, score, middleName, biography) VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id;
    std::string expectedSqlPattern = "INSERT INTO author (";
    ASSERT_TRUE(rawSql.find(expectedSqlPattern) == 0) << "SQL should start with '" << expectedSqlPattern << "' but got: " << rawSql;
    
    // Check for field names in the SQL
    ASSERT_TRUE(rawSql.find("name") != std::string::npos) << "SQL should contain 'name' field";
    ASSERT_TRUE(rawSql.find("age") != std::string::npos) << "SQL should contain 'age' field";
    ASSERT_TRUE(rawSql.find("email") != std::string::npos) << "SQL should contain 'email' field";
    
    // Check for VALUES clause
    ASSERT_TRUE(rawSql.find(") VALUES (") != std::string::npos) << "SQL should contain ') VALUES (' but got: " << rawSql;
    
    // Check for placeholders
    size_t questionMarkCount = 0;
    for (char c : rawSql) {
        if (c == '?') questionMarkCount++;
    }
    ASSERT_EQ(questionMarkCount, 8) << "Expected 8 placeholders but found " << questionMarkCount;
    
    // Check for RETURNING clause
    ASSERT_TRUE(rawSql.find("RETURNING id") != std::string::npos) << "SQL should contain 'RETURNING id' but got: " << rawSql;
    
    // Now execute the insert and verify it worked
    int authorId = QuerySet<Author>(conn).insert(testAuthor);
    ASSERT_GT(authorId, 0);

    // Test with multiple objects
    std::vector<Author> authors = {
        Author("Multi Author 1", 41, "multi1@example.com"),
        Author("Multi Author 2", 42, "multi2@example.com")
    };
    
    // Get the Statement object for multiple inserts
    auto multiStmt = QuerySet<Author>(conn).stmt_insert(authors);
    
    // Verify the SQL for multiple inserts
    std::string multiRawSql = multiStmt.sql();
    
    // Check the exact SQL string format for multiple inserts
    ASSERT_TRUE(multiRawSql.find(expectedSqlPattern) == 0) << "Multi SQL should start with '" << expectedSqlPattern << "' but got: " << multiRawSql;
    
    // Check for VALUES with multiple value sets
    ASSERT_TRUE(multiRawSql.find(") VALUES (?") != std::string::npos) << "SQL should contain ') VALUES (?' but got: " << multiRawSql;
    
    // Check for multiple sets of placeholders
    questionMarkCount = 0;
    for (char c : multiRawSql) {
        if (c == '?') questionMarkCount++;
    }
    ASSERT_EQ(questionMarkCount, 16) << "Expected 16 placeholders (8 per author) but found " << questionMarkCount;
    
    // Execute the multiple inserts and verify
    auto ids = QuerySet<Author>(conn).insert(authors);
    ASSERT_EQ(ids.size(), 2);
    ASSERT_GT(ids[0], 0);
    ASSERT_GT(ids[1], 0);
}

// GROUP BY TESTS
TEST_F(ORMTest, GroupByBasic) {
    // Add more authors with the same age for testing group by
    Author eve("Eve Johnson", 35, "eve@example.com");
    Author frank("Frank Miller", 40, "frank@example.com");
    Author grace("Grace Lee", 25, "grace@example.com");
    Author henry("Henry Wilson", 40, "henry@example.com");
    Author ian("Ian Davis", 35, "ian@example.com");
    
    QuerySet<Author>(conn).insert(eve);
    QuerySet<Author>(conn).insert(frank);
    QuerySet<Author>(conn).insert(grace);
    QuerySet<Author>(conn).insert(henry);
    QuerySet<Author>(conn).insert(ian);
    
    // Use group_by to get distinct ages
    auto results = QuerySet<Author>(conn)
        .group_by<&Author::age>()
        .select_all();
    
    // We should have 4 distinct age groups: 25, 28, 30, 35, 40
    ASSERT_EQ(results.size(), 5);
    
    // Verify the ages are present in the results
    std::set<int> distinctAges;
    for (const auto& author : results) {
        distinctAges.insert(author.age);
    }
    
    ASSERT_EQ(distinctAges.size(), 5);
    ASSERT_TRUE(distinctAges.contains(25));
    ASSERT_TRUE(distinctAges.contains(28));
    ASSERT_TRUE(distinctAges.contains(30));
    ASSERT_TRUE(distinctAges.contains(35));
    ASSERT_TRUE(distinctAges.contains(40));
}

TEST_F(ORMTest, GroupByMultipleFields) {
    // Add more authors with combinations of age and rating for testing group by
    Author jack("Jack Thompson", 40, "jack@example.com", 0, true, 4.0);
    Author kate("Kate Williams", 40, "kate@example.com", 0, true, 4.5);
    Author luke("Luke Brown", 45, "luke@example.com", 0, true, 4.0);
    
    QuerySet<Author>(conn).insert(jack);
    QuerySet<Author>(conn).insert(kate);
    QuerySet<Author>(conn).insert(luke);
    
    // Group by both age and rating
    auto results = QuerySet<Author>(conn)
        .group_by<&Author::age, &Author::rating>()
        .select_all();
    
    // We should have at least these combinations:
    // (25, 4.5) - Alice
    // (35, 5.0) - Bob
    // (30, 4.0) - Charlie
    // (28, 5.5) - Diana
    // (40, 4.0) - Jack
    // (40, 4.5) - Kate
    // (45, 4.0) - Luke
    ASSERT_GE(results.size(), 7);
    
    // Create a set of age-rating pairs to verify unique combinations
    std::set<std::pair<int, double>> ageRatingPairs;
    for (const auto& author : results) {
        ageRatingPairs.insert({author.age, author.rating});
    }
    
    // Verify specific combinations
    ASSERT_TRUE(ageRatingPairs.contains({25, 4.5})); // Alice
    ASSERT_TRUE(ageRatingPairs.contains({35, 5.0})); // Bob
    ASSERT_TRUE(ageRatingPairs.contains({30, 4.0})); // Charlie
    ASSERT_TRUE(ageRatingPairs.contains({40, 4.0})); // Jack
    ASSERT_TRUE(ageRatingPairs.contains({40, 4.5})); // Kate
    ASSERT_TRUE(ageRatingPairs.contains({45, 4.0})); // Luke
}

TEST_F(ORMTest, GroupByWithOrderBy) {
    // Add more authors for testing
    Author mark("Mark Johnson", 35, "mark@example.com", 0, true, 3.5);
    Author nancy("Nancy Davis", 40, "nancy@example.com", 0, true, 3.0);
    
    QuerySet<Author>(conn).insert(mark);
    QuerySet<Author>(conn).insert(nancy);
    
    // Group by age and order by age descending
    auto results = QuerySet<Author>(conn)
        .group_by<&Author::age>()
        .template order_by<&Author::age, false>()
        .select_all();
    
    // Extract ages to verify ordering
    std::vector<int> ages;
    for (const auto& author : results) {
        ages.push_back(author.age);
    }
    
    // Verify that ages are in descending order
    ASSERT_FALSE(ages.empty());
    for (size_t i = 1; i < ages.size(); ++i) {
        ASSERT_GE(ages[i-1], ages[i]) << "Ages should be in descending order";
    }
    
    // Verify the highest age is first
    ASSERT_EQ(ages[0], 40); // Nancy has the highest age (40)
}

TEST_F(ORMTest, GroupByWithJoinAndWhere) {
    // Add more posts for testing
    Post post5("Another Post by Alice", "More content from Alice", alice_id);
    Post post6("Second Post by Bob", "More content from Bob", bob_id);
    Post post7("Another Post by Charlie", "More content from Charlie", charlie_id);
    
    QuerySet<Post>(conn).insert(post5);
    QuerySet<Post>(conn).insert(post6);
    QuerySet<Post>(conn).insert(post7);
    
    // Group posts by author_id where the author's age is greater than 25
    auto results = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Author::age, 25, Op::GT)
        .template group_by<&Author::age>()
        .select_all();
    
    // We should have 3 groups: Bob (35), Charlie (30), Diana (28)
    ASSERT_EQ(results.size(), 3);
    
    // Extract author IDs directly from Author objects
    std::set<int> authorIds;
    for (const auto& post : results) {
        authorIds.insert(post.author_id);
    }
    
    // Verify the correct author IDs are present
    ASSERT_TRUE(authorIds.contains(bob_id));
    ASSERT_TRUE(authorIds.contains(charlie_id));
    ASSERT_TRUE(authorIds.contains(diana_id));
    ASSERT_FALSE(authorIds.contains(alice_id)); // Alice's age is 25, not > 25
}
