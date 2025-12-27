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
struct SqlitePerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Test QuerySet.remove() functionality
class QuerySetRemoveTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up default connection using QuerySet
        auto result = storm::QuerySet<SqlitePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to set default connection: " << result.error().message();

        // Create test table using the default connection
        auto& default_conn  = storm::QuerySet<SqlitePerson>::get_default_connection();
        auto  create_result = default_conn->execute(
                "CREATE TABLE SqlitePerson ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "name TEXT NOT NULL, "
                 "age INTEGER NOT NULL"
                 ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert test data
        auto insert_result = default_conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES "
                "(1, 'Alice', 30), "
                "(2, 'Bob', 25), "
                "(3, 'Charlie', 35)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
    }

    void TearDown() override {
        // Clear all connections
        storm::QuerySet<SqlitePerson>::clear_default_connection();
    }

    // Helper function to count records using the default connection
    static int countSqlitePersons() {
        auto&         conn = storm::QuerySet<SqlitePerson>::get_default_connection();
        sqlite3_stmt* stmt = nullptr;
        int           rc   = sqlite3_prepare_v2(conn->get(), "SELECT COUNT(*) FROM SqlitePerson", -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return -1;

        rc              = sqlite3_step(stmt);
        int const count = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        return count;
    }

    // Helper function to check if person exists using the default connection
    static bool personExists(int id) {
        auto&         conn = storm::QuerySet<SqlitePerson>::get_default_connection();
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(conn->get(), "SELECT COUNT(*) FROM SqlitePerson WHERE id = ?", -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return false;

        sqlite3_bind_int(stmt, 1, id);
        rc                = sqlite3_step(stmt);
        bool const exists = (rc == SQLITE_ROW) && (sqlite3_column_int(stmt, 0) > 0);

        sqlite3_finalize(stmt);
        return exists;
    }
};

TEST_F(QuerySetRemoveTest, DatabaseSetup) {
    // Verify database was created and populated correctly
    EXPECT_TRUE(storm::QuerySet<SqlitePerson>::has_default_connection()) << "Should have default connection";
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons in database";

    // Verify specific persons exist
    EXPECT_TRUE(personExists(1)) << "Alice should exist";
    EXPECT_TRUE(personExists(2)) << "Bob should exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should exist";
}

TEST_F(QuerySetRemoveTest, RemoveExistingSqlitePerson) {
    // Create QuerySet using simplified syntax - no explicit connection needed!
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create person object to remove (Alice with id=1)
    SqlitePerson const alice{.id = 1, .name = "Alice", .age = 30};

    // Verify Alice exists before removal
    EXPECT_TRUE(personExists(1)) << "Alice should exist before removal";
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons before removal";

    // Remove Alice using QuerySet.remove()
    auto result = queryset.remove(alice);

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Remove operation should succeed for existing person";
    // No need to check result.value() for void std::expected - success is indicated by has_value()

    // Verify Alice no longer exists
    EXPECT_FALSE(personExists(1)) << "Alice should not exist after removal";
    EXPECT_EQ(countSqlitePersons(), 2) << "Should have 2 persons after removal";

    // Verify other persons still exist
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveNonExistentSqlitePerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create person object that doesn't exist in database
    SqlitePerson const nonexistent{.id = 999, .name = "NonExistent", .age = 99};

    // Verify person doesn't exist
    EXPECT_FALSE(personExists(999)) << "SqlitePerson with id 999 should not exist";
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons before removal attempt";

    // Attempt to remove non-existent person using QuerySet.remove()
    auto result = queryset.remove(nonexistent);

    // Verify operation completes (SQLite DELETE with no matching rows is not an error)
    ASSERT_TRUE(result.has_value()) << "Remove operation should not error for non-existent person";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons after removal attempt";
    EXPECT_TRUE(personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveMultipleSqlitePersonsSequentially) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create person objects to remove
    SqlitePerson const alice{.id = 1, .name = "Alice", .age = 30};
    SqlitePerson const bob{.id = 2, .name = "Bob", .age = 25};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Remove Alice using QuerySet.remove()
    auto result1 = queryset.remove(alice);
    ASSERT_TRUE(result1.has_value()) << "First remove should succeed";
    // No need to check result1.value() for void std::expected - success is indicated by has_value()
    EXPECT_EQ(countSqlitePersons(), 2) << "Should have 2 persons after first removal";

    // Remove Bob using QuerySet.remove()
    auto result2 = queryset.remove(bob);
    ASSERT_TRUE(result2.has_value()) << "Second remove should succeed";
    // No need to check result2.value() for void std::expected - success is indicated by has_value()
    EXPECT_EQ(countSqlitePersons(), 1) << "Should have 1 person after second removal";

    // Verify only Charlie remains
    EXPECT_FALSE(personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(personExists(2)) << "Bob should be removed";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveWithZeroId) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create person object with id 0 (which doesn't exist)
    SqlitePerson const zero_person{.id = 0, .name = "Zero", .age = 0};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Attempt to remove person with id 0 using QuerySet.remove()
    auto result = queryset.remove(zero_person);

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Remove operation should complete for id 0";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons after removal attempt";
}

TEST_F(QuerySetRemoveTest, RemoveBatchSmall) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Add more test data for batch testing
    auto& conn = storm::QuerySet<SqlitePerson>::get_default_connection();
    for (int i = 4; i <= 12; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES (" + std::to_string(i) + ", 'SqlitePerson" +
                std::to_string(i) + "', " + std::to_string(20 + i) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 12 persons (3 original + 9 new)
    EXPECT_EQ(countSqlitePersons(), 12) << "Should have 12 persons initially";

    // Create batch of persons to remove (batch size ~10)
    std::vector<SqlitePerson> batch_to_remove;
    for (int i = 1; i <= 10; i++) {
        batch_to_remove.emplace_back(i, "SqlitePerson" + std::to_string(i), 20 + i);
    }

    // Remove batch using new batch API
    auto result = queryset.remove(std::span<const SqlitePerson>(batch_to_remove));

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Batch remove operation should succeed";

    // Verify correct number of persons removed
    EXPECT_EQ(countSqlitePersons(), 2) << "Should have 2 persons after batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 10; i++) {
        EXPECT_FALSE(personExists(i)) << "SqlitePerson " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(personExists(11)) << "SqlitePerson 11 should still exist";
    EXPECT_TRUE(personExists(12)) << "SqlitePerson 12 should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchLarge) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Add many test records for large batch testing
    auto& conn = storm::QuerySet<SqlitePerson>::get_default_connection();
    for (int i = 4; i <= 103; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES (" + std::to_string(i) + ", 'SqlitePerson" +
                std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 103 persons (3 original + 100 new)
    EXPECT_EQ(countSqlitePersons(), 103) << "Should have 103 persons initially";

    // Create large batch of persons to remove (batch size ~100)
    std::vector<SqlitePerson> large_batch;
    for (int i = 1; i <= 100; i++) {
        large_batch.emplace_back(i, "SqlitePerson" + std::to_string(i), 20 + (i % 60));
    }

    // Remove large batch - should use individual statements with transaction
    auto result = queryset.remove(std::span<const SqlitePerson>(large_batch));

    // Verify removal was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << '\n';
    }
    ASSERT_TRUE(result.has_value()) << "Large batch remove operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons removed
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons after large batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 100; i++) {
        EXPECT_FALSE(personExists(i)) << "SqlitePerson " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(personExists(101)) << "SqlitePerson 101 should still exist";
    EXPECT_TRUE(personExists(102)) << "SqlitePerson 102 should still exist";
    EXPECT_TRUE(personExists(103)) << "SqlitePerson 103 should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchEmpty) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Create empty batch
    std::vector<SqlitePerson> empty_batch;

    // Attempt to remove empty batch
    auto result = queryset.remove(std::span<const SqlitePerson>(empty_batch));

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Empty batch remove should not error";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons after empty batch removal";
    EXPECT_TRUE(personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(personExists(3)) << "Charlie should still exist";
}

TEST_F(QuerySetRemoveTest, RemoveBatchPartialExist) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Create batch with mix of existing and non-existing persons
    std::vector<SqlitePerson> mixed_batch = {
            {1, "Alice", 30},      // exists
            {999, "Ghost1", 99},   // doesn't exist
            {2, "Bob", 25},        // exists
            {1000, "Ghost2", 100}, // doesn't exist
            {3, "Charlie", 35}     // exists
    };

    // Remove mixed batch
    auto result = queryset.remove(std::span<const SqlitePerson>(mixed_batch));

    // Verify operation completes successfully (non-existing deletes are not errors)
    ASSERT_TRUE(result.has_value()) << "Mixed batch remove should succeed";

    // Verify only existing persons were removed
    EXPECT_EQ(countSqlitePersons(), 0) << "All existing persons should be removed";
    EXPECT_FALSE(personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(personExists(2)) << "Bob should be removed";
    EXPECT_FALSE(personExists(3)) << "Charlie should be removed";
}

TEST_F(QuerySetRemoveTest, RemoveBatchPerformance) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Add test data for performance comparison
    auto&     conn        = storm::QuerySet<SqlitePerson>::get_default_connection();
    const int num_records = 1000;
    for (int i = 4; i <= num_records; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES (" + std::to_string(i) + ", 'SqlitePerson" +
                std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Measure individual removes
    auto start_individual = std::chrono::steady_clock::now();
    for (int i = 1; i <= 50; i++) {
        SqlitePerson const p{.id = i, .name = "SqlitePerson" + std::to_string(i), .age = 20 + (i % 60)};
        auto               result = queryset.remove(p);
        ASSERT_TRUE(result.has_value()) << "Individual remove should succeed";
    }
    auto end_individual = std::chrono::steady_clock::now();
    auto duration_individual =
            std::chrono::duration_cast<std::chrono::microseconds>(end_individual - start_individual).count();

    // Prepare batch for batch remove
    std::vector<SqlitePerson> batch;
    for (int i = 51; i <= 100; i++) {
        batch.emplace_back(i, "SqlitePerson" + std::to_string(i), 20 + (i % 60));
    }

    // Measure batch remove
    auto start_batch = std::chrono::steady_clock::now();
    auto result      = queryset.remove(std::span<const SqlitePerson>(batch));
    ASSERT_TRUE(result.has_value()) << "Batch remove should succeed";
    auto end_batch      = std::chrono::steady_clock::now();
    auto duration_batch = std::chrono::duration_cast<std::chrono::microseconds>(end_batch - start_batch).count();

    // Log performance comparison (batch should be faster)
    std::cout << "\nPerformance Comparison (50 deletes):" << '\n';
    std::cout << "  Individual removes: " << duration_individual << " μs" << '\n';
    std::cout << "  Batch remove: " << duration_batch << " μs" << '\n';
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2)
              << static_cast<double>(duration_individual) / duration_batch << "x" << '\n';

    // Verify correct deletions
    EXPECT_EQ(countSqlitePersons(), num_records - 100) << "Should have correct number of persons after removes";
}

// Simple insert test
TEST_F(QuerySetRemoveTest, InsertSingleSqlitePerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create person object to insert (with explicit ID)
    SqlitePerson const dave{.id = 4, .name = "Dave", .age = 40};

    // Verify initially have 3 persons
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Insert Dave using QuerySet.insert()
    auto result = queryset.insert(dave);

    // Verify insertion was successful and ID was returned
    ASSERT_TRUE(result.has_value()) << "Insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    int64_t const returned_id = result.value();
    EXPECT_GT(returned_id, 0) << "Returned ID should be positive";
    EXPECT_EQ(returned_id, 4) << "Returned ID should be 4";

    // Verify Dave now exists in database
    EXPECT_TRUE(personExists(returned_id)) << "Dave should exist after insertion";
    EXPECT_EQ(countSqlitePersons(), 4) << "Should have 4 persons after insertion";
}

// Bulk insert tests to verify compile-time SQL prefix optimization
TEST_F(QuerySetRemoveTest, InsertSmallBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create a small batch (IDs will be auto-generated)
    std::vector<SqlitePerson> small_batch = {{0, "Dave", 40}, {0, "Eve", 35}, {0, "Frank", 45}};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const SqlitePerson>(small_batch));

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countSqlitePersons(), 6) << "Should have 6 persons after batch insertion";

    // Verify persons exist by selecting and checking names
    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());
    bool found_dave = false, found_eve = false, found_frank = false;
    for (const auto& person : select_result.value()) {
        if (person.name == "Dave")
            found_dave = true;
        if (person.name == "Eve")
            found_eve = true;
        if (person.name == "Frank")
            found_frank = true;
    }
    EXPECT_TRUE(found_dave) << "Dave should exist after batch insertion";
    EXPECT_TRUE(found_eve) << "Eve should exist after batch insertion";
    EXPECT_TRUE(found_frank) << "Frank should exist after batch insertion";
}

TEST_F(QuerySetRemoveTest, InsertMediumBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create a medium batch (25 objects - should still use bulk INSERT)
    std::vector<SqlitePerson> medium_batch;
    for (int i = 4; i <= 28; ++i) {
        medium_batch.emplace_back(0, "SqlitePerson" + std::to_string(i), 20 + (i % 30));
    }

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const SqlitePerson>(medium_batch));

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Medium batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countSqlitePersons(), 28) << "Should have 28 persons after medium batch insertion";
}

