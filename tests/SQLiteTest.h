#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include "QuerySet.h"
#include "SQLExceptions.h"
#include "Transaction.h"
#include <sstream>
#include <iomanip>
#include <variant>
#include <ranges>
#include <print>
#include <expected>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <format>
#include <optional>
#include <concepts>

using namespace storm;

struct Author {
    int id;
    std::string name;
    int age;
    std::string email;
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
    int views = 0;

    Post() = default;
    Post(const std::string& t, const std::string& c, int author_id, int id = 0, int views = 0) 
        : id(id), title(t), content(c), author_id(author_id), views(views) {}
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
    field(author_id),
    field(views)
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
                views INTEGER,
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
        
        alice_id = QuerySet<Author>(conn).insert(alice).value();
        bob_id = QuerySet<Author>(conn).insert(bob).value();
        charlie_id = QuerySet<Author>(conn).insert(charlie).value();
        diana_id = QuerySet<Author>(conn).insert(diana).value();
        
        // Create multiple posts for each author with different view counts
        // Alice's posts
        Post alice_post1("Alice's First Post", "Content A1", alice_id, 0, 100);
        Post alice_post2("Alice's Second Post", "Content A2", alice_id, 0, 150);
        Post alice_post3("Alice's Third Post", "Content A3", alice_id, 0, 200);
        
        // Bob's posts
        Post bob_post1("Bob's First Post", "Content B1", bob_id, 0, 250);
        Post bob_post2("Bob's Second Post", "Content B2", bob_id, 0, 300);
        
        // Charlie's posts
        Post charlie_post1("Charlie's First Post", "Content C1", charlie_id, 0, 120);
        Post charlie_post2("Charlie's Second Post", "Content C2", charlie_id, 0, 180);
        Post charlie_post3("Charlie's Third Post", "Content C3", charlie_id, 0, 240);
        Post charlie_post4("Charlie's Fourth Post", "Content C4", charlie_id, 0, 300);
        
        // Diana's posts
        Post diana_post1("Diana's First Post", "Content D1", diana_id, 0, 350);
        Post diana_post2("Diana's Second Post", "Content D2", diana_id, 0, 400);
        
        // Insert all posts using multi-insert
        std::vector<Post> posts = {
            alice_post1, alice_post2, alice_post3,
            bob_post1, bob_post2,
            charlie_post1, charlie_post2, charlie_post3, charlie_post4,
            diana_post1, diana_post2
        };
        
        QuerySet<Post>(conn).insert(posts);
    }

    void TearDown() override {
        conn.reset();
    }
    
    void clearTestData() {
        // Clear all data from tables
        QuerySet<Post>(conn).remove();
        QuerySet<Author>(conn).remove();
    }
};


bool VariantsEqual(const ValueVariant& lhs, const ValueVariant& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }
    
    return std::visit([](const auto& l, const auto& r) -> bool {
        using LType = std::decay_t<decltype(l)>;
        using RType = std::decay_t<decltype(r)>;
        
        if constexpr (!std::is_same_v<LType, RType>) {
            return false;
        } else if constexpr (std::is_same_v<LType, double>) {
            // For floating point, use epsilon comparison
            return std::abs(l - r) < std::numeric_limits<double>::epsilon();
        } else {
            return l == r;
        }
    }, lhs, rhs);
}

// Helper function to convert ValueVariant to string for error messages
std::string ValueVariantToString(const ValueVariant& var) {
    return std::visit([]<typename T>(const T& val) -> std::string {
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        } else if constexpr (std::is_same_v<T, int>) {
            return std::format("{}", val);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::format("{}", val);
        } else if constexpr (std::is_same_v<T, bool>) {
            return std::format("{}", val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::format("\"{}\"", val);
        }
        std::unreachable();
    }, var);
}

// Helper function to verify an entire vector of result maps
void AssertResultsMatch(const ExpectedValueVectorMap& actual_result,
                        const ValueVectorMap& expected_results) {
    
    ASSERT_TRUE(actual_result.has_value()) << "failed with error: " << actual_result.error();
    auto actual_results = actual_result.value();

    ASSERT_EQ(actual_results.size(), expected_results.size()) 
        << std::format("Result count mismatch. Expected {} results, got {}", 
                      expected_results.size(), actual_results.size());
    
    // Use bitset for tracking matched results
    std::vector<bool> matched(actual_results.size());
    
    // For each expected result, find a matching actual result
    for (auto&& [expected_idx, expected] : expected_results | std::views::enumerate) {
        ASSERT_FALSE(expected.empty()) 
            << std::format("Expected result map {} is empty", expected_idx);
        
        auto find_match = [&](const auto& actual_with_idx) -> bool {
            auto&& [actual_idx, actual] = actual_with_idx;
            
            if (matched[actual_idx]) return false;
            
            // Check size first for early exit
            if (actual.size() != expected.size()) return false;
            
            // Use ranges algorithms for key-value matching
            return std::ranges::all_of(expected, [&](const auto& expected_pair) {
                auto&& [key, expected_val] = expected_pair;
                return actual.contains(key) && VariantsEqual(actual.at(key), expected_val);
            });
        };
        
        auto match_it = std::ranges::find_if(
            actual_results | std::views::enumerate, 
            find_match
        );
        
        if (match_it != std::ranges::end(actual_results | std::views::enumerate)) {
            matched[std::get<0>(*match_it)] = true;
        } else {
            // Build comprehensive error message using ranges
            auto build_map_string = [](const auto& map) -> std::string {
                std::string result = "{";
                bool first = true;
                for (const auto& [key, val] : map) {
                    if (!first) result += ", ";
                    result += std::format("{}: {}", key, ValueVariantToString(val));
                    first = false;
                }
                result += "}";
                return result;
            };
            
            auto error_msg = std::format(
                "No matching result found for expected result {}. Expected: {}\n"
                "Actual results:\n", 
                expected_idx,
                build_map_string(expected)
            );
            
            for (const auto& [i, result] : actual_results | std::views::enumerate) {
                error_msg += std::format("  [{}] {}\n", i, build_map_string(result));
            }
            
            ASSERT_TRUE(false) << error_msg;
        }
    }
}

// =======================================
// INSERT TESTS
// =======================================
TEST_F(ORMTest, InsertSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    
    auto result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result.has_value()) << "Insert should return a valid ID" << result.error();
    EXPECT_GT(result.value(), 0) << "Insert should return a valid ID";
    EXPECT_EQ(author.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertEmptyFieldNames) {
    Author author("Jane Doe", 25, "jane@example.com");
    
    auto result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result.has_value()) << "Insert should return a valid ID" << result.error();
    EXPECT_GT(result.value(), 0) << "Insert should return a valid ID";
    EXPECT_EQ(author.id, 0) << "Must not be inserted from db";
}

TEST_F(ORMTest, InsertMultipleObjects) {
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com"),
        Author("Grace Wilson", 26, "grace@example.com")
    };
    
    auto ids = QuerySet<Author>(conn).insert(authors);
    
    EXPECT_TRUE(ids.has_value()) << "Insert should return a valid ID" << ids.error();
    EXPECT_EQ(ids.value().size(), authors.size()) << "Should return an ID for each inserted object";
    
    // Check that all returned IDs are valid
    for (const auto& id : ids.value()) {
        EXPECT_GT(id, 0) << "Each ID should be valid (greater than 0)";
    }
}

TEST_F(ORMTest, InsertEmptyVector) {
    std::vector<Author> empty_authors;
    
    auto ids = QuerySet<Author>(conn).insert(empty_authors);
    
    EXPECT_TRUE(ids.has_value()) << "Inserting empty vector should return empty vector of IDs" << ids.error();
}

TEST_F(ORMTest, InsertExceptionHandling) {
    Author author("", -1, "");
    
    auto result = QuerySet<Author>(conn).insert(author);
    
    EXPECT_TRUE(result.has_value()) << "Should return valid ID or -1 on error" << result.error();
}

// =======================================
// UPDATE TESTS
// =======================================
TEST_F(ORMTest, UpdateSingleObject) {
    Author author("John Doe", 30, "john@example.com");
    auto inserted_id = QuerySet<Author>(conn).insert(author);
    
    ASSERT_TRUE(inserted_id.has_value()) << "Insert should return a ID" << inserted_id.error();
    ASSERT_GT(inserted_id.value(), 0);
    
    // Update the author with the inserted ID
    author.id = inserted_id.value();
    author.name = "John Smith"; 
    author.age = 31;
    
    auto result = QuerySet<Author>(conn).update(author);
    
    EXPECT_TRUE(result.has_value()) << "Update should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, UpdateMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com")
    };
    auto ids = QuerySet<Author>(conn).insert(authors);
    
    ASSERT_TRUE(ids.has_value()) << "Insert should return a valid ID" << ids.error();
    ASSERT_EQ(ids.value().size(), authors.size());
    
    // Set the IDs and update them
    authors[0].id = ids.value()[0];
    authors[1].id = ids.value()[1];
    authors[0].age = 29;
    authors[1].name = "Robert";
    
    auto result = QuerySet<Author>(conn).update(authors);
    
    EXPECT_TRUE(result.has_value()) << "Update should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, UpdateEmptyVector) {
    std::vector<Author> empty_authors;
    
    auto result = QuerySet<Author>(conn).update(empty_authors);
    
    EXPECT_TRUE(result.has_value()) << "Updating empty vector should return true" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, UpdateNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    auto result = QuerySet<Author>(conn).update(author);
    
    // Behavior depends on implementation - might return true even if no rows affected
    EXPECT_TRUE(result.has_value()) << "Update should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

