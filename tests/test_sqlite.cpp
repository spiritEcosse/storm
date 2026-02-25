#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include <sqlite3.h>

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <expected>;
import <string>;
import <optional>;
import <span>;
import <chrono>;
import <iostream>;
import <iomanip>;

#include "test_models.h"

// Test QuerySet.remove() functionality
template <typename ConnType> class QuerySetRemoveTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
            return;
        }

        // Create test table using the default connection
        const auto& default_conn = storm::QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(default_conn, person_create_sql);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(default_conn, {"Person"});

        // Insert test data
        auto insert_result = default_conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES "
                "(1, 'Alice', 30, 0, 0, 0), "
                "(2, 'Bob', 25, 0, 0, 0), "
                "(3, 'Charlie', 35, 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();

        // For PG: reset identity sequence past the explicitly-inserted IDs
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            (void)default_conn->execute("ALTER TABLE Person ALTER COLUMN id RESTART WITH 4");
        }
    }

    // Helper function to count records using the ORM
    static auto countPersons() -> int {
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.count().get();
        if (!result.has_value()) {
            return -1;
        }
        return static_cast<int>(result.value());
    }

    // Helper function to check if person exists using the ORM
    static auto personExists(int person_id) -> bool {
        using namespace storm::orm::where;
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.where(field<^^Person::id>() == person_id).select().execute();
        return result.has_value() && !result.value().empty();
    }
};

TYPED_TEST_SUITE(QuerySetRemoveTest, DatabaseTypes);