TEST_F(QuerySetRemoveTest, InsertLargeBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create a large batch (60 objects - should use chunked bulk INSERT)
    std::vector<SqlitePerson> large_batch;
    for (int i = 4; i <= 63; ++i) {
        large_batch.push_back({0, "SqlitePerson" + std::to_string(i), 20 + (i % 30)});
    }

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const SqlitePerson>(large_batch));

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Large batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(countSqlitePersons(), 63) << "Should have 63 persons after large batch insertion";
}

TEST_F(QuerySetRemoveTest, InsertEmptyBatch) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Create an empty batch
    std::vector<SqlitePerson> empty_batch;

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Insert empty batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const SqlitePerson>(empty_batch));

    // Verify operation succeeds for empty batch (returns void)
    ASSERT_TRUE(result.has_value()) << "Empty batch insert operation should succeed";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons after empty batch insertion";
}

// Test QuerySet.update() functionality
class QuerySetUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up default connection using QuerySet
        auto result = storm::QuerySet<SqlitePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to set default connection: " << result.error().message();

        // Create test table using the default connection
        auto& default_conn  = storm::QuerySet<SqlitePerson>::get_default_connection();
        auto  create_result = default_conn->execute(
                "CREATE TABLE SqlitePerson ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "name TEXT NOT NULL, "
                 "age INTEGER NOT NULL"
                 ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert test data
        auto insert_result = default_conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES "
                "(1, 'Alice', 30), "
                "(2, 'Bob', 25), "
                "(3, 'Charlie', 35)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
    }

    void TearDown() override {
        // Clear all connections
        storm::QuerySet<SqlitePerson>::clear_default_connection();
    }

    // Helper function to count records using the default connection
    static int countSqlitePersons() {
        auto&         conn = storm::QuerySet<SqlitePerson>::get_default_connection();
        sqlite3_stmt* stmt = nullptr;
        int           rc   = sqlite3_prepare_v2(conn->get(), "SELECT COUNT(*) FROM SqlitePerson", -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return -1;

        rc              = sqlite3_step(stmt);
        int const count = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        return count;
    }

    // Helper function to get person by ID
    static std::optional<SqlitePerson> getSqlitePerson(int id) {
        auto&         conn = storm::QuerySet<SqlitePerson>::get_default_connection();
        sqlite3_stmt* stmt = nullptr;
        int           rc   = sqlite3_prepare_v2(
                conn->get(), "SELECT id, name, age FROM SqlitePerson WHERE id = ?", -1, &stmt, nullptr
        );
        if (rc != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_int(stmt, 1, id);
        rc = sqlite3_step(stmt);

        std::optional<SqlitePerson> result;
        if (rc == SQLITE_ROW) {
            result = SqlitePerson{
                    .id   = sqlite3_column_int(stmt, 0),
                    .name = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))),
                    .age  = sqlite3_column_int(stmt, 2)
            };
        }

        sqlite3_finalize(stmt);
        return result;
    }

    // Helper function to check if person exists
    static bool personExists(int id) {
        return getSqlitePerson(id).has_value();
    }
};