// =======================================
// DELETE TESTS
// =======================================
TEST_F(ORMTest, RemoveSingleObject) {
    // First insert a person
    Author author("John Doe", 30, "john@example.com");
    auto inserted_id = QuerySet<Author>(conn).insert(author);
    
    ASSERT_TRUE(inserted_id.has_value()) << "Insert should return a valid ID" << inserted_id.error();
    ASSERT_GT(inserted_id.value(), 0);
    
    author.id = inserted_id.value();
    auto result = QuerySet<Author>(conn).remove(author);
    
    EXPECT_TRUE(result.has_value()) << "Remove should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, RemoveMultipleObjects) {
    // First insert multiple people
    std::vector<Author> authors = {
        Author("Eve Adams", 28, "eve@example.com"),
        Author("Frank Miller", 32, "frank@example.com"),
        Author("Grace Wilson", 26, "grace@example.com")
    };
    auto ids = QuerySet<Author>(conn).insert(authors);
    
    ASSERT_TRUE(ids.has_value()) << "Insert should return a valid ID" << ids.error();
    ASSERT_EQ(ids.value().size(), authors.size());
    
    // Set the IDs
    for (size_t i = 0; i < authors.size(); ++i) {
        authors[i].id = ids.value()[i];
    }
    
    auto result = QuerySet<Author>(conn).remove(authors);
    
    EXPECT_TRUE(result.has_value()) << "Remove should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, RemoveEmptyVector) {
    std::vector<Author> empty_authors;
    
    auto result = QuerySet<Author>(conn).remove(empty_authors);
    
    EXPECT_TRUE(result.has_value()) << "Remove should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, RemoveNonExistentObject) {
    Author author("Ghost", 0, "ghost@example.com");
    author.id = 99999; // Non-existent ID
    
    auto result = QuerySet<Author>(conn).remove(author);
    
    // Should handle gracefully
    EXPECT_TRUE(result.has_value()) << "Remove should return a bool" << result.error();
    EXPECT_TRUE(result.value());
}

TEST_F(ORMTest, RemoveAll) {
    // First count how many authors we have
    auto result = QuerySet<Author>(conn).select_all();

    EXPECT_TRUE(result.has_value()) << "Select should return a value" << result.error();
    int initial_count = result.value().size();
    ASSERT_GT(initial_count, 0);
    
    // Delete all authors
    auto result2 = QuerySet<Author>(conn).remove();
    EXPECT_TRUE(result2.has_value()) << "Remove should return a bool" << result2.error();
    EXPECT_TRUE(result2.value());
    
    // Verify all authors were deleted
    auto result3 = QuerySet<Author>(conn).select_all();
    
    EXPECT_TRUE(result3.has_value()) << "Select should return a value" << result3.error();
    EXPECT_EQ(result3.value().size(), 0);
}

TEST_F(ORMTest, RemoveByCondition) {
    // First count how many authors we have
    auto result = QuerySet<Author>(conn).select_all();
    EXPECT_TRUE(result.has_value()) << "Select should return a value" << result.error();
    int initial_count = result.value().size();
    ASSERT_GT(initial_count, 0);
    
    // Count authors with age > 30
    auto result2 = QuerySet<Author>(conn).where(field(&Author::age) > 30).select_all();
    EXPECT_TRUE(result2.has_value()) << "Select should return a value" << result2.error();
    int older_count = result2.value().size();
    ASSERT_GT(older_count, 0);
    ASSERT_LT(older_count, initial_count);
    
    // Delete authors with age > 30
    auto result3 = QuerySet<Author>(conn).where(field(&Author::age) > 30).remove();
    EXPECT_TRUE(result3.has_value()) << "Remove should return a bool" << result3.error();
    EXPECT_TRUE(result3.value());
    
    // Verify only matching authors were deleted
    auto result4 = QuerySet<Author>(conn).select_all();
    EXPECT_TRUE(result4.has_value()) << "Select should return a value" << result4.error();
    EXPECT_EQ(result4.value().size(), initial_count - older_count);
    
    // Verify no remaining authors have age > 30
    for (const auto& author : result4.value()) {
        EXPECT_LE(author.age, 30);
    }
}

// =======================================
// QUERYSET COPY TESTS
// =======================================
TEST_F(ORMTest, QuerySetCopy) {
    // Create a base query
    auto baseQuery = QuerySet<Author>(conn).where<&Author::is_active>(true);
    
    // Create two copies and add different conditions
    auto youngAuthorsQuery = baseQuery; // Copy NOSONAR
    youngAuthorsQuery.where(field(&Author::age) <= 30);
    
    auto seniorAuthorsQuery = baseQuery; // Copy NOSONAR
    seniorAuthorsQuery.where(field(&Author::age) > 30);
    
    // Execute the queries
    auto youngAuthors = youngAuthorsQuery.select_all();
    ASSERT_TRUE(youngAuthors.has_value()) << "Select should return a value" << youngAuthors.error();
    auto seniorAuthors = seniorAuthorsQuery.select_all();
    ASSERT_TRUE(seniorAuthors.has_value()) << "Select should return a value" << seniorAuthors.error();
    
    // Verify the results
    for (const auto& author : youngAuthors.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_LE(author.age, 30);
    }
    
    for (const auto& author : seniorAuthors.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_GT(author.age, 30);
    }
    
    // Verify that the original query is unaffected
    auto activeAuthors = baseQuery.select_all();
    ASSERT_TRUE(activeAuthors.has_value()) << "Select should return a value" << activeAuthors.error();
    for (const auto& author : activeAuthors.value()) {
        EXPECT_TRUE(author.is_active);
    }
    
    // Verify that we have the correct number of results
    EXPECT_EQ(youngAuthors.value().size() + seniorAuthors.value().size(), activeAuthors.value().size());
}

TEST_F(ORMTest, QuerySetCopyConstructorDeepCopy) {
    // Create a complex query with all possible member variables set
    auto originalQuery = QuerySet<Author>(conn)
        .where(field(&Author::is_active) == true)
        .where(field(&Author::age) > 20)
        .order_by<&Author::age, false>() // descending
        .order_by<&Author::name, true>() // ascending
        .distinct<&Author::age>()
        .only<&Author::name>("author_name")
        .only<&Author::age>()
        .group_by<&Author::is_active>()
        .limit(10)
        .offset(5);
    
    // Create a copy using the copy constructor
    auto copiedQuery = originalQuery; // Copy NOSONAR
    
    // Verify that both queries produce the same results initially
    auto originalResults = originalQuery.select_all();
    ASSERT_TRUE(originalResults.has_value()) << "Select should return a value" << originalResults.error();
    auto copiedResults = copiedQuery.select_all();
    ASSERT_TRUE(copiedResults.has_value()) << "Select should return a value" << copiedResults.error();
    
    EXPECT_EQ(originalResults.value().size(), copiedResults.value().size());
    
    // Now modify the copied query
    copiedQuery.where(field(&Author::rating) > 4.0);
    
    // The original query should remain unchanged
    auto newOriginalResults = originalQuery.select_all();
    ASSERT_TRUE(newOriginalResults.has_value()) << "Select should return a value" << newOriginalResults.error();
    auto newCopiedResults = copiedQuery.select_all();
    ASSERT_TRUE(newCopiedResults.has_value()) << "Select should return a value" << newCopiedResults.error();
    
    // Original results should be the same as before
    EXPECT_EQ(originalResults.value().size(), newOriginalResults.value().size());
    
    // Copied results should be different (filtered further)
    EXPECT_LE(newCopiedResults.value().size(), originalResults.value().size());
    
    // Check that all authors in the copied results have rating > 4.0
    for (const auto& author : newCopiedResults.value()) {
        EXPECT_GT(author.rating, 4.0);
    }
}