TYPED_TEST(QuerySetRemoveTest, DatabaseSetup) {
    // Verify database was created and populated correctly
    EXPECT_TRUE((storm::QuerySet<Person, TypeParam>::has_default_connection())) << "Should have default connection";
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons in database";

    // Verify specific persons exist
    EXPECT_TRUE(this->personExists(1)) << "Alice should exist";
    EXPECT_TRUE(this->personExists(2)) << "Bob should exist";
    EXPECT_TRUE(this->personExists(3)) << "Charlie should exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveExistingPerson) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create person object to remove (Alice with id=1)
    Person const alice{.id = 1, .name = "Alice", .age = 30};

    // Verify Alice exists before removal
    EXPECT_TRUE(this->personExists(1)) << "Alice should exist before removal";
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons before removal";

    // Remove Alice using QuerySet.remove()
    auto result = queryset.remove(alice).execute();

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Remove operation should succeed for existing person";

    // Verify Alice no longer exists
    EXPECT_FALSE(this->personExists(1)) << "Alice should not exist after removal";
    EXPECT_EQ(this->countPersons(), 2) << "Should have 2 persons after removal";

    // Verify other persons still exist
    EXPECT_TRUE(this->personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(this->personExists(3)) << "Charlie should still exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveNonExistentPerson) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create person object that doesn't exist in database
    Person const nonexistent{.id = 999, .name = "NonExistent", .age = 99};

    // Verify person doesn't exist
    EXPECT_FALSE(this->personExists(999)) << "Person with id 999 should not exist";
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons before removal attempt";

    // Attempt to remove non-existent person using QuerySet.remove()
    auto result = queryset.remove(nonexistent).execute();

    // Verify operation completes (SQLite DELETE with no matching rows is not an error)
    ASSERT_TRUE(result.has_value()) << "Remove operation should not error for non-existent person";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons after removal attempt";
    EXPECT_TRUE(this->personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(this->personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(this->personExists(3)) << "Charlie should still exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveMultiplePersonsSequentially) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create person objects to remove
    Person const alice{.id = 1, .name = "Alice", .age = 30};
    Person const bob{.id = 2, .name = "Bob", .age = 25};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Remove Alice using QuerySet.remove()
    auto result1 = queryset.remove(alice).execute();
    ASSERT_TRUE(result1.has_value()) << "First remove should succeed";
    EXPECT_EQ(this->countPersons(), 2) << "Should have 2 persons after first removal";

    // Remove Bob using QuerySet.remove()
    auto result2 = queryset.remove(bob).execute();
    ASSERT_TRUE(result2.has_value()) << "Second remove should succeed";
    EXPECT_EQ(this->countPersons(), 1) << "Should have 1 person after second removal";

    // Verify only Charlie remains
    EXPECT_FALSE(this->personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(this->personExists(2)) << "Bob should be removed";
    EXPECT_TRUE(this->personExists(3)) << "Charlie should still exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveWithZeroId) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create person object with id 0 (which doesn't exist)
    Person const zero_person{.id = 0, .name = "Zero", .age = 0};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Attempt to remove person with id 0 using QuerySet.remove()
    auto result = queryset.remove(zero_person).execute();

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Remove operation should complete for id 0";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons after removal attempt";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, RemoveBatchSmall) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add more test data for batch testing
    const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
    for (int i = 4; i <= 12; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + i) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 12 persons (3 original + 9 new)
    EXPECT_EQ(this->countPersons(), 12) << "Should have 12 persons initially";

    // Create batch of persons to remove (batch size ~10)
    std::vector<Person> batch_to_remove;
    for (int i = 1; i <= 10; i++) {
        batch_to_remove.emplace_back(i, "Person" + std::to_string(i), 20 + i);
    }

    // Remove batch using new batch API
    auto result = queryset.remove(std::span<const Person>(batch_to_remove)).execute();

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Batch remove operation should succeed";

    // Verify correct number of persons removed
    EXPECT_EQ(this->countPersons(), 2) << "Should have 2 persons after batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 10; i++) {
        EXPECT_FALSE(this->personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(this->personExists(11)) << "Person 11 should still exist";
    EXPECT_TRUE(this->personExists(12)) << "Person 12 should still exist";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, RemoveBatchLarge) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add many test records for large batch testing
    const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
    for (int i = 4; i <= 103; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 103 persons (3 original + 100 new)
    EXPECT_EQ(this->countPersons(), 103) << "Should have 103 persons initially";

    // Create large batch of persons to remove (batch size ~100)
    std::vector<Person> large_batch;
    for (int i = 1; i <= 100; i++) {
        large_batch.emplace_back(i, "Person" + std::to_string(i), 20 + (i % 60));
    }

    // Remove large batch - should use individual statements with transaction
    auto result = queryset.remove(std::span<const Person>(large_batch)).execute();

    // Verify removal was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << '\n';
    }
    ASSERT_TRUE(result.has_value()) << "Large batch remove operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons removed
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons after large batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 100; i++) {
        EXPECT_FALSE(this->personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    EXPECT_TRUE(this->personExists(101)) << "Person 101 should still exist";
    EXPECT_TRUE(this->personExists(102)) << "Person 102 should still exist";
    EXPECT_TRUE(this->personExists(103)) << "Person 103 should still exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveBatchEmpty) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create empty batch
    std::vector<Person> empty_batch;

    // Attempt to remove empty batch
    auto result = queryset.remove(std::span<const Person>(empty_batch)).execute();

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Empty batch remove should not error";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons after empty batch removal";
    EXPECT_TRUE(this->personExists(1)) << "Alice should still exist";
    EXPECT_TRUE(this->personExists(2)) << "Bob should still exist";
    EXPECT_TRUE(this->personExists(3)) << "Charlie should still exist";
}

TYPED_TEST(QuerySetRemoveTest, RemoveBatchPartialExist) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create batch with mix of existing and non-existing persons
    std::vector<Person> mixed_batch = {
            {1, "Alice", 30},      // exists
            {999, "Ghost1", 99},   // doesn't exist
            {2, "Bob", 25},        // exists
            {1000, "Ghost2", 100}, // doesn't exist
            {3, "Charlie", 35}     // exists
    };

    // Remove mixed batch
    auto result = queryset.remove(std::span<const Person>(mixed_batch)).execute();

    // Verify operation completes successfully (non-existing deletes are not errors)
    ASSERT_TRUE(result.has_value()) << "Mixed batch remove should succeed";

    // Verify only existing persons were removed
    EXPECT_EQ(this->countPersons(), 0) << "All existing persons should be removed";
    EXPECT_FALSE(this->personExists(1)) << "Alice should be removed";
    EXPECT_FALSE(this->personExists(2)) << "Bob should be removed";
    EXPECT_FALSE(this->personExists(3)) << "Charlie should be removed";
}

TYPED_TEST(QuerySetRemoveTest, RemoveBatchPerformance) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add test data for performance comparison
    const auto& conn        = storm::QuerySet<Person, TypeParam>::get_default_connection();
    const int   num_records = 1000;
    for (int i = 4; i <= num_records; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Measure individual removes
    auto start_individual = std::chrono::steady_clock::now();
    for (int i = 1; i <= 50; i++) {
        Person const person{.id = i, .name = "Person" + std::to_string(i), .age = 20 + (i % 60)};
        auto         result = queryset.remove(person).execute();
        ASSERT_TRUE(result.has_value()) << "Individual remove should succeed";
    }
    auto end_individual = std::chrono::steady_clock::now();
    auto duration_individual =
            std::chrono::duration_cast<std::chrono::microseconds>(end_individual - start_individual).count();

    // Prepare batch for batch remove
    std::vector<Person> batch;
    for (int i = 51; i <= 100; i++) {
        batch.emplace_back(i, "Person" + std::to_string(i), 20 + (i % 60));
    }

    // Measure batch remove
    auto start_batch = std::chrono::steady_clock::now();
    auto result      = queryset.remove(std::span<const Person>(batch)).execute();
    ASSERT_TRUE(result.has_value()) << "Batch remove should succeed";
    auto end_batch      = std::chrono::steady_clock::now();
    auto duration_batch = std::chrono::duration_cast<std::chrono::microseconds>(end_batch - start_batch).count();

    // Log performance comparison (batch should be faster)
    std::cout << "\nPerformance Comparison (50 deletes):" << '\n';
    std::cout << "  Individual removes: " << duration_individual << " μs" << '\n';
    std::cout << "  Batch remove: " << duration_batch << " μs" << '\n';
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2)
              << static_cast<double>(duration_individual) / static_cast<double>(duration_batch) << "x" << '\n';

    // Verify correct deletions
    EXPECT_EQ(this->countPersons(), num_records - 100) << "Should have correct number of persons after removes";
}

// Simple insert test
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, InsertSinglePerson) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create person object to insert (with explicit ID)
    Person const dave{.id = 4, .name = "Dave", .age = 40};

    // Verify initially have 3 persons
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Insert Dave using QuerySet.insert()
    auto result = queryset.insert(dave).execute();

    // Verify insertion was successful and ID was returned
    ASSERT_TRUE(result.has_value()) << "Insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    int64_t const returned_id = result.value();
    EXPECT_GT(returned_id, 0) << "Returned ID should be positive";
    EXPECT_EQ(returned_id, 4) << "Returned ID should be 4";

    // Verify Dave now exists in database
    EXPECT_TRUE(this->personExists(returned_id)) << "Dave should exist after insertion";
    EXPECT_EQ(this->countPersons(), 4) << "Should have 4 persons after insertion";
}