TEST_F(QuerySetUpdateTest, DatabaseSetup) {
    // Verify database was created and populated correctly
    EXPECT_TRUE(storm::QuerySet<SqlitePerson>::has_default_connection()) << "Should have default connection";
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons in database";

    // Verify specific persons exist with correct values
    auto alice = getSqlitePerson(1);
    ASSERT_TRUE(alice.has_value()) << "Alice should exist";
    EXPECT_EQ(alice->name, "Alice");
    EXPECT_EQ(alice->age, 30);

    auto bob = getSqlitePerson(2);
    ASSERT_TRUE(bob.has_value()) << "Bob should exist";
    EXPECT_EQ(bob->name, "Bob");
    EXPECT_EQ(bob->age, 25);

    auto charlie = getSqlitePerson(3);
    ASSERT_TRUE(charlie.has_value()) << "Charlie should exist";
    EXPECT_EQ(charlie->name, "Charlie");
    EXPECT_EQ(charlie->age, 35);
}

TEST_F(QuerySetUpdateTest, UpdateExistingSqlitePerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";
    auto alice_before = getSqlitePerson(1);
    ASSERT_TRUE(alice_before.has_value());
    EXPECT_EQ(alice_before->name, "Alice");
    EXPECT_EQ(alice_before->age, 30);

    // Update Alice's information
    SqlitePerson const updated_alice{.id = 1, .name = "Alice Smith", .age = 31};
    auto               result = queryset.update(updated_alice);

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Update operation should succeed";

    // Verify count unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons";

    // Verify Alice was updated
    auto alice_after = getSqlitePerson(1);
    ASSERT_TRUE(alice_after.has_value()) << "Alice should still exist";
    EXPECT_EQ(alice_after->name, "Alice Smith") << "Name should be updated";
    EXPECT_EQ(alice_after->age, 31) << "Age should be updated";

    // Verify other persons unchanged
    auto bob = getSqlitePerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob");
    EXPECT_EQ(bob->age, 25);
}