TEST_F(ORMTest, QuerySetCopyConstructorMembersVerification) {
    // Test each member variable individually to ensure proper copying
    
    // 1. Test whereExpression copying
    auto whereQuery = QuerySet<Author>(conn).where(field(&Author::age) > 25); // Greater than
    auto whereQueryCopy = whereQuery; // Copy NOSONAR
    
    // Verify both queries return the same results
    auto whereResults = whereQuery.select_all();
    auto whereCopyResults = whereQueryCopy.select_all();
    ASSERT_TRUE(whereResults.has_value()) << "Select should return a value" << whereResults.error();
    ASSERT_TRUE(whereCopyResults.has_value()) << "Select should return a value" << whereCopyResults.error();
    EXPECT_EQ(whereResults.value().size(), whereCopyResults.value().size());
    
    // Modify copy and verify independence
    whereQueryCopy.where(field(&Author::email) == "alice@example.com"); // Use a specific email to ensure different results
    auto whereQueryResults = whereQuery.select_all();
    auto whereQueryCopyResults = whereQueryCopy.select_all();
    ASSERT_TRUE(whereQueryResults.has_value()) << "Select should return a value" << whereQueryResults.error();
    ASSERT_TRUE(whereQueryCopyResults.has_value()) << "Select should return a value" << whereQueryCopyResults.error();
    EXPECT_NE(whereQueryResults.value().size(), whereQueryCopyResults.value().size());
    
    // 2. Test joinInfo copying
    auto joinQuery = QuerySet<Post>(conn).join<Author>();
    auto joinQueryCopy = joinQuery; // Copy NOSONAR
    
    // Verify both queries can execute successfully
    EXPECT_NO_THROW(joinQuery.select_all());
    EXPECT_NO_THROW(joinQueryCopy.select_all());
    
    // 3. Test orderFields copying
    auto orderQuery = QuerySet<Author>(conn).order_by<&Author::age, false>();
    auto orderQueryCopy = orderQuery; // Copy NOSONAR
    
    // Verify both queries return results in the same order
    auto orderResults = orderQuery.select_all();
    ASSERT_TRUE(orderResults.has_value()) << "Select should return a value" << orderResults.error();
    auto orderCopyResults = orderQueryCopy.select_all();
    ASSERT_TRUE(orderCopyResults.has_value()) << "Select should return a value" << orderCopyResults.error();
    ASSERT_GT(orderResults.value().size(), 1);
    EXPECT_EQ(orderResults.value()[0].age, orderCopyResults.value()[0].age);
    
    // Modify copy's ordering and verify independence
    orderQueryCopy.order_by<&Author::name, true>();
    auto modifiedOrderCopyResults = orderQueryCopy.select_all();
    ASSERT_TRUE(modifiedOrderCopyResults.has_value()) << "Select should return a value" << modifiedOrderCopyResults.error();
    
    // The original order should still be by age descending
    auto newOrderResults = orderQuery.select_all();
    ASSERT_TRUE(newOrderResults.has_value()) << "Select should return a value" << newOrderResults.error();
    EXPECT_EQ(orderResults.value()[0].age, newOrderResults.value()[0].age);
    
    // 4. Test distinctFields copying
    auto distinctQuery = QuerySet<Author>(conn).distinct<&Author::age>();
    auto distinctQueryCopy = distinctQuery; // Copy NOSONAR
    
    // Verify both queries return the same number of distinct results
    auto distinctResults = distinctQuery.select_all();
    auto distinctCopyResults = distinctQueryCopy.select_all();
    ASSERT_TRUE(distinctResults.has_value()) << "Select should return a value" << distinctResults.error();
    ASSERT_TRUE(distinctCopyResults.has_value()) << "Select should return a value" << distinctCopyResults.error();
    EXPECT_EQ(distinctResults.value().size(), distinctCopyResults.value().size());
    
    // 5. Test onlyFields copying
    auto onlyQuery = QuerySet<Author>(conn).only<&Author::name>();
    auto onlyQueryCopy = onlyQuery; // Copy NOSONAR
    
    // Both should execute successfully
    EXPECT_NO_THROW(onlyQuery.select_all());
    EXPECT_NO_THROW(onlyQueryCopy.select_all());
    
    // 6. Test groupByFields copying
    auto groupQuery = QuerySet<Author>(conn).group_by<&Author::is_active>();
    auto groupQueryCopy = groupQuery; // Copy NOSONAR
    
    // Both should execute successfully
    EXPECT_NO_THROW(groupQuery.select_all());
    EXPECT_NO_THROW(groupQueryCopy.select_all());
    
    // 7. Test limit and offset copying
    auto limitQuery = QuerySet<Author>(conn).limit(2).offset(1);
    auto limitQueryCopy = limitQuery; // Copy NOSONAR
    
    // Verify both queries return the same number of results
    auto limitResults = limitQuery.select_all();
    auto limitCopyResults = limitQueryCopy.select_all();
    ASSERT_TRUE(limitResults.has_value()) << "Select should return a value" << limitResults.error();
    ASSERT_TRUE(limitCopyResults.has_value()) << "Select should return a value" << limitCopyResults.error();
    EXPECT_EQ(limitResults.value().size(), limitCopyResults.value().size());
    EXPECT_EQ(2, limitResults.value().size()); // Should be limited to 2
    
    // Modify copy and verify independence
    limitQueryCopy.limit(3);
    EXPECT_EQ(2, limitQuery.select_all().value().size());
    EXPECT_EQ(3, limitQueryCopy.select_all().value().size());
}

TEST_F(ORMTest, QuerySetCopyAssignmentOperator) {
    // Create two different queries
    auto query1 = QuerySet<Author>(conn)
        .where(field(&Author::age) < 30)
        .order_by<&Author::name, true>();
    
    auto query2 = QuerySet<Author>(conn)
        .where(field(&Author::rating) > 4.0)
        .limit(2);
    
    // Save the original results
    auto originalQuery1Results = query1.select_all();
    auto originalQuery2Results = query2.select_all();
    
    // Perform copy assignment
    query2 = query1; // Copy assignment
    
    // Verify query2 now produces the same results as query1
    auto newQuery2Results = query2.select_all();
    ASSERT_TRUE(originalQuery1Results.has_value()) << "Select should return a value" << originalQuery1Results.error();
    ASSERT_TRUE(newQuery2Results.has_value()) << "Select should return a value" << newQuery2Results.error();
    EXPECT_EQ(originalQuery1Results.value().size(), newQuery2Results.value().size());
    
    // Modify query2 and verify query1 remains unchanged
    query2.where(field(&Author::email).like("%alice@example.com"));
    
    auto modifiedQuery2Results = query2.select_all();
    auto finalQuery1Results = query1.select_all();
    
    // Original query1 should be unchanged
    ASSERT_TRUE(finalQuery1Results.has_value()) << "Select should return a value" << finalQuery1Results.error();
    EXPECT_EQ(originalQuery1Results.value().size(), finalQuery1Results.value().size());
    
    // Modified query2 should have different results
    ASSERT_TRUE(modifiedQuery2Results.has_value()) << "Select should return a value" << modifiedQuery2Results.error();
    EXPECT_NE(originalQuery1Results.value().size(), modifiedQuery2Results.value().size());
    
    // Self-assignment test
    query1 = query1; // Self-assignment should be safe
    auto afterSelfAssignResults = query1.select_all();
    ASSERT_TRUE(afterSelfAssignResults.has_value()) << "Select should return a value" << afterSelfAssignResults.error();
    EXPECT_EQ(originalQuery1Results.value().size(), afterSelfAssignResults.value().size());
}

TEST_F(ORMTest, QuerySetCopyEdgeCases) {
    // 1. Test copying an empty query
    auto emptyQuery = QuerySet<Author>(conn);
    auto emptyQueryCopy = emptyQuery; // Copy NOSONAR
    
    // Both should return all authors
    auto emptyQueryResults = emptyQuery.select_all();
    auto emptyQueryCopyResults = emptyQueryCopy.select_all();
    ASSERT_TRUE(emptyQueryResults.has_value()) << "Select should return a value" << emptyQueryResults.error();
    ASSERT_TRUE(emptyQueryCopyResults.has_value()) << "Select should return a value" << emptyQueryCopyResults.error();
    EXPECT_EQ(emptyQueryResults.value().size(), emptyQueryCopyResults.value().size());
    
    // 2. Test copying a query with no results
    auto noResultsQuery = QuerySet<Author>(conn).where(field(&Author::age) > 100); // No one is that old
    auto noResultsQueryCopy = noResultsQuery; // Copy NOSONAR
    
    // Both should return empty results
    auto noResultsQueryResults = noResultsQuery.select_all();
    auto noResultsQueryCopyResults = noResultsQueryCopy.select_all();
    ASSERT_TRUE(noResultsQueryResults.has_value()) << "Select should return a value" << noResultsQueryResults.error();
    ASSERT_TRUE(noResultsQueryCopyResults.has_value()) << "Select should return a value" << noResultsQueryCopyResults.error();
    EXPECT_EQ(noResultsQueryResults.value().size(), noResultsQueryCopyResults.value().size());
    
    // 3. Test complex chained query copying
    auto complexQuery = QuerySet<Author>(conn)
        .where<&Author::age>(25, Op::GT)
        .where<&Author::is_active>(true)
        .where<&Author::rating>(4.0, Op::GE)
        .order_by<&Author::age, false>()
        .order_by<&Author::name, true>()
        .limit(5)
        .offset(1);
    
    auto complexQueryCopy = complexQuery; // Copy NOSONAR
    
    // Both should return the same results
    auto complexResults = complexQuery.select_all();
    auto complexCopyResults = complexQueryCopy.select_all();
    
    ASSERT_TRUE(complexResults.has_value()) << "Select should return a value" << complexResults.error();
    ASSERT_TRUE(complexCopyResults.has_value()) << "Select should return a value" << complexCopyResults.error();
    EXPECT_EQ(complexResults.value().size(), complexCopyResults.value().size());
    
    if (!complexResults.value().empty() && !complexCopyResults.value().empty()) {
        // Check that the first result is the same in both queries
        EXPECT_EQ(complexResults.value()[0].id, complexCopyResults.value()[0].id);
        EXPECT_EQ(complexResults.value()[0].name, complexCopyResults.value()[0].name);
        EXPECT_EQ(complexResults.value()[0].age, complexCopyResults.value()[0].age);
    }
    
    // 4. Test multiple consecutive copies
    auto baseQuery = QuerySet<Author>(conn).where<&Author::is_active>(true);
    auto copy1 = baseQuery; // First copy
    auto copy2 = copy1;     // Copy of a copy
    auto copy3 = copy2;     // Copy of a copy of a copy
    
    // All should return the same results
    auto result = baseQuery.select_all();
    auto copy1Result = copy1.select_all();
    auto copy2Result = copy2.select_all();
    auto copy3Result = copy3.select_all();
    
    ASSERT_TRUE(result.has_value()) << "Select should return a value" << result.error();
    ASSERT_TRUE(copy1Result.has_value()) << "Select should return a value" << copy1Result.error();
    ASSERT_TRUE(copy2Result.has_value()) << "Select should return a value" << copy2Result.error();
    ASSERT_TRUE(copy3Result.has_value()) << "Select should return a value" << copy3Result.error();
    
    EXPECT_EQ(result.value().size(), copy1Result.value().size());
    EXPECT_EQ(result.value().size(), copy2Result.value().size());
    EXPECT_EQ(result.value().size(), copy3Result.value().size());
    
    // Modify each copy differently and verify independence
    copy1.where<&Author::age>(30, Op::LT); // Less than
    copy2.where<&Author::rating>(4.0, Op::GT); // Greater than
    copy3.where<&Author::name>("A%", Op::LIKE); // LIKE operator
    
    // Each query should now return different results
    auto baseResults = baseQuery.select_all();
    auto copy1Results = copy1.select_all();
    auto copy2Results = copy2.select_all();
    auto copy3Results = copy3.select_all();
    
    // The original should be unchanged
    ASSERT_TRUE(baseResults.has_value()) << "Select should return a value" << baseResults.error();
    ASSERT_TRUE(copy1Results.has_value()) << "Select should return a value" << copy1Results.error();
    for (const auto& author : baseResults.value()) {
        EXPECT_TRUE(author.is_active);
    }
    
    // Copy1 should have additional age filter
    ASSERT_TRUE(copy1Results.has_value()) << "Select should return a value" << copy1Results.error();
    for (const auto& author : copy1Results.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_LT(author.age, 30);
    }
    
    // Copy2 should have additional rating filter
    ASSERT_TRUE(copy2Results.has_value()) << "Select should return a value" << copy2Results.error();
    for (const auto& author : copy2Results.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_GT(author.rating, 4.0);
    }
    
    // Copy3 should have name filter (starts with 'A')
    ASSERT_TRUE(copy3Results.has_value()) << "Select should return a value" << copy3Results.error();
    for (const auto& author : copy3Results.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_EQ('A', author.name[0]);
    }
}