// Bulk insert tests to verify compile-time SQL prefix optimization
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, InsertSmallBatch) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create a small batch (IDs will be auto-generated)
    std::vector<Person> small_batch = {{0, "Dave", 40}, {0, "Eve", 35}, {0, "Frank", 45}};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(small_batch)).execute();

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(this->countPersons(), 6) << "Should have 6 persons after batch insertion";

    // Verify persons exist by selecting and checking names
    auto select_result = queryset.select().execute();
    ASSERT_TRUE(select_result.has_value());
    bool found_dave  = false; // NOLINT(misc-const-correctness) - modified in loop
    bool found_eve   = false; // NOLINT(misc-const-correctness) - modified in loop
    bool found_frank = false; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& person : select_result.value()) {
        if (person.name == "Dave") {
            found_dave = true;
        }
        if (person.name == "Eve") {
            found_eve = true;
        }
        if (person.name == "Frank") {
            found_frank = true;
        }
    }
    EXPECT_TRUE(found_dave) << "Dave should exist after batch insertion";
    EXPECT_TRUE(found_eve) << "Eve should exist after batch insertion";
    EXPECT_TRUE(found_frank) << "Frank should exist after batch insertion";
}

TYPED_TEST(QuerySetRemoveTest, InsertMediumBatch) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create a medium batch (25 objects - should still use bulk INSERT)
    std::vector<Person> medium_batch;
    for (int i = 4; i <= 28; ++i) {
        medium_batch.emplace_back(0, "Person" + std::to_string(i), 20 + (i % 30));
    }

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(medium_batch)).execute();

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Medium batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(this->countPersons(), 28) << "Should have 28 persons after medium batch insertion";
}

