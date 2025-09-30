#include <gtest/gtest.h>
#include <sqlite3.h>

import storm;
import <expected>;
import <string>;
import <optional>;
import <span>;
import <chrono>;
import <iostream>;
import <iomanip>;

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
    // No need to check result.value() for void std::expected - success is indicated by has_value()

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
    // No need to check result1.value() for void std::expected - success is indicated by has_value()
    EXPECT_EQ(countPersons(), 2) << "Should have 2 persons after first removal";

    // Remove Bob using QuerySet.remove()
    auto result2 = queryset.remove(bob);
    ASSERT_TRUE(result2.has_value()) << "Second remove should succeed";
    // No need to check result2.value() for void std::expected - success is indicated by has_value()
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

TEST_F(QuerySetRemoveTest, RemoveBatchSmall) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Add more test data for batch testing
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    for (int i = 4; i <= 12; i++) {
        auto insert_result = conn.execute(
                "INSERT INTO Person (id, name, age) VALUES (" + std::to_string(i) + ", 'Person" + std::to_string(i) +
                "', " + std::to_string(20 + i) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 12 persons (3 original + 9 new)
    EXPECT_EQ(countPersons(), 12) << "Should have 12 persons initially";

    // Create batch of persons to remove (batch size ~10)
    std::vector<Person> batch_to_remove;
    for (int i = 1; i <= 10; i++) {
        batch_to_remove.push_back({i, "Person" + std::to_string(i), 20 + i});
    }

    // Remove batch using new batch API
    auto result = queryset.remove(std::span<const Person>(batch_to_remove));

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Batch remove operation should succeed";

    // Verify correct number of persons removed
    EXPECT_EQ(countPersons(), 2) << "Should have 2 persons after batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 10; i++) {
        EXPECT_FALSE(personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(personExists(11)) << "Person 11 should still exist";
    EXPECT_TRUE(personExists(12)) << "Person 12 should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchLarge) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Add many test records for large batch testing
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    for (int i = 4; i <= 103; i++) {
        auto insert_result = conn.execute(
                "INSERT INTO Person (id, name, age) VALUES (" + std::to_string(i) + ", 'Person" + std::to_string(i) +
                "', " + std::to_string(20 + (i % 60)) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 103 persons (3 original + 100 new)
    EXPECT_EQ(countPersons(), 103) << "Should have 103 persons initially";

    // Create large batch of persons to remove (batch size ~100)
    std::vector<Person> large_batch;
    for (int i = 1; i <= 100; i++) {
        large_batch.push_back({i, "Person" + std::to_string(i), 20 + (i % 60)});
    }

    // Remove large batch - should use individual statements with transaction
    auto result = queryset.remove(std::span<const Person>(large_batch));

    // Verify removal was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << std::endl;
    }
    ASSERT_TRUE(result.has_value()) << "Large batch remove operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons removed
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons after large batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 100; i++) {
        EXPECT_FALSE(personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(personExists(101)) << "Person 101 should still exist";
    EXPECT_TRUE(personExists(102)) << "Person 102 should still exist";
    EXPECT_TRUE(personExists(103)) << "Person 103 should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchEmpty) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Create empty batch
    std::vector<Person> empty_batch;

    // Attempt to remove empty batch
    auto result = queryset.remove(std::span<const Person>(empty_batch));

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Empty batch remove should not error";

    // Verify database state unchanged
    EXPECT_EQ(countPersons(), 3) << "Should still have 3 persons after empty batch removal";
    EXPECT_TRUE(personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchPartialExist) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Create batch with mix of existing and non-existing persons
    std::vector<Person> mixed_batch = {
            {1, "Alice", 30},      // exists
            {999, "Ghost1", 99},   // doesn't exist
            {2, "Bob", 25},        // exists
            {1000, "Ghost2", 100}, // doesn't exist
            {3, "Charlie", 35}     // exists
    };

    // Remove mixed batch
    auto result = queryset.remove(std::span<const Person>(mixed_batch));

    // Verify operation completes successfully (non-existing deletes are not errors)
    ASSERT_TRUE(result.has_value()) << "Mixed batch remove should succeed";

    // Verify only existing persons were removed
    EXPECT_EQ(countPersons(), 0) << "All existing persons should be removed";
    EXPECT_FALSE(personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(personExists(2)) << "Bob should be removed";
    EXPECT_FALSE(personExists(3)) << "Charlie should be removed";
}

TEST_F(QuerySetRemoveTest, RemoveBatchPerformance) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Add test data for performance comparison
    auto&     conn        = storm::QuerySet<Person>::get_default_connection();
    const int num_records = 1000;
    for (int i = 4; i <= num_records; i++) {
        auto insert_result = conn.execute(
                "INSERT INTO Person (id, name, age) VALUES (" + std::to_string(i) + ", 'Person" + std::to_string(i) +
                "', " + std::to_string(20 + (i % 60)) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Measure individual removes
    auto start_individual = std::chrono::steady_clock::now();
    for (int i = 1; i <= 50; i++) {
        Person p{i, "Person" + std::to_string(i), 20 + (i % 60)};
        auto   result = queryset.remove(p);
        ASSERT_TRUE(result.has_value()) << "Individual remove should succeed";
    }
    auto end_individual = std::chrono::steady_clock::now();
    auto duration_individual =
            std::chrono::duration_cast<std::chrono::microseconds>(end_individual - start_individual).count();

    // Prepare batch for batch remove
    std::vector<Person> batch;
    for (int i = 51; i <= 100; i++) {
        batch.push_back({i, "Person" + std::to_string(i), 20 + (i % 60)});
    }

    // Measure batch remove
    auto start_batch = std::chrono::steady_clock::now();
    auto result      = queryset.remove(std::span<const Person>(batch));
    ASSERT_TRUE(result.has_value()) << "Batch remove should succeed";
    auto end_batch      = std::chrono::steady_clock::now();
    auto duration_batch = std::chrono::duration_cast<std::chrono::microseconds>(end_batch - start_batch).count();

    // Log performance comparison (batch should be faster)
    std::cout << "\nPerformance Comparison (50 deletes):" << std::endl;
    std::cout << "  Individual removes: " << duration_individual << " μs" << std::endl;
    std::cout << "  Batch remove: " << duration_batch << " μs" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2)
              << static_cast<double>(duration_individual) / duration_batch << "x" << std::endl;

    // Verify correct deletions
    EXPECT_EQ(countPersons(), num_records - 100) << "Should have correct number of persons after removes";
}

// Simple insert test
TEST_F(QuerySetRemoveTest, InsertSinglePerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create person object to insert
    Person dave{4, "Dave", 40};

    // Verify Dave doesn't exist initially
    EXPECT_FALSE(personExists(4)) << "Dave should not exist initially";
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Insert Dave using QuerySet.insert()
    auto result = queryset.insert(dave);

    // Verify insertion was successful
    ASSERT_TRUE(result.has_value()) << "Insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify Dave now exists in database
    EXPECT_TRUE(personExists(4)) << "Dave should exist after insertion";
    EXPECT_EQ(countPersons(), 4) << "Should have 4 persons after insertion";
}

// Bulk insert tests to verify compile-time SQL prefix optimization
TEST_F(QuerySetRemoveTest, InsertSmallBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create a small batch (should use bulk INSERT with VALUES)
    std::vector<Person> small_batch = {{4, "Dave", 40}, {5, "Eve", 35}, {6, "Frank", 45}};

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(small_batch));

    // Verify batch insertion was successful
    ASSERT_TRUE(result.has_value()) << "Batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countPersons(), 6) << "Should have 6 persons after batch insertion";
    EXPECT_TRUE(personExists(4)) << "Dave should exist after batch insertion";
    EXPECT_TRUE(personExists(5)) << "Eve should exist after batch insertion";
    EXPECT_TRUE(personExists(6)) << "Frank should exist after batch insertion";
}

TEST_F(QuerySetRemoveTest, InsertMediumBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create a medium batch (25 objects - should still use bulk INSERT)
    std::vector<Person> medium_batch;
    for (int i = 4; i <= 28; ++i) {
        medium_batch.push_back({i, "Person" + std::to_string(i), 20 + (i % 30)});
    }

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(medium_batch));

    // Verify batch insertion was successful
    ASSERT_TRUE(result.has_value()) << "Medium batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countPersons(), 28) << "Should have 28 persons after medium batch insertion";
    EXPECT_TRUE(personExists(4)) << "First inserted person should exist";
    EXPECT_TRUE(personExists(28)) << "Last inserted person should exist";
    EXPECT_TRUE(personExists(15)) << "Middle inserted person should exist";
}

TEST_F(QuerySetRemoveTest, InsertLargeBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create a large batch (60 objects - should use individual inserts with transaction)
    std::vector<Person> large_batch;
    for (int i = 4; i <= 63; ++i) {
        large_batch.push_back({i, "Person" + std::to_string(i), 20 + (i % 30)});
    }

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(large_batch));

    // Verify batch insertion was successful
    ASSERT_TRUE(result.has_value()) << "Large batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countPersons(), 63) << "Should have 63 persons after large batch insertion";
    EXPECT_TRUE(personExists(4)) << "First inserted person should exist";
    EXPECT_TRUE(personExists(63)) << "Last inserted person should exist";
    EXPECT_TRUE(personExists(30)) << "Middle inserted person should exist";
}

TEST_F(QuerySetRemoveTest, InsertEmptyBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<Person>{};

    // Create an empty batch
    std::vector<Person> empty_batch;

    // Verify initial state
    EXPECT_EQ(countPersons(), 3) << "Should have 3 persons initially";

    // Insert empty batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(empty_batch));

    // Verify operation succeeds for empty batch
    ASSERT_TRUE(result.has_value()) << "Empty batch insert operation should succeed";

    // Verify database state unchanged
    EXPECT_EQ(countPersons(), 3) << "Should still have 3 persons after empty batch insertion";
}
