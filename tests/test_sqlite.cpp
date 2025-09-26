#include <gtest/gtest.h>
#include <sqlite3.h>

import storm;
import <expected>;
import <string>;
import <optional>;

// Test struct with proper Storm attribute syntax
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Test QuerySet.remove() functionality
class QuerySetRemoveTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up default connection using QuerySet
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to set default connection: " << result.error().message();

        // Create test table using the default connection
        auto& default_conn  = storm::QuerySet<Person>::get_default_connection();
        auto  create_result = default_conn.execute(
                "CREATE TABLE Person ("
                 "id INTEGER PRIMARY KEY, "
                 "name TEXT NOT NULL, "
                 "age INTEGER NOT NULL"
                 ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert test data
        auto insert_result = default_conn.execute(
                "INSERT INTO Person (id, name, age) VALUES "
                "(1, 'Alice', 30), "
                "(2, 'Bob', 25), "
                "(3, 'Charlie', 35)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
    }

    void TearDown() override {
        // Clear all connections
        storm::QuerySet<Person>::clear_default_connection();
    }

    // Helper function to count records using the default connection
    int countPersons() {
        auto&         conn = storm::QuerySet<Person>::get_default_connection();
        sqlite3_stmt* stmt;
        int           rc = sqlite3_prepare_v2(conn.get(), "SELECT COUNT(*) FROM Person", -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return -1;

        rc        = sqlite3_step(stmt);
        int count = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        return count;
    }

    // Helper function to check if person exists using the default connection
    bool personExists(int id) {
        auto&         conn = storm::QuerySet<Person>::get_default_connection();
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(conn.get(), "SELECT COUNT(*) FROM Person WHERE id = ?", -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return false;

        sqlite3_bind_int(stmt, 1, id);
        rc          = sqlite3_step(stmt);
        bool exists = (rc == SQLITE_ROW) && (sqlite3_column_int(stmt, 0) > 0);

        sqlite3_finalize(stmt);
        return exists;
    }
};

TEST_F(QuerySetRemoveTest, DatabaseSetup) {
    // Verify database was created and populated correctly
    EXPECT_TRUE(storm::QuerySet<Person>::has_default_connection()) << "Should have default connection";
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons in database";

    // Verify specific persons exist
    EXPECT_TRUE(personExists(1)) << "Alice should exist";
    EXPECT_TRUE(personExists(2)) << "Bob should exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should exist";
}

TEST_F(QuerySetRemoveTest, RemoveExistingPerson) {
    // Create QuerySet using simplified syntax - no explicit connection needed!
    auto queryset = storm::QuerySet<Person>{};

    // Create person object to remove (Alice with id=1)
    Person alice{1, "Alice", 30};

    // Verify Alice exists before removal
    EXPECT_TRUE(personExists(1)) << "Alice should exist before removal";
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons before removal";

    // Remove Alice using QuerySet.remove()
    auto result = queryset.remove(alice);

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Remove operation should succeed for existing person";
    EXPECT_TRUE(result.value()) << "Remove should return true for successful operation";

    // Verify Alice no longer exists
    EXPECT_FALSE(personExists(1)) << "Alice should not exist after removal";
    EXPECT_EQ(countPersons(), 2) << "Should have 2 persons after removal";

    // Verify other persons still exist
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveNonExistentPerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create person object that doesn't exist in database
    Person nonexistent{999, "NonExistent", 99};

    // Verify person doesn't exist
    EXPECT_FALSE(personExists(999)) << "Person with id 999 should not exist";
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons before removal attempt";

    // Attempt to remove non-existent person using QuerySet.remove()
    auto result = queryset.remove(nonexistent);

    // Verify operation completes (SQLite DELETE with no matching rows is not an error)
    ASSERT_TRUE(result.has_value()) << "Remove operation should not error for non-existent person";

    // Verify database state unchanged
    EXPECT_EQ(countPersons(), 3) << "Should still have 3 persons after removal attempt";
    EXPECT_TRUE(personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveMultiplePersonsSequentially) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create person objects to remove
    Person alice{1, "Alice", 30};
    Person bob{2, "Bob", 25};

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Remove Alice using QuerySet.remove()
    auto result1 = queryset.remove(alice);
    ASSERT_TRUE(result1.has_value()) << "First remove should succeed";
    EXPECT_TRUE(result1.value()) << "First remove should return true";
    EXPECT_EQ(countPersons(), 2) << "Should have 2 persons after first removal";

    // Remove Bob using QuerySet.remove()
    auto result2 = queryset.remove(bob);
    ASSERT_TRUE(result2.has_value()) << "Second remove should succeed";
    EXPECT_TRUE(result2.value()) << "Second remove should return true";
    EXPECT_EQ(countPersons(), 1) << "Should have 1 person after second removal";

    // Verify only Charlie remains
    EXPECT_FALSE(personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(personExists(2)) << "Bob should be removed";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveWithZeroId) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create person object with id 0 (which doesn't exist)
    Person zero_person{0, "Zero", 0};

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Attempt to remove person with id 0 using QuerySet.remove()
    auto result = queryset.remove(zero_person);

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Remove operation should complete for id 0";

    // Verify database state unchanged
    EXPECT_EQ(countPersons(), 3) << "Should still have 3 persons after removal attempt";
}