// =======================================
// INTEGRATION TESTS
// =======================================
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Author author("John Doe", 30, "john@example.com");
    auto result = QuerySet<Author>(conn).insert(author);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    int id = result.value();
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
    auto result = QuerySet<Author>(conn).insert(authors);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    ASSERT_EQ(result.value(), authors.size());
    
    // Verify all returned IDs are valid
    for (const auto& id : result.value()) {
        ASSERT_GT(id, 0);
    }
    
    // Set IDs and batch update
    for (size_t i = 0; i < authors.size(); ++i) {
        authors[i].id = result.value()[i];
        authors[i].age += 1;
    }
    ASSERT_TRUE(QuerySet<Author>(conn).update(authors));
    
    // Batch delete
    ASSERT_TRUE(QuerySet<Author>(conn).remove(authors));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Author single_author("Single", 25, "single@example.com");
    auto result = QuerySet<Author>(conn).insert(single_author);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    ASSERT_GT(result.value(), 0);
    
    // Insert batch
    std::vector<Author> batch_authors = {
        Author("Batch1", 30, "batch1@example.com"),
        Author("Batch2", 35, "batch2@example.com")
    };
    auto result2 = QuerySet<Author>(conn).insert(batch_authors);
    ASSERT_TRUE(result2.has_value()) << "Insert should return a value" << result2.error();
    auto result2Value = result2.value();
    ASSERT_EQ(result2Value, batch_authors.size());
    
    // Update single
    single_author.id = result.value();
    single_author.age = 26;
    result = QuerySet<Author>(conn).update(single_author);
    ASSERT_TRUE(result.has_value()) << "Update should return a value" << result.error();
    ASSERT_EQ(result.value(), true);
    
    // Update batch
    for (size_t i = 0; i < batch_authors.size(); ++i) {
        batch_authors[i].id = result2Value[i];
        batch_authors[i].age += 1;
    }
    result = QuerySet<Author>(conn).update(batch_authors);
    ASSERT_TRUE(result.has_value()) << "Update should return a value" << result.error();
    ASSERT_EQ(result.value(), true);
    
    // Remove single
    result = QuerySet<Author>(conn).remove(single_author);
    ASSERT_TRUE(result.has_value()) << "Remove should return a value" << result.error();
    ASSERT_EQ(result.value(), true);
    
    // Remove batch
    result = QuerySet<Author>(conn).remove(batch_authors);
    ASSERT_TRUE(result.has_value()) << "Remove should return a value" << result.error();
    ASSERT_EQ(result.value(), true);
}

// =======================================
// SELECT TESTS
// =======================================
TEST_F(ORMTest, SelectAll) {
    auto all_authors = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(all_authors.has_value()) << "Select should return a value" << all_authors.error();
    ASSERT_EQ(all_authors.value().size(), 4); // Alice, Bob, Charlie, Diana
}

TEST_F(ORMTest, SelectAllWhereId) {
    auto authors = QuerySet<Author>(conn)
        .where(&Author::id, alice_id)
        .select_all();
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto value = authors.value();
    EXPECT_EQ(value[0].name, "Alice Smith");
}

TEST_F(ORMTest, SelectAllWhereMany) {
    auto posts = QuerySet<Post>(conn)
        .where(&Post::author_id, alice_id)
        .where(&Post::title, "Alice's First Post")
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Alice's First Post");
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
    } catch (const std::exception& e) {
        FAIL() << "Expected InvalidColumnException, but a different exception was thrown: " << e.what();
    }
}

// =======================================
// JOIN TESTS
// =======================================
TEST_F(ORMTest, SelectAllWithJoin) {
    auto all_posts = QuerySet<Post>(conn)
        .join<Author>()
        .select_all();
    ASSERT_TRUE(all_posts.has_value()) << "Select should return a value" << all_posts.error();
    ASSERT_EQ(all_posts.value().size(), 4); // Should have all 4 posts with author data
}

TEST_F(ORMTest, SelectAllWithJoinWhere) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereMany) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .where(&Author::is_active, true)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereLimit) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    EXPECT_EQ(posts.value()[0].title, "Post A");
}

TEST_F(ORMTest, SelectAllWithJoinWhereOffset) {
    // First insert another post for Alice to test offset
    Post extra_post("Post A2", "Content A2", alice_id);
    QuerySet<Post>(conn).insert(extra_post);
    
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .offset(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1); // After offset(1), we should have 1 post remaining
}

TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffset) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 0); // Only one post for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupBy) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimit) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .limit(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndOffset) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .offset(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 0); // Only one group for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimitOffset) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .template group_by<&Author::name>()
        .limit(1)
        .offset(1)
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 0);
}

// =======================================
// ORDER BY TESTS
// =======================================
TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffsetOrderBy) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .where(&Post::author_id, alice_id)
        .limit(1)
        .offset(1)
        .order_by<&Author::name>()
        .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 0);
}

TEST_F(ORMTest, OrderBySingleFieldAscendingExplicit) {
    auto authors = QuerySet<Author>(conn)
        .order_by<&Author::name, true>()
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();
    // Check ascending order by name: Alice, Bob, Charlie, Diana
    EXPECT_EQ(value[0].name, "Alice Smith");
    EXPECT_EQ(value[1].name, "Bob Johnson");
    EXPECT_EQ(value[2].name, "Charlie Brown");
    EXPECT_EQ(value[3].name, "Diana Prince");
}