TYPED_TEST(QuerySetRemoveTest, InsertLargeBatch) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create a large batch (60 objects - should use chunked bulk INSERT)
    std::vector<Person> large_batch;
    for (int i = 4; i <= 63; ++i) {
        large_batch.push_back({0, "Person" + std::to_string(i), 20 + (i % 30)});
    }

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Insert batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(large_batch)).execute();

    // Verify batch insertion was successful (returns void)
    ASSERT_TRUE(result.has_value()) << "Large batch insert operation should succeed: "
                                    << (result.has_value() ? "success" : result.error().message());

    // Verify all persons now exist in database
    EXPECT_EQ(this->countPersons(), 63) << "Should have 63 persons after large batch insertion";
}

TYPED_TEST(QuerySetRemoveTest, InsertEmptyBatch) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Create an empty batch
    std::vector<Person> empty_batch;

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Insert empty batch using QuerySet.insert() with span
    auto result = queryset.insert(std::span<const Person>(empty_batch)).execute();

    // Verify operation succeeds for empty batch (returns void)
    ASSERT_TRUE(result.has_value()) << "Empty batch insert operation should succeed";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons after empty batch insertion";
}

// Test chunked remove (>799 rows to trigger execute_chunked path)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, RemoveBatchChunked) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add many test records (1000+) to test chunked deletion
    // MAX_CHUNK_SIZE is 799, so >799 triggers chunked path
    const auto& conn        = storm::QuerySet<Person, TypeParam>::get_default_connection();
    const int   num_records = 1000;
    for (int i = 4; i <= num_records; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert record " << i;
    }

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 1000) << "Should have 1000 persons initially";

    // Create batch of >799 persons to remove (triggers chunked path)
    std::vector<Person> chunked_batch;
    for (int i = 1; i <= 850; i++) {
        chunked_batch.emplace_back(i, "Person" + std::to_string(i), 20 + (i % 60));
    }

    // Remove chunked batch - should use execute_chunked with transaction
    auto result = queryset.remove(std::span<const Person>(chunked_batch)).execute();

    // Verify removal was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << '\n';
    }
    ASSERT_TRUE(result.has_value()) << "Chunked batch remove operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons removed
    EXPECT_EQ(this->countPersons(), 150) << "Should have 150 persons after chunked batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 850; i++) {
        EXPECT_FALSE(this->personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    for (int i = 851; i <= 1000; i++) {
        EXPECT_TRUE(this->personExists(i)) << "Person " << i << " should still exist";
    }
}

// Test chunked remove with remainder (tests both full chunks and remainder processing)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetRemoveTest, RemoveBatchChunkedWithRemainder) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add many test records to test chunked deletion with remainder
    // MAX_CHUNK_SIZE is 799, so 1650 rows = 2 full chunks (1598) + 52 remainder
    const auto& conn        = storm::QuerySet<Person, TypeParam>::get_default_connection();
    const int   num_records = 1800;
    for (int i = 4; i <= num_records; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert record " << i;
    }

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 1800) << "Should have 1800 persons initially";

    // Create batch of 1650 persons (2 full chunks + 52 remainder)
    std::vector<Person> chunked_batch;
    for (int i = 1; i <= 1650; i++) {
        chunked_batch.emplace_back(i, "Person" + std::to_string(i), 20 + (i % 60));
    }

    // Remove chunked batch - should use execute_chunked with remainder
    auto result = queryset.remove(std::span<const Person>(chunked_batch)).execute();

    // Verify removal was successful
    ASSERT_TRUE(result.has_value()) << "Chunked batch remove with remainder should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons removed
    EXPECT_EQ(this->countPersons(), 150) << "Should have 150 persons after chunked batch removal";

    // Verify specific persons were removed
    for (int i = 1; i <= 1650; i++) {
        EXPECT_FALSE(this->personExists(i)) << "Person " << i << " should be removed";
    }

    // Verify remaining persons still exist
    for (int i = 1651; i <= 1800; i++) {
        EXPECT_TRUE(this->personExists(i)) << "Person " << i << " should still exist";
    }
}