TEST_F(QuerySetUpdateTest, UpdateNonExistingSqlitePerson) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";
    EXPECT_FALSE(personExists(999)) << "SqlitePerson 999 should not exist";

    // Attempt to update non-existing person
    SqlitePerson const non_existing{.id = 999, .name = "Ghost SqlitePerson", .age = 99};
    auto               result = queryset.update(non_existing);

    // Verify operation completes without error (UPDATE of non-existing row is not an error in SQL)
    ASSERT_TRUE(result.has_value()) << "Update of non-existing person should not error";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons";
    EXPECT_FALSE(personExists(999)) << "SqlitePerson 999 should still not exist";
}

TEST_F(QuerySetUpdateTest, UpdateMultipleTimes) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Update Alice multiple times
    SqlitePerson const alice_v1{.id = 1, .name = "Alice A", .age = 31};
    auto               result1 = queryset.update(alice_v1);
    ASSERT_TRUE(result1.has_value()) << "First update should succeed";

    auto check1 = getSqlitePerson(1);
    ASSERT_TRUE(check1.has_value());
    EXPECT_EQ(check1->name, "Alice A");
    EXPECT_EQ(check1->age, 31);

    SqlitePerson const alice_v2{.id = 1, .name = "Alice B", .age = 32};
    auto               result2 = queryset.update(alice_v2);
    ASSERT_TRUE(result2.has_value()) << "Second update should succeed";

    auto check2 = getSqlitePerson(1);
    ASSERT_TRUE(check2.has_value());
    EXPECT_EQ(check2->name, "Alice B");
    EXPECT_EQ(check2->age, 32);

    SqlitePerson const alice_v3{.id = 1, .name = "Alice C", .age = 33};
    auto               result3 = queryset.update(alice_v3);
    ASSERT_TRUE(result3.has_value()) << "Third update should succeed";

    auto check3 = getSqlitePerson(1);
    ASSERT_TRUE(check3.has_value());
    EXPECT_EQ(check3->name, "Alice C");
    EXPECT_EQ(check3->age, 33);
}