TEST_F(ORMTest, OrderBySingleFieldDescending) {
    auto authors = QuerySet<Author>(conn)
        .order_by<&Author::name, false>()
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();
    
    // Check descending order by name: Diana, Charlie, Bob, Alice
    EXPECT_EQ(value[0].name, "Diana Prince");
    EXPECT_EQ(value[1].name, "Charlie Brown");
    EXPECT_EQ(value[2].name, "Bob Johnson");
    EXPECT_EQ(value[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderBySingleFieldDefaultAscending) {
    auto authors = QuerySet<Author>(conn)
        .order_by<&Author::age>()  // Default is ascending
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();
    
    // Check ascending order by age: 25, 28, 30, 35
    EXPECT_EQ(value[0].age, 25);  // Alice
    EXPECT_EQ(value[1].age, 28);  // Diana
    EXPECT_EQ(value[2].age, 30);  // Charlie
    EXPECT_EQ(value[3].age, 35);  // Bob
    
    EXPECT_EQ(value[0].name, "Alice Smith");
    EXPECT_EQ(value[1].name, "Diana Prince");
    EXPECT_EQ(value[2].name, "Charlie Brown");
    EXPECT_EQ(value[3].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByMultipleFieldsMixedDirections) {
    // Add more test data with same ages to test secondary ordering
    Author author5("Eve Adams", 25, "eve@example.com");  // Same age as Alice
    Author author6("Frank Miller", 35, "frank@example.com");  // Same age as Bob
    QuerySet<Author>(conn).insert(author5);
    QuerySet<Author>(conn).insert(author6);
    
    auto authors = QuerySet<Author>(conn)
        .order_by<&Author::age, true, &Author::name, false>()  // Age ASC, Name DESC
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 6);
    auto authorsValue = authors.value();
    
    // Check ordering: Age ascending, then name descending within same age
    // Age 25: Eve Adams, Alice Smith (Eve > Alice alphabetically, DESC)
    // Age 28: Diana Prince
    // Age 30: Charlie Brown  
    // Age 35: Frank Miller, Bob Johnson (Frank > Bob alphabetically, DESC)
    
    EXPECT_EQ(authorsValue[0].age, 25);
    EXPECT_EQ(authorsValue[0].name, "Eve Adams");
    
    EXPECT_EQ(authorsValue[1].age, 25);
    EXPECT_EQ(authorsValue[1].name, "Alice Smith");
    
    EXPECT_EQ(authorsValue[2].age, 28);
    EXPECT_EQ(authorsValue[2].name, "Diana Prince");
    
    EXPECT_EQ(authorsValue[3].age, 30);
    EXPECT_EQ(authorsValue[3].name, "Charlie Brown");
    
    EXPECT_EQ(authorsValue[4].age, 35);
    EXPECT_EQ(authorsValue[4].name, "Frank Miller");
    
    EXPECT_EQ(authorsValue[5].age, 35);
    EXPECT_EQ(authorsValue[5].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByWithJoin) {
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .order_by<&Author::name, true>()
        .select_all();
    
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 4);
    auto postsValue = posts.value();
    
    // Posts should be ordered by their author's name
    // Since we have posts A,B,C,D for authors Alice,Bob,Charlie,Diana
    EXPECT_EQ(postsValue[0].title, "Post A");  // Alice's post
    EXPECT_EQ(postsValue[1].title, "Post B");  // Bob's post
    EXPECT_EQ(postsValue[2].title, "Post C");  // Charlie's post
    EXPECT_EQ(postsValue[3].title, "Post D");  // Diana's post
}

TEST_F(ORMTest, OrderByAgeDescending) {
    auto authors = QuerySet<Author>(conn)
        .order_by<&Author::age, false>()
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    
    // Should be ordered by age DESC
    EXPECT_EQ(authorsValue[0].age, 35);  // Bob (highest age)
    EXPECT_EQ(authorsValue[0].name, "Bob Johnson");
    
    EXPECT_EQ(authorsValue[1].age, 30);  // Charlie
    EXPECT_EQ(authorsValue[1].name, "Charlie Brown");
    
    EXPECT_EQ(authorsValue[2].age, 28);  // Diana
    EXPECT_EQ(authorsValue[2].name, "Diana Prince");
    
    EXPECT_EQ(authorsValue[3].age, 25);  // Alice (lowest age)
    EXPECT_EQ(authorsValue[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderByComplexJoinWithMultipleOrderFields) {
    // Add more authors and posts for complex testing
    Author author5("Alice Johnson", 27, "alice.j@example.com");  // Same first name as Alice Smith
    auto author5_id = QuerySet<Author>(conn).insert(author5);
    ASSERT_TRUE(author5_id.has_value()) << "Insert should return a value" << author5_id.error();
    Post post5("Post E", "Content E", author5_id.value());
    auto post5_id = QuerySet<Post>(conn).insert(post5);
    ASSERT_TRUE(post5_id.has_value()) << "Insert should return a value" << post5_id.error();
    
    auto posts = QuerySet<Post>(conn)
        .join<Author>()
        .order_by<&Author::name, true>()
        .order_by<&Author::age, false>()  // Name ASC, Age DESC
        .select_all();
    
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 5);
    auto postsValue = posts.value();
    
    // Should order by author name first, then by age DESC within same name
    // Alice Johnson (27), Alice Smith (25), Bob Johnson (35), Charlie Brown (30), Diana Prince (28)
    
    // First Alice (Johnson, higher age comes first due to DESC)
    EXPECT_EQ(postsValue[0].title, "Post E");
    
    // Second Alice (Smith, lower age)
    EXPECT_EQ(postsValue[1].title, "Post A");
    
    // Then Bob
    EXPECT_EQ(postsValue[2].title, "Post B");
    
    // Then Charlie
    EXPECT_EQ(postsValue[3].title, "Post C");
    
    // Finally Diana
    EXPECT_EQ(postsValue[4].title, "Post D");
}

TEST_F(ORMTest, OrderingWithWhereClause) {
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 30)  // Only Charlie has age 30
        .order_by<&Author::name, true>()
        .select_all();  
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

// =======================================
// WHERE TESTS
// =======================================
TEST_F(ORMTest, WhereClauseEqualsDefault) {
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 30)  // Only Charlie has age 30
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseEqualsOperator) {
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 30)  // Only Charlie has age 30
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseConvenientSyntax) {
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 30)  // Default to EQUALS
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseMultipleConditions) {
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GT)
        .where(&Author::is_active, true)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPSyntax) {
    auto authors = QuerySet<Author>(conn)
        .where<&Author::age>(30)  // NTTP syntax - member pointer as template parameter
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseNTTPStringComparison) {
    auto authors = QuerySet<Author>(conn)
        .where<&Author::name>(std::string("Alice Smith"), Op::EQ)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Alice Smith");
    EXPECT_EQ(authorsValue[0].age, 25);
}

TEST_F(ORMTest, WhereClauseNTTPBooleanValue) {
    auto authors = QuerySet<Author>(conn)
        .where<&Author::is_active>(true)  // Boolean using NTTP
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPWithOperators) {
    // Test NTTP with GREATER_THAN operator
    auto authors = QuerySet<Author>(conn)
        .where<&Author::age>(25, Op::GT)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
    }
}

TEST_F(ORMTest, WhereClauseNTTPLikeOperator) {
    auto authors = QuerySet<Author>(conn)
        .where<&Author::name>(std::string("A%"), Op::LIKE)  // Names starting with 'A'
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_EQ(author.name[0], 'A');
    }
}

TEST_F(ORMTest, WhereClauseNTTPChaining) {
    // Test chaining NTTP where clauses
    auto authors = QuerySet<Author>(conn)
        .where<&Author::age>(20, Op::GE)
        .where<&Author::is_active>(true)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.age, 20);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseMixedSyntax) {
    // Test mixing traditional and NTTP syntax
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GT)  // Traditional syntax
        .where<&Author::is_active>(true)                   // NTTP syntax
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPConstCharPtr) {
    // Test NTTP with const char* (C-style string)
    auto authors = QuerySet<Author>(conn)
        .where<&Author::name>("Charlie Brown")  // const char* literal
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
    EXPECT_EQ(authorsValue[0].age, 30);
}

TEST_F(ORMTest, WhereClauseNTTPWithArithmeticTypes) {
    // Test NTTP with various arithmetic types (now works without SFINAE constraint)
    auto authorsInt = QuerySet<Author>(conn)
        .where<&Author::age>(30)  // int
        .select_all();
    
    auto authorsDouble = QuerySet<Author>(conn)
        .where<&Author::rating>(4.5)  // double
        .select_all();
    
    auto authorsFloat = QuerySet<Author>(conn)
        .where<&Author::score>(85.5f)  // float
        .select_all();
    
    ASSERT_TRUE(authorsInt.has_value()) << "Select should return a value" << authorsInt.error();
    ASSERT_EQ(authorsInt.value().size(), 1);
    auto authorsIntValue = authorsInt.value();
    
    ASSERT_TRUE(authorsDouble.has_value()) << "Select should return a value" << authorsDouble.error();
    ASSERT_EQ(authorsDouble.value().size(), 1);
    auto authorsDoubleValue = authorsDouble.value();
    
    ASSERT_TRUE(authorsFloat.has_value()) << "Select should return a value" << authorsFloat.error();
    ASSERT_EQ(authorsFloat.value().size(), 1);
    auto authorsFloatValue = authorsFloat.value();
}

TEST_F(ORMTest, WhereClauseNTTPComparisonWithTraditional) {
    // Verify NTTP and traditional syntax produce identical results
    auto authorsTraditional = QuerySet<Author>(conn)
        .where(&Author::age, 30, Op::EQ)
        .select_all();
    
    ASSERT_TRUE(authorsTraditional.has_value()) << "Select should return a value" << authorsTraditional.error();
    ASSERT_EQ(authorsTraditional.value().size(), 1);
    auto authorsTraditionalValue = authorsTraditional.value();
    
    auto authorsNTTP = QuerySet<Author>(conn)
        .where<&Author::age>(30, Op::EQ)
        .select_all();
    
    ASSERT_TRUE(authorsNTTP.has_value()) << "Select should return a value" << authorsNTTP.error();
    ASSERT_EQ(authorsTraditional.value().size(), authorsNTTP.value().size());
    ASSERT_EQ(authorsTraditional.value().size(), 1);
    
    // Both should return the same results
    for (size_t i = 0; i < authorsTraditional.value().size(); ++i) {
        EXPECT_EQ(authorsTraditional.value()[i].name, authorsNTTP.value()[i].name);
        EXPECT_EQ(authorsTraditional.value()[i].age, authorsNTTP.value()[i].age);
        EXPECT_EQ(authorsTraditional.value()[i].is_active, authorsNTTP.value()[i].is_active);
    }
}

TEST_F(ORMTest, WhereClauseStringComparison) {
    Author author("John Doe", 30, "john@example.com");
    QuerySet<Author>(conn).insert(author);
    
    Author author2("John Doe Smith", 31, "john.smith@example.com");
    QuerySet<Author>(conn).insert(author2);
    
    auto authors = QuerySet<Author>(conn)
        .where(&Author::name, std::string("John%"), Op::LIKE)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    for (const auto& foundAuthor : authorsValue) {
        EXPECT_TRUE(foundAuthor.name.substr(0, 4) == "John");
    }
}

TEST_F(ORMTest, WhereClauseLessOrEqualOperator) {
    // Test LESS_OR_EQUAL operator
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 30, Op::LE)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_LE(author.age, 30);
    }
}