// Test QuerySet.update() functionality
template <typename ConnType> class QuerySetUpdateTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
            return;
        }

        // Create test table using the default connection
        const auto& default_conn = storm::QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(default_conn, person_create_sql);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(default_conn, {"Person"});

        // Insert test data
        auto insert_result = default_conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES "
                "(1, 'Alice', 30, 0, 0, 0), "
                "(2, 'Bob', 25, 0, 0, 0), "
                "(3, 'Charlie', 35, 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();

        // For PG: reset identity sequence past the explicitly-inserted IDs
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            (void)default_conn->execute("ALTER TABLE Person ALTER COLUMN id RESTART WITH 4");
        }
    }

    // Helper function to count records using the ORM
    static auto countPersons() -> int {
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.count().get();
        if (!result.has_value()) {
            return -1;
        }
        return static_cast<int>(result.value());
    }

    // Helper function to get person by ID using the ORM
    static auto getPerson(int person_id) -> std::optional<Person> {
        using namespace storm::orm::where;
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.where(field<^^Person::id>() == person_id).select().execute();
        if (!result.has_value() || result.value().empty()) {
            return std::nullopt;
        }
        return *result.value().begin();
    }

    // Helper function to check if person exists
    static auto personExists(int person_id) -> bool {
        return getPerson(person_id).has_value();
    }
};

TYPED_TEST_SUITE(QuerySetUpdateTest, DatabaseTypes);

TYPED_TEST(QuerySetUpdateTest, DatabaseSetup) {
    // Verify database was created and populated correctly
    EXPECT_TRUE((storm::QuerySet<Person, TypeParam>::has_default_connection())) << "Should have default connection";
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons in database";

    // Verify specific persons exist with correct values
    auto alice = this->getPerson(1);
    ASSERT_TRUE(alice.has_value()) << "Alice should exist";
    EXPECT_EQ(alice->name, "Alice");
    EXPECT_EQ(alice->age, 30);

    auto bob = this->getPerson(2);
    ASSERT_TRUE(bob.has_value()) << "Bob should exist";
    EXPECT_EQ(bob->name, "Bob");
    EXPECT_EQ(bob->age, 25);

    auto charlie = this->getPerson(3);
    ASSERT_TRUE(charlie.has_value()) << "Charlie should exist";
    EXPECT_EQ(charlie->name, "Charlie");
    EXPECT_EQ(charlie->age, 35);
}

TYPED_TEST(QuerySetUpdateTest, UpdateExistingPerson) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";
    auto alice_before = this->getPerson(1);
    ASSERT_TRUE(alice_before.has_value());
    EXPECT_EQ(alice_before->name, "Alice");
    EXPECT_EQ(alice_before->age, 30);

    // Update Alice's information
    Person const updated_alice{.id = 1, .name = "Alice Smith", .age = 31};
    auto         result = queryset.update(updated_alice).execute();

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Update operation should succeed";

    // Verify count unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons";

    // Verify Alice was updated
    auto alice_after = this->getPerson(1);
    ASSERT_TRUE(alice_after.has_value()) << "Alice should still exist";
    EXPECT_EQ(alice_after->name, "Alice Smith") << "Name should be updated";
    EXPECT_EQ(alice_after->age, 31) << "Age should be updated";

    // Verify other persons unchanged
    auto bob = this->getPerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob");
    EXPECT_EQ(bob->age, 25);
}

TYPED_TEST(QuerySetUpdateTest, UpdateNonExistingPerson) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";
    EXPECT_FALSE(this->personExists(999)) << "Person 999 should not exist";

    // Attempt to update non-existing person
    Person const non_existing{.id = 999, .name = "Ghost Person", .age = 99};
    auto         result = queryset.update(non_existing).execute();

    // Verify operation completes without error (UPDATE of non-existing row is not an error in SQL)
    ASSERT_TRUE(result.has_value()) << "Update of non-existing person should not error";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons";
    EXPECT_FALSE(this->personExists(999)) << "Person 999 should still not exist";
}

TYPED_TEST(QuerySetUpdateTest, UpdateMultipleTimes) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Update Alice multiple times
    Person const alice_v1{.id = 1, .name = "Alice A", .age = 31};
    auto         result1 = queryset.update(alice_v1).execute();
    ASSERT_TRUE(result1.has_value()) << "First update should succeed";

    auto check1 = this->getPerson(1);
    ASSERT_TRUE(check1.has_value());
    EXPECT_EQ(check1->name, "Alice A");
    EXPECT_EQ(check1->age, 31);

    Person const alice_v2{.id = 1, .name = "Alice B", .age = 32};
    auto         result2 = queryset.update(alice_v2).execute();
    ASSERT_TRUE(result2.has_value()) << "Second update should succeed";

    auto check2 = this->getPerson(1);
    ASSERT_TRUE(check2.has_value());
    EXPECT_EQ(check2->name, "Alice B");
    EXPECT_EQ(check2->age, 32);

    Person const alice_v3{.id = 1, .name = "Alice C", .age = 33};
    auto         result3 = queryset.update(alice_v3).execute();
    ASSERT_TRUE(result3.has_value()) << "Third update should succeed";

    auto check3 = this->getPerson(1);
    ASSERT_TRUE(check3.has_value());
    EXPECT_EQ(check3->name, "Alice C");
    EXPECT_EQ(check3->age, 33);
}

