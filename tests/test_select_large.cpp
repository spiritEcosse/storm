#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;
import <format>;

using namespace storm;

// Test model for large dataset testing
struct TestRecord {
    [[= storm::meta::FieldAttr::primary]] int id;
    int                                       value;
    std::string                               name;
};

// Test fixture for large SELECT operations
class SelectLargeTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<TestRecord>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<TestRecord>::get_default_connection();

        // Create table
        auto create_result = conn->execute(
                "CREATE TABLE TestRecord ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "value INTEGER NOT NULL, "
                "name TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();
    }

    auto TearDown() -> void override {
        QuerySet<TestRecord>::clear_default_connection();
    }
};

// Test: SELECT with result set larger than initial capacity (10K)
// This tests the exponential growth path
TEST_F(SelectLargeTest, SelectMoreThan10KRows) {
    QuerySet<TestRecord> queryset;

    // Insert 25,000 records (will trigger exponential growth: 10K → 20K → 40K)
    constexpr int           RECORD_COUNT = 25000;
    std::vector<TestRecord> records;
    records.reserve(RECORD_COUNT);

    for (int i = 1; i <= RECORD_COUNT; ++i) {
        records.emplace_back(i, i * 10, std::format("Record_{}", i));
    }

    auto insert_result = queryset.insert(std::span<const TestRecord>(records));
    ASSERT_TRUE(insert_result.has_value()) << "Batch INSERT failed: " << insert_result.error().message();

    // SELECT all rows - this will exercise exponential growth
    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT failed: " << select_result.error().message();

    const auto& retrieved = select_result.value();
    ASSERT_EQ(retrieved.size(), RECORD_COUNT) << "Should retrieve all 25K records";

    // Verify data integrity (spot checks)
    auto it = retrieved.begin();
    EXPECT_EQ(it->value, 10);
    EXPECT_EQ(it->name, "Record_1");

    auto it_10k = std::ranges::next(retrieved.begin(), 9999); // 10000th element
    EXPECT_EQ(it_10k->value, 100000);                         // 10K boundary
    EXPECT_EQ(it_10k->name, "Record_10000");

    auto it_20k = std::ranges::next(retrieved.begin(), 19999); // 20000th element
    EXPECT_EQ(it_20k->value, 200000);                          // 20K boundary
    EXPECT_EQ(it_20k->name, "Record_20000");

    auto last_it = std::ranges::next(retrieved.begin(), RECORD_COUNT - 1);
    EXPECT_EQ(last_it->value, RECORD_COUNT * 10);
    EXPECT_EQ(last_it->name, "Record_" + std::to_string(RECORD_COUNT));
}

// Test: SELECT with result set at exactly 10K (no exponential growth needed)
TEST_F(SelectLargeTest, SelectExactly10KRows) {
    QuerySet<TestRecord> queryset;

    constexpr int           RECORD_COUNT = 10000;
    std::vector<TestRecord> records;
    records.reserve(RECORD_COUNT);

    for (int i = 1; i <= RECORD_COUNT; ++i) {
        records.emplace_back(i, i, std::format("R{}", i));
    }

    auto insert_result = queryset.insert(std::span<const TestRecord>(records));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT all rows - should fit in initial capacity exactly
    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& retrieved = select_result.value();
    ASSERT_EQ(retrieved.size(), RECORD_COUNT);

    EXPECT_EQ(retrieved.begin()->value, 1);
    auto last_it = std::ranges::next(retrieved.begin(), RECORD_COUNT - 1);
    EXPECT_EQ(last_it->value, RECORD_COUNT);
}

// Test: SELECT with result set slightly over 10K (minimal exponential growth)
TEST_F(SelectLargeTest, SelectSlightlyOver10KRows) {
    QuerySet<TestRecord> queryset;

    constexpr int           RECORD_COUNT = 10001; // Just 1 over capacity
    std::vector<TestRecord> records;
    records.reserve(RECORD_COUNT);

    for (int i = 1; i <= RECORD_COUNT; ++i) {
        records.emplace_back(i, i, "Test");
    }

    auto insert_result = queryset.insert(std::span<const TestRecord>(records));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT all rows - will grow to 20K
    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& retrieved = select_result.value();
    ASSERT_EQ(retrieved.size(), RECORD_COUNT);

    auto last_it = std::ranges::next(retrieved.begin(), RECORD_COUNT - 1);
    EXPECT_EQ(last_it->value, 10001); // The one that triggered growth
}

// Test: SELECT with very large result set (100K rows)
// Tests multiple exponential growth cycles
TEST_F(SelectLargeTest, SelectVeryLargeDataset) {
    QuerySet<TestRecord> queryset;

    // 100K records: 10K → 20K → 40K → 80K → 160K (4 growth cycles)
    constexpr int RECORD_COUNT = 100000;

    // Insert in batches to avoid hitting SQLite limits
    constexpr int BATCH_SIZE = 10000;
    for (int batch = 0; batch < RECORD_COUNT / BATCH_SIZE; ++batch) {
        std::vector<TestRecord> batch_records;
        batch_records.reserve(BATCH_SIZE);

        for (int i = 1; i <= BATCH_SIZE; ++i) {
            int const record_num = batch * BATCH_SIZE + i;
            batch_records.emplace_back(record_num, record_num, std::format("B{}", batch));
        }

        auto insert_result = queryset.insert(std::span<const TestRecord>(batch_records));
        ASSERT_TRUE(insert_result.has_value()) << "Batch " << batch << " INSERT failed";
    }

    // SELECT all 100K rows
    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT 100K rows failed: " << select_result.error().message();

    const auto& retrieved = select_result.value();
    ASSERT_EQ(retrieved.size(), RECORD_COUNT) << "Should retrieve all 100K records";

    // Verify data integrity at key boundaries
    EXPECT_EQ(retrieved.begin()->value, 1);
    EXPECT_EQ(std::ranges::next(retrieved.begin(), 10000)->value, 10001); // After initial capacity
    EXPECT_EQ(std::ranges::next(retrieved.begin(), 20000)->value, 20001); // After first growth
    EXPECT_EQ(std::ranges::next(retrieved.begin(), 40000)->value, 40001); // After second growth
    EXPECT_EQ(std::ranges::next(retrieved.begin(), 80000)->value, 80001); // After third growth
    auto last_it = std::ranges::next(retrieved.begin(), RECORD_COUNT - 1);
    EXPECT_EQ(last_it->value, RECORD_COUNT);
}