TEST_F(ORMTest, WhereClauseGreaterOrEqualOperator) {
    // Test GREATER_OR_EQUAL operator  
    auto authors = QuerySet<Author>(conn)
        .where(&Author::age, 25, Op::GE)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.age, 25);
    }
}

TEST_F(ORMTest, WhereClauseNTTPLessOrEqual) {
    // Test LESS_OR_EQUAL with NTTP syntax
    auto authors = QuerySet<Author>(conn)
        .where<&Author::age>(35, Op::LE)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_LE(author.age, 35);
    }
}

TEST_F(ORMTest, WhereClauseNTTPGreaterOrEqual) {
    // Test GREATER_OR_EQUAL with NTTP syntax
    auto authors = QuerySet<Author>(conn)
        .where<&Author::rating>(3.0, Op::GE)
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.rating, 3.0);
    }
}

TEST_F(ORMTest, WhereClauseIsOperator) {
    // TODO: simplify using insert instead raw sql, i need this for now because i dont have mechanism to insert NULL values
    // Insert an author with NULL biography directly using SQL
    auto stmt = Statement(conn, "INSERT INTO author (name, age, email, biography) VALUES (?, ?, ?, NULL)");
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();
    
    // Execute the query using our ORM
    auto authors = QuerySet<Author>(conn)
        .where(&Author::biography, std::nullopt, Op::IS)  // WHERE biography IS NULL
        .select_all();
    
    // Verify we found the author with NULL biography
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "John Doe");
    EXPECT_EQ(authorsValue[0].age, 30);
    EXPECT_EQ(authorsValue[0].email, "john@example.com");
}

TEST_F(ORMTest, WhereClauseNTTPIsOperator) {
    // Test IS operator with NTTP syntax
    // TODO: simplify using insert instead raw sql, i need this for now because i dont have mechanism to insert NULL values
    // Insert an author with NULL biography directly using SQL
    auto stmt = Statement(conn, "INSERT INTO author (name, age, email, biography) VALUES (?, ?, ?, NULL)");
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();
    
    auto authors = QuerySet<Author>(conn)
        .where<&Author::biography>(std::nullopt, Op::IS)  // WHERE biography IS NULL
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "John Doe");
    EXPECT_EQ(authorsValue[0].age, 30);
    EXPECT_EQ(authorsValue[0].email, "john@example.com");
}

TEST_F(ORMTest, WhereClauseOrOperator) {
    // Test: age = 25 OR age = 35 (Alice OR Bob)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or field(&Author::age) == 35) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseAndOperator) {
    // Test: age >= 30 AND rating >= 4.5 (Only Bob - Diana is 28, Charlie is 30 but rating 4.0)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) >= 30 and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Bob Johnson");
}

TEST_F(ORMTest, WhereClauseComplexAndOr) {
    // Test: (age = 25 OR age = 28) AND rating >= 4.5 (Alice AND Diana)
    auto authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 28) and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseComplexOrAnd) {
    // Test: age = 25 OR (age >= 30 AND rating = 5.0) (Alice OR Bob)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or (field(&Author::age) >= 30 and field(&Author::rating) == 5.0)) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue  ) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseTripleAnd) {
    // Test: age >= 25 AND age <= 30 AND rating >= 4.0 (Alice, Charlie, Diana)
    // Alice: 25, 4.5; Charlie: 30, 4.0; Diana: 28, 5.5
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) >= 25 and field(&Author::age) <= 30 and field(&Author::rating) >= 4.0) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseTripleOr) {
    // Test: age = 25 OR age = 30 OR age = 35 (Alice, Charlie, Bob)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) == 25 or field(&Author::age) == 30 or field(&Author::age) == 35) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseNestedComplexGrouping) {
    // Test: (age = 25 OR age = 35) AND (rating >= 4.5 OR score >= 90.0)
    // Should match: Alice (25, 4.5) and Bob (35, 5.0, 90.0)
    auto authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 35) and // NOSONAR
               (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0)) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseComplexNestedConditions) {
    // Test: ((age < 30 AND rating > 4.0) OR (age >= 30 AND score >= 85.0)) AND is_active = true
    // Should match: Alice (25, 4.5, active), Bob (35, 90.0, active), Diana (28, 5.5, 95.0, active)
    auto authors = QuerySet<Author>(conn)
        .where(((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                (field(&Author::age) >= 30 and field(&Author::score) >= 85.0)) and field(&Author::is_active) == true) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseMixedDataTypes) {
    // Test with different data types: string, int, double, bool
    // Test: name LIKE 'Alice%' OR (age > 30 AND rating >= 4.0 AND is_active = true)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::name).like("Alice%") or // NOSONAR
               (field(&Author::age) > 30 and field(&Author::rating) >= 4.0 and field(&Author::is_active) == true)) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseGroupMethod) {
    // Test using the group() method for explicit grouping
    // Test: (age = 25 OR age = 28) AND rating >= 4.5
    auto authors = QuerySet<Author>(conn)
        .where((field(&Author::age) == 25 or field(&Author::age) == 28) and field(&Author::rating) >= 4.5) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseStringOperations) {
    // Test string operations with complex conditions
    // Test: (name LIKE '%Brown' OR email LIKE '%alice%') AND age >= 25
    auto authors = QuerySet<Author>(conn)    
        .where((field(&Author::name).like("%Brown") or field(&Author::email).like("%alice%")) and field(&Author::age) >= 25) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
}

TEST_F(ORMTest, WhereClauseFloatComparisons) {
    // Test float/double comparisons with complex logic
    // Test: (score >= 85.0 AND score <= 90.0) OR rating > 5.0
    // Should match: Alice (85.5), Bob (90.0), Diana (95.0, 5.5)
    auto authors = QuerySet<Author>(conn)
        .where((field(&Author::score) >= 85.0 and field(&Author::score) <= 90.0) or field(&Author::rating) > 5.0) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
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
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) != 30 and (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0)) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
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
    auto authors = QuerySet<Author>(conn)
        .where((field(&Author::age) >= 25 and field(&Author::age) <= 30) or // NOSONAR
               (field(&Author::rating) >= 5.0 and field(&Author::score) >= 90.0) or // NOSONAR
               field(&Author::name).like("%Prince"))
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4); // All authors should match
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
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
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::is_active) == true and // NOSONAR
               ((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                field(&Author::score) >= 95.0))
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseEmptyResult) {
    // Test condition that should return no results
    // Test: age > 100 OR (rating < 0 AND is_active = false)
    auto authors = QuerySet<Author>(conn)
        .where(field(&Author::age) > 100 or // NOSONAR
               (field(&Author::rating) < 0 and field(&Author::is_active) == false)) // NOSONAR
        .select_all();
    
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 0);
}