TYPED_TEST(QuerySetUpdateTest, UpdateBatchSmall) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create small batch of persons to update
    std::vector<Person> batch_to_update =
            {{1, "Alice Updated", 31}, {2, "Bob Updated", 26}, {3, "Charlie Updated", 36}};

    // Update batch using batch API
    auto result = queryset.update(std::span<const Person>(batch_to_update)).execute();

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Batch update operation should succeed";

    // Verify correct number of persons still in database
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons";

    // Verify all persons were updated
    auto alice = this->getPerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice Updated");
    EXPECT_EQ(alice->age, 31);

    auto bob = this->getPerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob Updated");
    EXPECT_EQ(bob->age, 26);

    auto charlie = this->getPerson(3);
    ASSERT_TRUE(charlie.has_value());
    EXPECT_EQ(charlie->name, "Charlie Updated");
    EXPECT_EQ(charlie->age, 36);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetUpdateTest, UpdateBatchMedium) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add more test data for batch testing
    const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
    for (int i = 4; i <= 25; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + i) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 25 persons
    EXPECT_EQ(this->countPersons(), 25) << "Should have 25 persons initially";

    // Create batch of persons to update (batch size ~20)
    std::vector<Person> batch_to_update;
    for (int i = 1; i <= 20; i++) {
        batch_to_update.emplace_back(i, "Updated" + std::to_string(i), 100 + i);
    }

    // Update batch
    auto result = queryset.update(std::span<const Person>(batch_to_update)).execute();

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Batch update operation should succeed";

    // Verify correct number of persons in database
    EXPECT_EQ(this->countPersons(), 25) << "Should still have 25 persons";

    // Verify updated persons have new values
    for (int i = 1; i <= 20; i++) {
        auto person = this->getPerson(i);
        ASSERT_TRUE(person.has_value()) << "Person " << i << " should exist";
        EXPECT_EQ(person->name, "Updated" + std::to_string(i)) << "Person " << i << " name should be updated";
        EXPECT_EQ(person->age, 100 + i) << "Person " << i << " age should be updated";
    }

    // Verify non-updated persons have original values
    for (int i = 21; i <= 25; i++) {
        auto person = this->getPerson(i);
        ASSERT_TRUE(person.has_value()) << "Person " << i << " should exist";
        EXPECT_EQ(person->name, "Person" + std::to_string(i)) << "Person " << i << " should not be updated";
        EXPECT_EQ(person->age, 20 + i) << "Person " << i << " age should not be updated";
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetUpdateTest, UpdateBatchLarge) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Add many test records for large batch testing
    const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
    for (int i = 4; i <= 103; i++) {
        auto insert_result = conn->execute(
                "INSERT INTO Person (id, name, age, salary, is_active, years_experience) VALUES (" + std::to_string(i) +
                ", 'Person" + std::to_string(i) + "', " + std::to_string(20 + (i % 60)) + ", 0, 0, 0)"
        );
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
    }

    // Verify initial state - should have 103 persons
    EXPECT_EQ(this->countPersons(), 103) << "Should have 103 persons initially";

    // Create large batch of persons to update (batch size ~100)
    std::vector<Person> large_batch;
    for (int i = 1; i <= 100; i++) {
        large_batch.emplace_back(i, "LargeUpdate" + std::to_string(i), 200 + i);
    }

    // Update large batch - should use individual statements with transaction
    auto result = queryset.update(std::span<const Person>(large_batch)).execute();

    // Verify update was successful
    if (!result.has_value()) {
        std::cout << "Error: " << result.error().message() << '\n';
    }
    ASSERT_TRUE(result.has_value()) << "Large batch update operation should succeed: "
                                    << (result.has_value() ? "" : result.error().message());

    // Verify correct number of persons in database
    EXPECT_EQ(this->countPersons(), 103) << "Should still have 103 persons";

    // Verify updated persons have new values
    for (int i = 1; i <= 100; i++) {
        auto person = this->getPerson(i);
        ASSERT_TRUE(person.has_value()) << "Person " << i << " should exist";
        EXPECT_EQ(person->name, "LargeUpdate" + std::to_string(i)) << "Person " << i << " name should be updated";
        EXPECT_EQ(person->age, 200 + i) << "Person " << i << " age should be updated";
    }

    // Verify non-updated persons have original values
    for (int i = 101; i <= 103; i++) {
        auto person = this->getPerson(i);
        ASSERT_TRUE(person.has_value()) << "Person " << i << " should exist";
        EXPECT_EQ(person->name, "Person" + std::to_string(i)) << "Person " << i << " should not be updated";
    }
}