TEST_F(QuerySetUpdateTest, UpdateBatchSmall) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Create small batch of persons to update
    std::vector<SqlitePerson> batch_to_update =
            {{1, "Alice Updated", 31}, {2, "Bob Updated", 26}, {3, "Charlie Updated", 36}};

    // Update batch using batch API
    auto result = queryset.update(std::span<const SqlitePerson>(batch_to_update));

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Batch update operation should succeed";

    // Verify correct number of persons still in database
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons";

    // Verify all persons were updated
    auto alice = getSqlitePerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice Updated");
    EXPECT_EQ(alice->age, 31);

    auto bob = getSqlitePerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob Updated");
    EXPECT_EQ(bob->age, 26);

    auto charlie = getSqlitePerson(3);
    ASSERT_TRUE(charlie.has_value());
    EXPECT_EQ(charlie->name, "Charlie Updated");
    EXPECT_EQ(charlie->age, 36);
}

TEST_F(QuerySetUpdateTest, UpdateBatchMedium) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Add more test data for batch testing
    auto& conn = storm::QuerySet<SqlitePerson>::get_default_connection();
    for (int i = 4; i <= 25; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES (" + std::to_string(i) + ", 'SqlitePerson" +
                std::to_string(i) + "', " + std::to_string(20 + i) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 25 persons
    EXPECT_EQ(countSqlitePersons(), 25) << "Should have 25 persons initially";

    // Create batch of persons to update (batch size ~20)
    std::vector<SqlitePerson> batch_to_update;
    for (int i = 1; i <= 20; i++) {
        batch_to_update.emplace_back(i, "Updated" + std::to_string(i), 100 + i);
    }

    // Update batch
    auto result = queryset.update(std::span<const SqlitePerson>(batch_to_update));

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Batch update operation should succeed";

    // Verify correct number of persons in database
    EXPECT_EQ(countSqlitePersons(), 25) << "Should still have 25 persons";

    // Verify updated persons have new values
    for (int i = 1; i <= 20; i++) {
        auto person = getSqlitePerson(i);
        ASSERT_TRUE(person.has_value()) << "SqlitePerson " << i << " should exist";
        EXPECT_EQ(person->name, "Updated" + std::to_string(i)) << "SqlitePerson " << i << " name should be updated";
        EXPECT_EQ(person->age, 100 + i) << "SqlitePerson " << i << " age should be updated";
    }

    // Verify non-updated persons have original values
    for (int i = 21; i <= 25; i++) {
        auto person = getSqlitePerson(i);
        ASSERT_TRUE(person.has_value()) << "SqlitePerson " << i << " should exist";
        EXPECT_EQ(person->name, "SqlitePerson" + std::to_string(i)) << "SqlitePerson " << i << " should not be updated";
        EXPECT_EQ(person->age, 20 + i) << "SqlitePerson " << i << " age should not be updated";
    }
}

TEST_F(QuerySetUpdateTest, UpdateBatchLarge) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Add many test records for large batch testing
    auto& conn = storm::QuerySet<SqlitePerson>::get_default_connection();
    for (int i = 4; i <= 103; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO SqlitePerson (id, name, age) VALUES (" + std::to_string(i) + ", 'SqlitePerson" +
                std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ")"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 103 persons
    EXPECT_EQ(countSqlitePersons(), 103) << "Should have 103 persons initially";

    // Create large batch of persons to update (batch size ~100)
    std::vector<SqlitePerson> large_batch;
    for (int i = 1; i <= 100; i++) {
        large_batch.emplace_back(i, "LargeUpdate" + std::to_string(i), 200 + i);
    }

    // Update large batch - should use individual statements with transaction
    auto result = queryset.update(std::span<const SqlitePerson>(large_batch));

    // Verify update was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << '\n';
    }
    ASSERT_TRUE(result.has_value()) << "Large batch update operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons in database
    EXPECT_EQ(countSqlitePersons(), 103) << "Should still have 103 persons";

    // Verify updated persons have new values
    for (int i = 1; i <= 100; i++) {
        auto person = getSqlitePerson(i);
        ASSERT_TRUE(person.has_value()) << "SqlitePerson " << i << " should exist";
        EXPECT_EQ(person->name, "LargeUpdate" + std::to_string(i)) << "SqlitePerson " << i << " name should be updated";
        EXPECT_EQ(person->age, 200 + i) << "SqlitePerson " << i << " age should be updated";
    }

    // Verify non-updated persons have original values
    for (int i = 101; i <= 103; i++) {
        auto person = getSqlitePerson(i);
        ASSERT_TRUE(person.has_value()) << "SqlitePerson " << i << " should exist";
        EXPECT_EQ(person->name, "SqlitePerson" + std::to_string(i)) << "SqlitePerson " << i << " should not be updated";
    }
}

TEST_F(QuerySetUpdateTest, UpdateBatchEmpty) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Create empty batch
    std::vector<SqlitePerson> empty_batch;

    // Attempt to update empty batch
    auto result = queryset.update(std::span<const SqlitePerson>(empty_batch));

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Empty batch update should not error";

    // Verify database state unchanged
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons after empty batch update";

    auto alice = getSqlitePerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice");
    EXPECT_EQ(alice->age, 30);
}