// =======================================
// ONLY TESTS
// =======================================
TEST_F(ORMTest, SelectOnlySpecificFields) {
    // Test selecting only specific fields from the model using NTTP
    // Execute the query using method chaining
    auto authors = QuerySet<Author>(conn)
        .only<&Author::name, &Author::age, &Author::email>()
        .select_all();
    
    // Verify results
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4); // Should return all 4 authors
    
    // Check that we have the expected fields populated
    for (const auto& author : authors.value()) {
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
    auto authors = QuerySet<Author>(conn)
        .only<&Author::name>("author_name")
        .only<&Author::age>("author_age")
        .select_all();
    
    // Verify results
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4); // Should return all 4 authors
    
    // Check that we have the expected fields populated
    for (const auto& author : authors.value()) {
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
    auto result = QuerySet<Author>(conn)
        .only<&Author::name, &Author::age, &Author::is_active>()
        .select_values();
    
    // Check if we got a valid result
    ASSERT_TRUE(result.has_value()) << "select_values failed with error: " << result.error();
    
    // Get the actual values from the expected
    const auto& values = result.value();
    
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

// =======================================
// DISTINCT TESTS
// =======================================
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
    
    // Check if we got a valid result
    ASSERT_TRUE(distinctAgeValues.has_value()) << "select_values failed with error: " << distinctAgeValues.error();

    ASSERT_TRUE(allAuthors.has_value()) << "select_all failed with error: " << allAuthors.error();
    // We should have 5 total authors (Alice, Bob, Charlie, Diana, Frank)
    ASSERT_EQ(allAuthors.value().size(), 5);

    // With our implementation, we should get 4 distinct ages
    ASSERT_EQ(distinctAgeValues.value().size(), 4);
    
    // Create a set of all ages to verify all ages are present
    std::set<int> allAgesSet;
    for (const auto& author : allAuthors.value()) {
        allAgesSet.insert(author.age);
    }
    
    // Extract the ages from the values and convert to a set
    std::set<int> distinctAgesSet;
    for (const auto& row : distinctAgeValues.value()) {
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
    for (const auto& author : allAuthors.value()) {
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
    
    // Check if we got a valid result
    ASSERT_TRUE(distinctValues.has_value()) << "select_values failed with error: " << distinctValues.error();
    
    // Count distinct combinations of age and rating
    std::set<std::pair<int, double>> distinctCombinations;
    for (const auto& row : distinctValues.value()) {
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
    
    // Check if we got a valid result
    ASSERT_TRUE(distinctValues.has_value()) << "select_values failed with error: " << distinctValues.error();
    
    // When using distinct with only, we get distinct combinations of the fields
    // So we'll have 7 rows (all unique combinations of age and name)
    ASSERT_EQ(distinctValues.value().size(), 7);
    
    // Each row should contain only age and name fields
    for (const auto& row : distinctValues.value()) {
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
    for (const auto& row : distinctValues.value()) {
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
    
    // Check if we got a valid result
    ASSERT_TRUE(authorResults.has_value()) << "select_values failed with error: " << authorResults.error();
    
    // We should have at least 3 distinct ages (35, 40, 45) that are >= 35
    // Note: Other tests may have added additional authors with ages >= 35
    ASSERT_GE(authorResults.value().size(), 3);
    
    // Create a set of ages to verify uniqueness
    std::set<int> distinctAges;
    for (const auto& result : authorResults.value()) {
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

// =======================================
// RAW SQL TESTS
// =======================================
TEST_F(ORMTest, RawSqlFromStatementInsert) {
    // Create a new author for testing
    Author testAuthor("Test Author", 40, "test@example.com");
    
    // Get the Statement object without executing it
    auto stmt = QuerySet<Author>(conn).stmt_insert(testAuthor);
    
    // Verify that we can get the raw SQL
    std::string rawSql = stmt.sql();
    
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
    auto authorId = QuerySet<Author>(conn).insert(testAuthor);
    ASSERT_TRUE(authorId.has_value()) << "insert failed with error: " << authorId.error();
    ASSERT_GT(authorId.value(), 0);

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
    ASSERT_TRUE(ids.has_value()) << "insert failed with error: " << ids.error();
    ASSERT_EQ(ids.value().size(), 2);
    ASSERT_GT(ids.value()[0], 0);
    ASSERT_GT(ids.value()[1], 0);
}

// =======================================
// GROUP BY TESTS
// =======================================

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
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 5);
    
    // Verify the ages are present in the results
    std::set<int> distinctAges;
    for (const auto& author : results.value()) {
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
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_GE(results.value().size(), 7);
    
    // Create a set of age-rating pairs to verify unique combinations
    std::set<std::pair<int, double>> ageRatingPairs;
    for (const auto& author : results.value()) {
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
    for (const auto& author : results.value()) {
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
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 3);
    
    // Extract author IDs directly from Author objects
    std::set<int> authorIds;
    for (const auto& post : results.value()) {
        authorIds.insert(post.author_id);
    }
    
    // Verify the correct author IDs are present
    ASSERT_TRUE(authorIds.contains(bob_id));
    ASSERT_TRUE(authorIds.contains(charlie_id));
    ASSERT_TRUE(authorIds.contains(diana_id));
    ASSERT_FALSE(authorIds.contains(alice_id)); // Alice's age is 25, not > 25
}

// =======================================
// TRANSACTION TESTS
// =======================================

TEST_F(ORMTest, BasicTransactionCommit) {
    // Clear existing test data
    clearTestData();
    
    // Start a transaction
    conn->begin_transaction();
    
    // Insert an author within the transaction
    Author alice("Alice Smith", 25, "alice@example.com");
    auto alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
    
    // Commit the transaction
    conn->commit();
    
    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
    ASSERT_EQ(results.value()[0].id, alice_id.value());
}

TEST_F(ORMTest, BasicTransactionRollback) {
    // Clear existing test data
    clearTestData();
    
    // Start a transaction
    conn->begin_transaction();
    
    // Insert an author within the transaction
    Author alice("Alice Smith", 25, "alice@example.com");
    auto alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
    
    // Rollback the transaction
    conn->rollback();
    
    // Verify the author was not inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 0); // No authors should exist after rollback
}

TEST_F(ORMTest, TransactionLevels) {
    // Clear existing test data
    clearTestData();
    
    // Test different transaction isolation levels
    conn->begin_transaction(Connection::TransactionLevel::IMMEDIATE);
    
    Author alice("Alice Smith", 25, "alice@example.com");
    auto alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
    
    conn->commit();
    
    // Start another transaction with EXCLUSIVE level
    conn->begin_transaction(Connection::TransactionLevel::EXCLUSIVE);
    
    Author bob("Bob Johnson", 30, "bob@example.com");
    auto bob_id = QuerySet<Author>(conn).insert(bob);
    ASSERT_TRUE(bob_id.has_value()) << "query failed with error: " << bob_id.error();
    
    conn->commit();
    
    // Verify both authors were inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 2);
}

TEST_F(ORMTest, NestedTransactionException) {
    // Clear existing test data
    clearTestData();
    
    // Start a transaction
    conn->begin_transaction();
    
    // Attempt to start another transaction while one is active
    ASSERT_THROW(conn->begin_transaction(), TransactionAlreadyActiveException);
    
    // Clean up
    conn->rollback();
}

TEST_F(ORMTest, CommitWithoutTransactionException) {
    // Clear existing test data
    clearTestData();
    
    // Attempt to commit without an active transaction
    ASSERT_THROW(conn->commit(), TransactionNotActiveException);
}

TEST_F(ORMTest, RollbackWithoutTransactionException) {
    // Clear existing test data
    clearTestData();
    
    // Attempt to rollback without an active transaction
    ASSERT_THROW(conn->rollback(), TransactionNotActiveException);
}

TEST_F(ORMTest, TransactionRAIIWrapper) {
    // Clear existing test data
    clearTestData();
    
    // Test the RAII transaction wrapper
    {
        Transaction tx(conn);
        
        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
        
        // Transaction will be rolled back when it goes out of scope
        // because we don't call commit()
    }
    
    // Verify the author was not inserted (transaction was rolled back)
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 0); // No authors should exist after rollback
}

TEST_F(ORMTest, TransactionRAIICommit) {
    // Clear existing test data
    clearTestData();
    
    // Use the RAII Transaction wrapper with explicit commit
    {
        Transaction tx(conn);
        
        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
        
        // Explicitly commit the transaction
        tx.commit();
    }
    
    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
}

TEST_F(ORMTest, TransactionRAIIRollback) {
    // Clear existing test data
    clearTestData();
    
    // Test the RAII transaction wrapper with explicit rollback
    {
        Transaction tx(conn);
        
        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
        
        // Explicitly rollback the transaction
        tx.rollback();
    }
    
    // Verify the author was not inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 0); // No authors should exist after rollback
}

TEST_F(ORMTest, WithTransactionHelper) {
    // Clear existing test data
    clearTestData();
    
    // Test the with_transaction helper function
    bool success = with_transaction(conn, [this]() -> bool {
        Author alice("Alice Smith", 25, "alice@example.com");
        auto alice_id = QuerySet<Author>(this->conn).insert(alice);
        EXPECT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
        return true;
    }); 
    
    ASSERT_TRUE(success);
    
    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
}

TEST_F(ORMTest, WithTransactionExceptionRollback) {
    // Clear existing test data
    clearTestData();
    
    // Test that with_transaction rolls back on exception
    try {
        with_transaction(conn, [this]() {
            Author alice("Alice Smith", 25, "alice@example.com");
            auto alice_id = QuerySet<Author>(this->conn).insert(alice);
            ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
            
            // Throw an exception to trigger rollback
            throw std::runtime_error("Test exception");
        });
        
        FAIL() << "Expected exception was not thrown";
    } catch (const std::runtime_error& e) {
        ASSERT_STREQ(e.what(), "Test exception");
    }
    
    // Verify the author was not inserted (transaction was rolled back)
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 0);
}

TEST_F(ORMTest, MultipleOperationsInTransaction) {
    // Clear existing test data
    clearTestData();
    
    // Test multiple database operations in a single transaction
    conn->begin_transaction();
    
    // Insert multiple authors
    Author alice("Alice Smith", 25, "alice@example.com");
    Author bob("Bob Johnson", 30, "bob@example.com");
    Author charlie("Charlie Brown", 35, "charlie@example.com");
    
    auto alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
    auto bob_id = QuerySet<Author>(conn).insert(bob);
    ASSERT_TRUE(bob_id.has_value()) << "query failed with error: " << bob_id.error();
    auto charlie_id = QuerySet<Author>(conn).insert(charlie);
    ASSERT_TRUE(charlie_id.has_value()) << "query failed with error: " << charlie_id.error();
    
    // Update an author - first retrieve the current object with the correct ID
    auto alice_from_db = QuerySet<Author>(conn).where(&Author::id, alice_id).select_one();
    ASSERT_TRUE(alice_from_db.has_value()) << "query failed with error: " << alice_from_db.error();
    alice_from_db.value().age = 26;
    
    // Delete an author
    QuerySet<Author>(conn).where(&Author::name, "Charlie Brown").remove();
    
    conn->commit();
    
    // Verify the final state
    auto results = QuerySet<Author>(conn).order_by<&Author::name>().select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 2);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
    ASSERT_EQ(results.value()[0].age, 26);
    ASSERT_EQ(results.value()[1].name, "Bob Johnson");
}

TEST_F(ORMTest, TransferBetweenAuthorsTransaction) {
    // This test is different as it requires the initial test data
    // We don't clear the data here because we need alice_id and bob_id from setupTestData
    
    // First, let's create a new post for Alice
    Post alicePost("Alice's Original Post", "This post belongs to Alice", alice_id, 0, 100);
    auto postId = QuerySet<Post>(conn).insert(alicePost);
    ASSERT_TRUE(postId.has_value()) << "query failed with error: " << postId.error();
    
    // Now let's transfer the post from Alice to Bob using a transaction
    // to ensure the operation is atomic
    {
        Transaction tx(conn);
        
        try {
            // 1. Update the post to belong to Bob
            auto post = QuerySet<Post>(conn).where(&Post::id, postId).select_one();
            ASSERT_TRUE(post.has_value()) << "query failed with error: " << post.error();
            auto postValue = post.value();
            postValue.author_id = bob_id;
            QuerySet<Post>(conn).where(&Post::id, postId).update(postValue);
            
            // 2. Update some metadata about the post (title to reflect new ownership)
            postValue.title = "Post Transferred to Bob";
            QuerySet<Post>(conn).where(&Post::id, postId).update(postValue);
            
            // Commit the transaction - both operations succeed atomically
            tx.commit();
        } catch (const std::exception& e) {
            // Transaction will automatically roll back if not committed
            std::cerr << "Error during post transfer: " << e.what() << std::endl;
            FAIL() << "Transaction should not have failed";
        }
    }
    
    // Verify the post now belongs to Bob
    auto updatedPost = QuerySet<Post>(conn).where(&Post::id, postId).select_one();
    ASSERT_TRUE(updatedPost.has_value()) << "query failed with error: " << updatedPost.error();
    ASSERT_EQ(updatedPost.value().author_id, bob_id);
    ASSERT_EQ(updatedPost.value().title, "Post Transferred to Bob");
    
    // Demonstrate a failed transaction with automatic rollback
    {
        Transaction tx(conn);
        
        // 1. Update the post to belong to Charlie
        auto post = QuerySet<Post>(conn).where(&Post::id, postId).select_one();
        ASSERT_TRUE(post.has_value()) << "query failed with error: " << post.error();
        auto postValue = post.value();
        postValue.author_id = charlie_id;
        QuerySet<Post>(conn).where(&Post::id, postId).update(postValue);
        
        // 2. This update will fail because we're using a non-existent author ID
        // (deliberately causing an error to demonstrate rollback)
        postValue.author_id = 9999; // Non-existent author ID
        
        // We don't commit, so the transaction will be rolled back
        // when it goes out of scope
    }
    
    // Verify the post still belongs to Bob (rollback worked)
    updatedPost = QuerySet<Post>(conn).where(&Post::id, postId).select_one();
    ASSERT_TRUE(updatedPost.has_value()) << "query failed with error: " << updatedPost.error();
    ASSERT_EQ(updatedPost.value().author_id, bob_id); // Still Bob's post
}

// =======================================
// MAX AGGREGATE FUNCTION TESTS
// =======================================

TEST_F(ORMTest, MaxStringField) {
    // Test MAX function with string field (alphabetically highest value)
    auto result = QuerySet<Author>(conn)
        .max<&Author::name>("max_name")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"max_name", "Diana Prince"}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxOnEmptyTable) {
    // Clear all authors
    auto remove_result = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(remove_result);
    
    // Test MAX function on an empty table
    auto result = QuerySet<Author>(conn)
        .max<&Author::age>("max_age")
        .select_values();

    ValueVectorMap expected_results = {
        {
            {"max_age", 0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxWithOrderBy) {
    // Test MAX function with ORDER BY
    // The ORDER BY shouldn't affect the MAX result but should affect the order of rows
    auto result = QuerySet<Author>(conn)
        .max<&Author::age>("max_age")
        .order_by<&Author::name, false>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"max_age", 35}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleMaxFunctions) {
    // Test multiple MAX functions in the same query
    auto result = QuerySet<Author>(conn)
        .max<&Author::age>("max_age")
        .max<&Author::rating>("max_rating")
        .max<&Author::score>("max_score")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"max_age", 35},
            {"max_rating", 5.0},
            {"max_score", 100.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxWithJoin) {
    // Test MAX function with a JOIN
    // This will find the maximum post ID for each author
    // We need to include the name field in the result set when using group_by with aggregate functions
    auto result = QuerySet<Post>(conn)
        .join<Author>()
        .group_by<&Author::name>()
        .only<&Author::name>()
        .max<&Post::id>("max_post_id")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"name", "Alice Smith"},
            {"max_post_id", 3}
        },
        {
            {"name", "Bob Johnson"},
            {"max_post_id", 5}
        },
        {
            {"name", "Charlie Brown"},
            {"max_post_id", 9}
        },
        {
            {"name", "Diana Prince"},
            {"max_post_id", 11}
        }
    };
    AssertResultsMatch(result, expected_results);
}

// =======================================
// MIN AGGREGATE FUNCTION TESTS
// =======================================

TEST_F(ORMTest, MinStringField) {
    auto result = QuerySet<Author>(conn)
        .min<&Author::name>("min_name")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"min_name", "Alice Smith"}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinOnEmptyTable) {
    auto clear_authors = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(clear_authors);
    
    auto result = QuerySet<Author>(conn)
        .min<&Author::age>("min_age")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"min_age", 0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinWithOrderBy) {
    auto result = QuerySet<Author>(conn)
        .min<&Author::age>("min_age")
        .order_by<&Author::name, false>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"min_age", 35}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleMinFunctions) {
    auto result = QuerySet<Author>(conn)
        .min<&Author::age>("min_age")
        .min<&Author::rating>("min_rating")
        .min<&Author::score>("min_score")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"min_age", 0},
            {"min_rating", 0.0},
            {"min_score", 0.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinWithJoin) {
    auto result = QuerySet<Author>(conn)
        .join<Post>()
        .min<&Author::age>("min_age")
        .group_by<&Post::author_id>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"min_age", 25},
        },
        {
            {"min_age", 28},
        },
        {
            {"min_age", 30},
        },
        {
            {"min_age", 35},
        }
    };
    AssertResultsMatch(result, expected_results);
}