// Test batch update with exactly 1 element (exercises execute_single_row path)
TYPED_TEST(QuerySetUpdateTest, UpdateBatchSingleElement) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create batch with exactly 1 element (triggers execute_single_row in batch path)
    std::vector<Person> single_batch = {{1, "Alice BatchSingle", 99}};

    // Update using batch API with single element
    auto result = queryset.update(std::span<const Person>(single_batch)).execute();

    // Verify update was successful
    ASSERT_TRUE(result.has_value()) << "Single element batch update should succeed";

    // Verify correct number of persons in database
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons";

    // Verify Alice was updated
    auto alice = this->getPerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice BatchSingle");
    EXPECT_EQ(alice->age, 99);

    // Verify other persons unchanged
    auto bob = this->getPerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob");
    EXPECT_EQ(bob->age, 25);
}

TYPED_TEST(QuerySetUpdateTest, UpdateBatchEmpty) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create empty batch
    std::vector<Person> empty_batch;

    // Attempt to update empty batch
    auto result = queryset.update(std::span<const Person>(empty_batch)).execute();

    // Verify operation completes without error
    ASSERT_TRUE(result.has_value()) << "Empty batch update should not error";

    // Verify database state unchanged
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons after empty batch update";

    auto alice = this->getPerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice");
    EXPECT_EQ(alice->age, 30);
}

TYPED_TEST(QuerySetUpdateTest, UpdateBatchPartialExist) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Verify initial state
    EXPECT_EQ(this->countPersons(), 3) << "Should have 3 persons initially";

    // Create batch with mix of existing and non-existing persons
    std::vector<Person> mixed_batch = {
            {1, "Alice New", 31},  // exists
            {999, "Ghost1", 99},   // doesn't exist
            {2, "Bob New", 26},    // exists
            {1000, "Ghost2", 100}, // doesn't exist
            {3, "Charlie New", 36} // exists
    };

    // Update mixed batch
    auto result = queryset.update(std::span<const Person>(mixed_batch)).execute();

    // Verify operation completes successfully (non-existing updates are not errors)
    ASSERT_TRUE(result.has_value()) << "Mixed batch update should succeed";

    // Verify only existing persons were updated
    EXPECT_EQ(this->countPersons(), 3) << "Should still have 3 persons";

    auto alice = this->getPerson(1);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->name, "Alice New");
    EXPECT_EQ(alice->age, 31);

    auto bob = this->getPerson(2);
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->name, "Bob New");
    EXPECT_EQ(bob->age, 26);

    auto charlie = this->getPerson(3);
    ASSERT_TRUE(charlie.has_value());
    EXPECT_EQ(charlie->name, "Charlie New");
    EXPECT_EQ(charlie->age, 36);

    // Non-existing persons should not be created
    EXPECT_FALSE(this->personExists(999));
    EXPECT_FALSE(this->personExists(1000));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(QuerySetUpdateTest, UpdateCachedStatementReuse) {
    auto queryset = storm::QuerySet<Person, TypeParam>{};

    // Perform multiple updates to verify statement caching works correctly
    for (int i = 0; i < 10; ++i) {
        Person const updated_alice{.id = 1, .name = "Alice V" + std::to_string(i), .age = 30 + i};
        auto         result = queryset.update(updated_alice).execute();
        ASSERT_TRUE(result.has_value()) << "Update iteration " << i << " should succeed";

        auto alice = this->getPerson(1);
        ASSERT_TRUE(alice.has_value());
        EXPECT_EQ(alice->name, "Alice V" + std::to_string(i));
        EXPECT_EQ(alice->age, 30 + i);
    }

    // Verify final state
    auto final_alice = this->getPerson(1);
    ASSERT_TRUE(final_alice.has_value());
    EXPECT_EQ(final_alice->name, "Alice V9");
    EXPECT_EQ(final_alice->age, 39);
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