TEST_F(QuerySetUpdateTest, UpdateBatchPartialExist) {
    // Create QuerySet using simplified syntax
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Verify initial state
    EXPECT_EQ(countSqlitePersons(), 3) << "Should have 3 persons initially";

    // Create batch with mix of existing and non-existing persons
    std::vector<SqlitePerson> mixed_batch = {
            {1, "Alice New", 31},  // exists
            {999, "Ghost1", 99},   // doesn't exist
            {2, "Bob New", 26},    // exists
            {1000, "Ghost2", 100}, // doesn't exist
            {3, "Charlie New", 36} // exists
    };

    // Update mixed batch
    auto result = queryset.update(std::span<const SqlitePerson>(mixed_batch));

    // Verify operation completes successfully (non-existing updates are not errors)
    ASSERT_TRUE(result.has_value()) << "Mixed batch update should succeed";

    // Verify only existing persons were updated
    EXPECT_EQ(countSqlitePersons(), 3) << "Should still have 3 persons";

    auto alice = getSqlitePerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice New");
    EXPECT_EQ(alice->age, 31);

    auto bob = getSqlitePerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob New");
    EXPECT_EQ(bob->age, 26);

    auto charlie = getSqlitePerson(3);
    ASSERT_TRUE(charlie.has_value());
    EXPECT_EQ(charlie->name, "Charlie New");
    EXPECT_EQ(charlie->age, 36);

    // Non-existing persons should not be created
    EXPECT_FALSE(personExists(999));
    EXPECT_FALSE(personExists(1000));
}

TEST_F(QuerySetUpdateTest, UpdateCachedStatementReuse) {
    // Create QuerySet using simplified syntax - this creates cached UpdateStatement
    auto queryset = storm::QuerySet<SqlitePerson>{};

    // Perform multiple updates to verify statement caching works correctly
    for (int i = 0; i < 10; ++i) {
        SqlitePerson const updated_alice{.id = 1, .name = "Alice V" + std::to_string(i), .age = 30 + i};
        auto               result = queryset.update(updated_alice);
        ASSERT_TRUE(result.has_value()) << "Update iteration " << i << " should succeed";

        auto alice = getSqlitePerson(1);
        ASSERT_TRUE(alice.has_value());
        EXPECT_EQ(alice->name, "Alice V" + std::to_string(i));
        EXPECT_EQ(alice->age, 30 + i);
    }

    // Verify final state
    auto final_alice = getSqlitePerson(1);
    ASSERT_TRUE(final_alice.has_value());
    EXPECT_EQ(final_alice->name, "Alice V9");
    EXPECT_EQ(final_alice->age, 39);
}