// =======================================
// AVG AGGREGATE FUNCTION TESTS
// =======================================

TEST_F(ORMTest, AvgValueInt) {
    auto result = QuerySet<Author>(conn)
        .avg_value<&Author::age>();
    ASSERT_TRUE(result.has_value()) << "avg failed with error: " << result.error();
    ASSERT_EQ(result.value(), 25.0);
}

TEST_F(ORMTest, AvgValueWithFilter) {
    auto result = QuerySet<Author>(conn)
        .where<&Author::age>(20, Op::GT)
        .avg_value<&Author::age>();
    ASSERT_TRUE(result.has_value()) << "avg with filter failed with error: " << result.error();
    ASSERT_EQ(result.value(), 30.0); // Average of authors with age > 20
}

TEST_F(ORMTest, AvgValueFloat) {
    auto result = QuerySet<Author>(conn)
        .avg_value<&Author::rating>();
    ASSERT_TRUE(result.has_value()) << "avg of float failed with error: " << result.error();
    ASSERT_EQ(result.value(), 4.33);
}

TEST_F(ORMTest, AvgValueNoRows) {
    clearTestData();
    
    auto result = QuerySet<Author>(conn)
        .avg_value<&Author::age>();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), "No rows to average");
}

TEST_F(ORMTest, AvgValueWithGroupBy) {
    auto result = QuerySet<Author>(conn)
        .group_by<&Author::age>()
        .avg_value<&Author::rating>();
    ASSERT_TRUE(result.has_value()) << "avg with group by failed with error: " << result.error();
    ASSERT_EQ(result.value(), 4.33);
}

TEST_F(ORMTest, AvgNumericField) {
    auto result = QuerySet<Author>(conn)
        .avg<&Author::age>("avg_age")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"avg_age", 25.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgOnEmptyTable) {
    auto remove_result = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(remove_result);
    
    auto result = QuerySet<Author>(conn)
        .avg<&Author::age>("avg_age")
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"avg_age", 0.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgWithOrderBy) {
    auto result = QuerySet<Author>(conn)
        .avg<&Author::age>("avg_age")
        .order_by<&Author::name, false>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"avg_age", 25.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleAvgFunctions) {
    auto result = QuerySet<Author>(conn)
        .avg<&Author::rating>("avg_rating")
        .avg<&Author::score>("avg_score")
        .group_by<&Author::age>()
        .only<&Author::age>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"age", 25},
            {"avg_rating", 4.5},
            {"avg_score", 85.5}
        },
        {
            {"age", 28},
            {"avg_rating", 5.5},
            {"avg_score", 95.0}
        },
        {
            {"age", 30},
            {"avg_rating", 4.0},
            {"avg_score", 80.0}
        },
        {
            {"age", 35},
            {"avg_rating", 5.0},
            {"avg_score", 90.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgWithGroupBy) {
    auto result = QuerySet<Author>(conn)
        .avg<&Author::rating>("avg_rating")
        .group_by<&Author::age>()
        .only<&Author::age>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"age", 25},
            {"avg_rating", 4.5}
        },
        {
            {"age", 28},
            {"avg_rating", 5.5}
        },
        {
            {"age", 30},
            {"avg_rating", 4.0}
        },
        {
            {"age", 35},
            {"avg_rating", 5.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, CombineAvgMinMaxWithGroupBy) {
    auto result = QuerySet<Author>(conn)
        .avg<&Author::rating>("avg_rating")
        .min<&Author::rating>("min_rating")
        .max<&Author::rating>("max_rating")
        .group_by<&Author::age>()
        .only<&Author::age>()
        .select_values();
    
    ValueVectorMap expected_results = {
        {
            {"age", 25},
            {"avg_rating", 4.5},
            {"min_rating", 4.5},
            {"max_rating", 4.5}
        },
        {
            {"age", 28},
            {"avg_rating", 5.5},
            {"min_rating", 5.5},
            {"max_rating", 5.5}
        },
        {
            {"age", 30},
            {"avg_rating", 4.0},
            {"min_rating", 4.0},
            {"max_rating", 4.0}
        },
        {
            {"age", 35},
            {"avg_rating", 5.0},
            {"min_rating", 5.0},
            {"max_rating", 5.0}
        }
    };
    AssertResultsMatch(result, expected_results);
}
